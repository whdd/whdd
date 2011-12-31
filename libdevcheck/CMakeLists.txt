
add_custom_command (
    OUTPUT version.h
    COMMAND ./version.sh > version.h
)

add_library (devcheck STATIC libdevcheck.c device.c action.c utils.c readtest.c
    zerofill.c version.h)
