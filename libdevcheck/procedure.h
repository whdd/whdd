#ifndef PROCEDURE_H
#define PROCEDURE_H

#include "libdevcheck.h"
#include "device.h"
#include <pthread.h>

struct dc_procedure {
    char *name;
    int priv_data_size;
    int (*open)(DC_ProcedureCtx *procedure);
    int (*perform)(DC_ProcedureCtx *ctx);
    void (*close)(DC_ProcedureCtx *ctx);

    struct dc_procedure *next;
};

int dc_procedure_register(DC_Procedure *procedure);
DC_Procedure *dc_find_procedure(char *name);

struct dc_procedure_ctx {
    void* priv; // for procedure private context
    DC_Dev *dev; // device which is operated
    DC_Procedure *procedure;
    uint64_t blk_size;
    uint64_t blk_index;
    uint64_t blks_total;
    uint64_t performs_executed; // how many times .perform() took place. For callbacks handiness
    uint64_t performs_total; // how many times .perform() from start to finish has to be done
    int interrupt; // if 1, then looped processing must stop
    int finished; // if 1, then looped processing has finished

    struct dc_test_report {
        uint64_t blk_access_time; // in mcs
        int blk_access_errno;
    } report; // report of last perform
};

int dc_procedure_open(DC_Procedure *procedure, DC_Dev *dev, DC_ProcedureCtx **ctx);
int dc_procedure_perform(DC_ProcedureCtx *ctx);
void dc_procedure_close(DC_ProcedureCtx *ctx);

typedef int (*ProcedureDetachedLoopCB)(DC_ProcedureCtx *ctx, void *callback_priv);
int dc_procedure_perform_loop(DC_ProcedureCtx *ctx, ProcedureDetachedLoopCB callback, void *callback_priv);
int dc_procedure_perform_loop_detached(DC_ProcedureCtx *ctx, ProcedureDetachedLoopCB callback,
        void *callback_priv, pthread_t *tid
        );

#endif // PROCEDURE_H
