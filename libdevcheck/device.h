#ifndef DEVICE_H
#define DEVICE_H

#include <inttypes.h>

struct dc_dev {
    char *dev_fs_name;
    char *model_str;
    uint64_t capacity;
    struct dc_dev *next;
};
typedef struct dc_dev DC_Dev;

#endif // DEVICE_H
