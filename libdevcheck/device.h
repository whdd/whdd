#ifndef DEVICE_H
#define DEVICE_H

#include <inttypes.h>

struct dc_dev {
    char *dev_fs_name;
    char *dev_path;
    char *model_str;
    uint64_t capacity;
    struct dc_dev *next;
};
typedef struct dc_dev DC_Dev;

char *dc_dev_smartctl_text(DC_Dev *dev, char *options);

#endif // DEVICE_H
