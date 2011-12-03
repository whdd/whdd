#ifndef LIBDEVCHECK_H
#define LIBDEVCHECK_H

#include "device.h"

#ifndef LIBDEVCHECK_PRIV_H
struct dc_ctx;
typedef struct dc_ctx DC_Ctx;
#endif // LIBDEVCHECK_PRIV_H

/**
 * This func initializes underlying libraries,
 * internal structures connectivity object, and so on.
 * Call this once at app beginning
 * And store returned pointer for further use of lib.
 */
DC_Ctx *dc_init(void);
void dc_finish(DC_Ctx *ctx);

#ifndef LIBDEVCHECK_PRIV_H
struct dc_dev_list;
typedef struct dc_dev_list DC_DevList;
#endif // LIBDEVCHECK_PRIV_H

/**
 * Return array of testable block devices
 */
DC_DevList *dc_dev_list(DC_Ctx *dc_ctx);
void dc_dev_list_free(DC_DevList *list);
int dc_dev_list_size(DC_DevList *list);
DC_Dev *dc_dev_list_get_entry(DC_DevList *list, int index);



#endif // LIBDEVCHECK_H
