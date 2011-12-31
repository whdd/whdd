
add_executable (whdd-cli main.c)
include_directories ( ${PROJECT_SOURCE_DIR}/libdevcheck )

target_link_libraries ( whdd-cli ${PROJECT_BINARY_DIR}/libdevcheck/libdevcheck.a
    ${RT} pthread )

install (TARGETS whdd-cli DESTINATION bin)
