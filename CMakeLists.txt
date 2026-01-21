cmake_minimum_required(VERSION 2.8.4)
include(ExternalProject)
project(whdd)

option(STATIC "Build static binaries" OFF)
option(CLI "Build whdd-cli" OFF)

set(CMAKE_C_FLAGS "-std=gnu99 -D_GNU_SOURCE -pthread -Wall -Wextra -Wno-missing-field-initializers ${CFLAGS}")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g -ggdb ${CMAKE_C_FLAGS}")

set(CLI_SRCS
    cli/main.c
    ui_mutual.c
    )

set(CUI_SRCS
    cui/main.c
    cui/ncurses_convenience.c
    cui/vis.c
    ui_mutual.c
    cui/sliding_window_renderer.c
    cui/whole_space_renderer.c
    )

set(LIBDEVCHECK_SRCS
    libdevcheck/procedure.c
    libdevcheck/libdevcheck.c
    libdevcheck/read_test.c
    libdevcheck/utils.c
    libdevcheck/posix_write_zeros.c
    libdevcheck/log.c
    libdevcheck/ata.c
    libdevcheck/scsi.c
    libdevcheck/copy.c
    libdevcheck/copy_read_strategies.c
    libdevcheck/render.c
    libdevcheck/hpa_set.c
    libdevcheck/smart_show.c
    )

include_directories(
    cli
    cui
    libdevcheck
    .
    )

include(CheckSymbolExists)
check_symbol_exists(CLOCK_MONOTONIC_RAW "time.h" HAVE_CLOCK_MONOTONIC_RAW)
configure_file(libdevcheck/config.h.in ${CMAKE_SOURCE_DIR}/config.h)

set_source_files_properties(version.h PROPERTIES GENERATED true HEADER_FILE_ONLY TRUE)
add_custom_target(version ALL)
add_custom_command(TARGET version COMMAND ${CMAKE_SOURCE_DIR}/version.sh ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/version.h)

if (${CLI})
    add_executable(whdd-cli
        ${CLI_SRCS}
        ${LIBDEVCHECK_SRCS}
        )
    add_dependencies(whdd-cli version)
    target_link_libraries(whdd-cli rt pthread)
    install(TARGETS whdd-cli DESTINATION sbin)
endif(${CLI})

add_executable(whdd
    ${CUI_SRCS}
    ${LIBDEVCHECK_SRCS}
    )

add_dependencies(whdd version)
target_link_libraries(whdd rt pthread)

if (${STATIC})
    # full static build
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static -no-pie")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -no-pie")
    # Set vars containing to deps builds
    set(NCURSES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/ncurses/install/")
    set(DIALOG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/dialog/dialog-prefix/src/dialog/")

    # Set include paths
    include_directories(BEFORE SYSTEM
        ${NCURSES_DIR}/include
        ${NCURSES_DIR}/include/ncursesw
        ${DIALOG_DIR}
        )

    add_subdirectory(external/dialog)
    add_dependencies(whdd dialog)

    add_library(ncurses_lib STATIC IMPORTED)
    set_property(TARGET ncurses_lib PROPERTY IMPORTED_LOCATION "${NCURSES_DIR}/lib/libncursesw.a")
    add_library(menu_lib STATIC IMPORTED)
    set_property(TARGET menu_lib PROPERTY IMPORTED_LOCATION "${NCURSES_DIR}/lib/libmenuw.a")
    add_library(tinfo_lib STATIC IMPORTED)
    set_property(TARGET tinfo_lib PROPERTY IMPORTED_LOCATION "${NCURSES_DIR}/lib/libtinfow.a")
    add_library(dialog_lib STATIC IMPORTED)
    set_property(TARGET dialog_lib PROPERTY IMPORTED_LOCATION "${DIALOG_DIR}/libdialog.a")

    target_link_libraries(whdd dialog_lib)
    target_link_libraries(whdd menu_lib)
    target_link_libraries(whdd ncurses_lib)
    target_link_libraries(whdd tinfo_lib)
    target_link_libraries(whdd dl)
    target_link_libraries(whdd m)

else (${STATIC})

    set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")
    find_package(DIALOG REQUIRED)
    if (NOT DIALOG_FOUND)
        message(FATAL_ERROR "Dialog library not found")
    endif(NOT DIALOG_FOUND)
    include_directories(${DIALOG_INCLUDE_DIRS})

    find_library(MENUW_LIBRARY menuw)
    find_library(TINFO_LIBRARY tinfo)
    find_library(NCURSESW_LIBRARY ncursesw)
    if (NOT (MENUW_LIBRARY AND NCURSESW_LIBRARY))
        message(FATAL_ERROR "Ncurses libraries (ncursesw, menuw) not found")
    endif(NOT (MENUW_LIBRARY AND NCURSESW_LIBRARY))

    if (TINFO_LIBRARY)
        target_link_libraries(whdd ${TINFO_LIBRARY})
    endif (TINFO_LIBRARY)

    target_link_libraries(whdd
        ${DIALOG_LIBRARIES} ${MENUW_LIBRARY} ${NCURSESW_LIBRARY} rt pthread)

    install(TARGETS whdd DESTINATION sbin)

    # Packaging stuff
    # 'make package_source' to make tarball
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "WHDD is a diagnostic and recovery tool for block devices.")
    set(CPACK_PACKAGE_VENDOR "Andrey Utkin <andrey.od.utkin@gmail.com>")
    set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README")
    set(CPACK_PACKAGE_VERSION ${WHDD_VERSION})
    #set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
    set(CPACK_SOURCE_PACKAGE_FILE_NAME
        "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION}")
    set(CPACK_SOURCE_GENERATOR TBZ2)
    include(CPack)

endif (${STATIC})
