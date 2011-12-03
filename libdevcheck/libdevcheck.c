#include <stdlib.h>
#include <assert.h>

#include "libdevcheck_priv.h"
#include "libdevcheck.h"

DC_Ctx *dc_init(void) {
    DC_Ctx *ctx = calloc(1, sizeof(*ctx));
    return ctx;
}

void dc_finish(DC_Ctx *ctx) {
    free(ctx);
}

DC_DevList *dc_dev_list(DC_Ctx *dc_ctx) {
    // FIXME stub
    DC_DevList *list = calloc(1, sizeof(*list));
    assert(list);
    list->arr = NULL;
    list->arr_size = 0;
    return list;
}

void dc_dev_list_free(DC_DevList *list) {
    while (list->arr) {
        DC_Dev *next = list->arr->next;
        free(list->arr);
        list->arr = next;
    }
    free(list);
}

int dc_dev_list_size(DC_DevList *list) {
    return list->arr_size;
}

DC_Dev *dc_dev_list_get_entry(DC_DevList *list, int index) {
    DC_Dev *dev = list->arr;
    while (index > 0) {
        if (!dev) return NULL;
        dev = dev->next;
        index--;
    }
    return dev;
}

