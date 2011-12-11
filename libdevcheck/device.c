#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include "libdevcheck.h"
#include "device.h"
#include "utils.h"

char *dc_dev_smartctl_text(DC_Dev *dev, char *options) {
    int r;
    char *command_line;
    r = asprintf(&command_line, "smartctl %s /dev/%s", options, dev->dev_fs_name);
    if (r == -1)
        return NULL;

    char *smartctl_output = cmd_output(command_line);
    free(command_line);

    return smartctl_output;
}

