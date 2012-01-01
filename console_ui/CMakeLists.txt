
add_executable (whdd-cli main.c)
add_dependencies (whdd-cli devcheck)
include_directories ( ${PROJECT_SOURCE_DIR}/libdevcheck
    ${PROJECT_BINARY_DIR}/libdevcheck )

target_link_libraries ( whdd-cli ${PROJECT_BINARY_DIR}/libdevcheck/libdevcheck.a
    rt pthread )

install (TARGETS whdd-cli DESTINATION bin)
