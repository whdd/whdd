#include <stdio.h>

#include "ui_mutual.h"
#include "utils.h"

void ui_dev_descr_format(char *buf, int bufsize, DC_Dev *dev) {
    char cap_buf[30];
    char native_cap_buf[30];
    char *cap_print, *native_cap_print, *primary_cap_print;  // TODO fix weird commaprint behaviour
    primary_cap_print = cap_print = commaprint(dev->capacity, cap_buf, sizeof(cap_buf));
    native_cap_print = commaprint(dev->native_capacity, native_cap_buf, sizeof(native_cap_buf));

    if (!dev->ata_capable) {
        snprintf(buf, bufsize, "%s %s bytes; ATA capabilities unavailable", dev->model_str, cap_print);
        return;
    }
    char warning[50] = "; no HPA";
    if (dev->native_capacity > 0) {
        primary_cap_print = native_cap_print;
        if (dev->native_capacity != dev->capacity)
            snprintf(warning, sizeof(warning), " !!! HPA enabled %s bytes", cap_print);
    }
    snprintf(buf, bufsize, "%s %s bytes%s", dev->model_str, primary_cap_print, warning);
}
