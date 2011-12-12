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

struct dc_test_report {
    uint64_t blk_index;
    uint64_t blk_access_time; // in mcs
    int blk_access_errno;
    uint64_t blks_total;
    uint64_t blk_size;
};
typedef struct dc_test_report DC_TestReport;

typedef int (*TestCB)(DC_Dev *dev, void *priv, DC_TestReport *report);

int dc_dev_readtest(DC_Dev *dev, TestCB callback, void *priv);
int dc_dev_zerofilltest(DC_Dev *dev, TestCB callback, void *priv);

#endif // DEVICE_H
