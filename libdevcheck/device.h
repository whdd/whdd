#ifndef DEVICE_H
#define DEVICE_H

#include <inttypes.h>

struct dc_dev {
    char *dev_fs_name;
    char *dev_path;
    char *model_str;
    int ata_capable;
    uint64_t capacity;
    uint64_t native_capacity;
    int mounted;
    struct dc_dev *next;
};

#endif // DEVICE_H
