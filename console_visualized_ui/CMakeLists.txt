
add_executable (whdd-curses main.c vis.c ncurses_convenience.c
    dialog_convenience.c)
include_directories ( ${PROJECT_SOURCE_DIR}/libdevcheck )
target_link_libraries (whdd-curses
    ${RT} ${NCURSESW} ${MENUW} m pthread
    ${PROJECT_BINARY_DIR}/libdevcheck/libdevcheck.a
    ${PROJECT_SOURCE_DIR}/contrib/x86/libdialog.a)
set_property (TARGET whdd-curses PROPERTY COMPILE_FLAGS -std=c99)

install (TARGETS whdd-curses DESTINATION bin)
