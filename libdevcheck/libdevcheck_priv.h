#ifndef LIBDEVCHECK_PRIV_H
#define LIBDEVCHECK_PRIV_H

#include "device.h"

struct dc_ctx {
    int dummy;
};
typedef struct dc_ctx DC_Ctx;

struct dc_dev_list {
    DC_Dev *arr;
    int arr_size;
};
typedef struct dc_dev_list DC_DevList;

#endif // LIBDEVCHECK_PRIV_H
