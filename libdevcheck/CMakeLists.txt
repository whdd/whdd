
include (CheckSymbolExists)
check_symbol_exists (CLOCK_MONOTONIC_RAW "time.h" HAVE_CLOCK_MONOTONIC_RAW)
configure_file (${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/config.h)

add_custom_command (
    OUTPUT version.h
    COMMAND cd ${PROJECT_SOURCE_DIR}/libdevcheck/ && ./version.sh >
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
)

include_directories ( ${CMAKE_CURRENT_BINARY_DIR} )
add_library (devcheck STATIC libdevcheck.c device.c action.c utils.c readtest.c
    zerofill.c version.h)
