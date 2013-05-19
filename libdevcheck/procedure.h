#ifndef PROCEDURE_H
#define PROCEDURE_H

#include "libdevcheck.h"
#include "device.h"
#include <pthread.h>

#define DC_PROC_FLAG_DESTRUCTIVE 1

struct dc_procedure {
    char *name;
    char *long_name;
    int flags;  // For DC_PROC_FLAG_*
    int priv_data_size;
    int (*open)(DC_ProcedureCtx *procedure);
    int (*perform)(DC_ProcedureCtx *ctx);
    void (*close)(DC_ProcedureCtx *ctx);

    struct dc_procedure *next;
};

int dc_procedure_register(DC_Procedure *procedure);
DC_Procedure *dc_find_procedure(char *name);

typedef struct dc_rational {
    uint64_t num;  // numerator
    uint64_t den;  // denominator
} DC_Rational;

typedef enum {
    DC_BlockStatus_eOk = 0,
    DC_BlockStatus_eError,   // Generic error condition
    DC_BlockStatus_eTimeout,
    DC_BlockStatus_eUnc,
    DC_BlockStatus_eIdnf,
    DC_BlockStatus_eAbrt,
    // More to come with details from low-level drive commands
} DC_BlockStatus;

struct dc_procedure_ctx {
    void* priv; // for procedure private context
    DC_Dev *dev; // device which is operated
    DC_Procedure *procedure;
    uint64_t blk_size;  // set by procedure on .open()
    uint64_t current_lba;  // updated by procedure on .perform()
    DC_Rational progress;  // updated by procedure on .perform()
    int interrupt; // if set to 1 by frontend, then looped processing must stop
    // TODO interrupt is now meant for loop, think of interrupting blocking perform operation
    int finished; // if 1, then looped processing has finished

    struct dc_test_report {
        uint64_t blk_access_time; // in mcs
        DC_BlockStatus blk_status;
    } report; // updated by procedure on .perform()
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
