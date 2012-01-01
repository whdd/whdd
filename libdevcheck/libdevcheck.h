#ifndef LIBDEVCHECK_H
#define LIBDEVCHECK_H

#include "config.h"
#define WHDD_ABOUT "WHDD - disk drives diagnostic tool\n" \
    "Revision " WHDD_VERSION "\n" \
    "License: GNU GPL\n" \
    "Sources: https://github.com/krieger-od/whdd\n" \
    "Author: Andrey 'Krieger' Utkin <andrey.krieger.utkin@gmail.com>\n" \
    "Directed by:\n" \
        "\tVitaliy 'Rozik' Roziznaniy <rozik@homei.net.ua>\n" \
        "\thttp://rozik.od.ua\n" \
        "\thttp://hdd.od.ua\n" \
    "Heil comrade Stalin!\n"

#include "objects_def.h"
#include "device.h"
#include "action.h"

extern clockid_t DC_BEST_CLOCK;

struct dc_ctx {
    DC_Action *action_list;
};

struct dc_dev_list {
    DC_Dev *arr;
    int arr_size;
};

/**
 * This func initializes underlying libraries,
 * internal structures connectivity object, and so on.
 * Call this once at app beginning
 * And store returned pointer for further use of lib.
 */
DC_Ctx *dc_init(void);
void dc_finish(DC_Ctx *ctx);

/**
 * Return array of testable block devices
 */
DC_DevList *dc_dev_list(DC_Ctx *dc_ctx);
void dc_dev_list_free(DC_DevList *list);
int dc_dev_list_size(DC_DevList *list);
DC_Dev *dc_dev_list_get_entry(DC_DevList *list, int index);

#endif // LIBDEVCHECK_H
