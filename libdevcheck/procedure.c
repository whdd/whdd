#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include "utils.h"
#include "procedure.h"

int dc_procedure_register(DC_Procedure *procedure) {
    procedure->next = dc_ctx_global->procedure_list;
    dc_ctx_global->procedure_list = procedure;
    return 0;
}

DC_Procedure *dc_find_procedure(char *name) {
    DC_Procedure *iter = dc_ctx_global->procedure_list;
    while (iter) {
        if (!strcmp(iter->name, name))
            break;
        iter = iter->next;
    }
    return iter;
}

int dc_procedure_open(DC_Procedure *procedure, DC_Dev *dev, DC_ProcedureCtx **ctx_arg) {
    DC_ProcedureCtx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        goto fail_ctx;

    ctx->priv = calloc(1, procedure->priv_data_size);
    if (!ctx->priv)
        goto fail_priv;

    ctx->dev = dev;
    ctx->procedure = procedure;
    *ctx_arg = ctx;
    return procedure->open(ctx);

fail_priv:
    free(ctx);
fail_ctx:
    return 1;
}

void dc_procedure_close(DC_ProcedureCtx *ctx) {
    ctx->procedure->close(ctx);
    free(ctx->priv);
    free(ctx);
}

int dc_procedure_perform_loop(DC_ProcedureCtx *ctx, ProcedureDetachedLoopCB callback, void *callback_priv) {
    int r;
    int ret = 0;
    int perform_ret;
    while (!ctx->interrupt) {
        if (ctx->progress.num >= ctx->progress.den)
            break;
        perform_ret = ctx->procedure->perform(ctx);
        r = callback(ctx, callback_priv);
        if (perform_ret) {
            ret = perform_ret;
            break;
        }
        if (r) {
            ret = r;
            break;
        }
    }
    ctx->finished = 1;
    return ret;
}

int dc_procedure_perform_loop_detached(DC_ProcedureCtx *ctx, ProcedureDetachedLoopCB callback,
        void *callback_priv, pthread_t *tid
        ) {
    struct args_pack {
        DC_ProcedureCtx *ctx;
        ProcedureDetachedLoopCB callback;
        void *callback_priv;
    };
    void *thread_proc(void *packed_args) {
        struct args_pack *args = packed_args;
        dc_realtime_scheduling_enable_with_prio(1);
        dc_procedure_perform_loop(args->ctx, args->callback, args->callback_priv);
        free(args);
        return NULL;
    }

    int r;
    struct args_pack *args = calloc(1, sizeof(*args));
    if (!args)
        return 1;
    args->ctx = ctx;
    args->callback = callback;
    args->callback_priv = callback_priv;

    r = pthread_create(tid, NULL, thread_proc, args);
    if (r)
        return r;
    return 0;
}

