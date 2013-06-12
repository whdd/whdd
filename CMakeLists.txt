cmake_minimum_required(VERSION 2.8)
project(whdd)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

set(CMAKE_C_FLAGS "-std=gnu99 -pthread -Wall -Wextra -Wno-missing-field-initializers ${CFLAGS}")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g -ggdb ${CMAKE_C_FLAGS}")

# If we in git repository, add git revision too
if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
	find_package(Git)
	if(GIT_FOUND)
		exec_program(${GIT_EXECUTABLE} ${CMAKE_SOURCE_DIR}
			ARGS describe --always
			OUTPUT_VARIABLE WHDD_REVISION)
	endif(GIT_FOUND)
	set(WHDD_VERSION "${WHDD_VERSION}-g${WHDD_REVISION}")
endif(EXISTS "${CMAKE_SOURCE_DIR}/.git")

set(CLI_SRCS
	cli/main.c
	ui_mutual.c
)

set(CUI_SRCS
	cui/dialog_convenience.c
	cui/main.c
	cui/render.c
	cui/ncurses_convenience.c
	cui/vis.c
	ui_mutual.c
)

set(LIBDEVCHECK_SRCS
	libdevcheck/procedure.c
	libdevcheck/libdevcheck.c
	libdevcheck/posix_read.c
	libdevcheck/utils.c
	libdevcheck/posix_write_zeros.c
	libdevcheck/log.c
	libdevcheck/sgio_ata_verify_ext.c
	libdevcheck/ata.c
	libdevcheck/scsi.c
)

include_directories(
	cli
	cui
	libdevcheck
	.
)

include(CheckSymbolExists)
check_symbol_exists(CLOCK_MONOTONIC_RAW "time.h" HAVE_CLOCK_MONOTONIC_RAW)
configure_file(libdevcheck/config.h.in
	./config.h)

set_source_files_properties(version.h PROPERTIES GENERATED true HEADER_FILE_ONLY TRUE)
add_custom_target(version ALL)
add_custom_command(TARGET version COMMAND ./version.sh . version.h)

add_executable(whdd-cli
	${CLI_SRCS}
	${LIBDEVCHECK_SRCS}
)

add_executable(whdd
	${CUI_SRCS}
	${LIBDEVCHECK_SRCS}
)

include_directories(${DIALOG_INCLUDE_DIRS})
add_dependencies(whdd-cli version)
add_dependencies(whdd version)

target_link_libraries(whdd-cli rt pthread)

find_package(DIALOG REQUIRED)
find_library(MENUW_LIBRARY menuw)
find_library(NCURSESW_LIBRARY ncursesw)
if(DIALOG_FOUND AND MENUW_LIBRARY AND NCURSESW_LIBRARY)
	target_link_libraries(whdd
		${DIALOG_LIBRARIES} ${MENUW_LIBRARY} ${NCURSESW_LIBRARY} rt pthread)
else()
	message(FATAL_ERROR "Dialog or menuw library not found!")
endif(DIALOG_FOUND AND MENUW_LIBRARY AND NCURSESW_LIBRARY)
install(TARGETS whdd-cli whdd DESTINATION sbin)

# Packaging stuff
# 'make package_source' to make tarball
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "WHDD is a diagnostic and recovery tool for block devices.")
set(CPACK_PACKAGE_VENDOR "Andrey 'Krieger' Utkin <andrey.krieger.utkin@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README")
set(CPACK_PACKAGE_VERSION ${WHDD_VERSION})
#set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
set(CPACK_SOURCE_PACKAGE_FILE_NAME
	"${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION}")
set(CPACK_SOURCE_GENERATOR TBZ2)
include(CPack)
