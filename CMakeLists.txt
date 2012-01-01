cmake_minimum_required(VERSION 2.8)
project(whdd)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

set(WHDD_VERSION "1.0rc4")

# If we in git repository, add git revision too
if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
	find_package(Git)
	if(GIT_FOUND)
		exec_program(${GIT_EXECUTABLE} ${CMAKE_SOURCE_DIR}
			ARGS describe --always
			OUTPUT_VARIABLE WHDD_REVISION)
	endif(GIT_FOUND)
endif(EXISTS "${CMAKE_SOURCE_DIR}/.git")

set(WHDD_VERSION "${WHDD_VERSION}-${WHDD_REVISION}")

set(CONSOLE_UI_DIR console_ui)
set(CONSOLE_UI_SRCS
	${CONSOLE_UI_DIR}/main.c
)

set(CONSOLE_VISUALIZED_UI_DIR console_visualized_ui)
set(CONSOLE_VISUALIZED_UI_SRCS
	${CONSOLE_VISUALIZED_UI_DIR}/dialog_convenience.c
	${CONSOLE_VISUALIZED_UI_DIR}/main.c
	${CONSOLE_VISUALIZED_UI_DIR}/ncurses_convenience.c
	${CONSOLE_VISUALIZED_UI_DIR}/vis.c
)

set(LIBDEVCHECK_DIR libdevcheck)
set(LIBDEVCHECK_SRCS
	${LIBDEVCHECK_DIR}/action.c
	${LIBDEVCHECK_DIR}/device.c
	${LIBDEVCHECK_DIR}/libdevcheck.c
	${LIBDEVCHECK_DIR}/readtest.c
	${LIBDEVCHECK_DIR}/utils.c
	${LIBDEVCHECK_DIR}/zerofill.c
)

include_directories(
	${CONSOLE_UI_DIR}
	${CONSOLE_VISUALIZED_UI_DIR}
	${LIBDEVCHECK_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
)

include(CheckSymbolExists)
check_symbol_exists(CLOCK_MONOTONIC_RAW "time.h" HAVE_CLOCK_MONOTONIC_RAW)
configure_file(${LIBDEVCHECK_DIR}/config.h.in
	${CMAKE_CURRENT_BINARY_DIR}/config.h)

add_executable(whdd-cli
	${CONSOLE_UI_SRCS}
	${LIBDEVCHECK_SRCS}
)

add_executable(whdd-curses
	${CONSOLE_VISUALIZED_UI_SRCS}
	${LIBDEVCHECK_SRCS}
)

include_directories(${DIALOG_INCLUDE_DIRS})
target_link_libraries(whdd-cli rt pthread)

find_package(DIALOG REQUIRED)
find_library(MENUW_LIBRARY menuw)
if(DIALOG_FOUND AND MENUW_LIBRARY)
	target_link_libraries(whdd-curses 
		${DIALOG_LIBRARIES} ${MENUW_LIBRARY} rt pthread)
else()
	message(FATAL_ERROR "Dialog or menuw library not found!")
endif(DIALOG_FOUND AND MENUW_LIBRARY)
install(TARGETS whdd-cli whdd-curses DESTINATION sbin)
