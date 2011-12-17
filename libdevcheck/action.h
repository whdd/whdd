#ifndef ACTION_H
#define ACTION_H

#include "libdevcheck.h"
#include "device.h"
#include <pthread.h>

struct dc_action {
    char *name;
    int priv_data_size;
    int (*open)(DC_ActionCtx *action);
    int (*perform)(DC_ActionCtx *ctx);
    void (*close)(DC_ActionCtx *ctx);

    struct dc_action *next;
};

int dc_action_register(DC_Ctx *ctx, DC_Action *action);
DC_Action *dc_find_action(DC_Ctx *ctx, char *name);

struct dc_action_ctx {
    void* priv; // for action private context
    DC_Dev *dev; // device which is operated
    DC_Action *action;
    uint64_t blk_size;
    uint64_t blk_index;
    uint64_t blks_total;
    uint64_t performs_executed; // how many times .perform() took place. For callbacks handiness
    uint64_t performs_total; // how many times .perform() from start to finish has to be done
    int interrupt; // if 1, then looped processing must stop

    struct dc_test_report {
        uint64_t blk_access_time; // in mcs
        int blk_access_errno;
    } report; // report of last perform
};

int dc_action_open(DC_Action *action, DC_Dev *dev, DC_ActionCtx **ctx);
int dc_action_perform(DC_ActionCtx *ctx);
void dc_action_close(DC_ActionCtx *ctx);

typedef int (*ActionDetachedLoopCB)(DC_ActionCtx *ctx, void *callback_priv);
int dc_action_perform_loop(DC_ActionCtx *ctx, ActionDetachedLoopCB callback, void *callback_priv);
int dc_action_perform_loop_detached(DC_ActionCtx *ctx, ActionDetachedLoopCB callback,
        void *callback_priv, pthread_t *tid
        );

#endif // ACTION_H
