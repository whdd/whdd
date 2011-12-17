#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "action.h"

int dc_action_register(DC_Ctx *ctx, DC_Action *action) {
    action->next = ctx->action_list;
    ctx->action_list = action;
    return 0;
}

DC_Action *dc_find_action(DC_Ctx *ctx, char *name) {
    DC_Action *iter = ctx->action_list;
    while (iter) {
        if (!strcmp(iter->name, name))
            break;
        iter = iter->next;
    }
    return iter;
}

int dc_action_open(DC_Action *action, DC_Dev *dev, DC_ActionCtx **ctx_arg) {
    DC_ActionCtx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        goto fail_ctx;

    ctx->priv = calloc(1, action->priv_data_size);
    if (!ctx->priv)
        goto fail_priv;

    ctx->dev = dev;
    ctx->action = action;
    *ctx_arg = ctx;
    return action->open(ctx);

fail_priv:
    free(ctx);
fail_ctx:
    return 1;
}

int dc_action_perform(DC_ActionCtx *ctx) {
    int r;
    struct timespec pre, post;

    if (ctx->performs_total && (ctx->performs_executed >= ctx->performs_total))
        return 1; // TODO retcodes enum

    clock_gettime(CLOCK_MONOTONIC_RAW, &pre);
    errno = 0;
    r = ctx->action->perform(ctx);
    ctx->report.blk_access_errno = errno;
    clock_gettime(CLOCK_MONOTONIC_RAW, &post);
    ctx->report.blk_access_time = (post.tv_sec - pre.tv_sec) * 1000000 +
        (post.tv_nsec - pre.tv_nsec) / 1000;
    ctx->performs_executed++;
    return r;
}

void dc_action_close(DC_ActionCtx *ctx) {
    ctx->action->close(ctx);
    free(ctx->priv);
    free(ctx);
}

int dc_action_perform_loop(DC_ActionCtx *ctx, ActionDetachedLoopCB callback, void *callback_priv) {
    int r;
    int ret = 0;
    int perform_ret;
    while (!ctx->interrupt) {
        perform_ret = dc_action_perform(ctx);
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

int dc_action_perform_loop_detached(DC_ActionCtx *ctx, ActionDetachedLoopCB callback,
        void *callback_priv, pthread_t *tid
        ) {
    struct args_pack {
        DC_ActionCtx *ctx;
        ActionDetachedLoopCB callback;
        void *callback_priv;
    };
    void *thread_proc(void *packed_args) {
        struct args_pack *args = packed_args;
        dc_action_perform_loop(args->ctx, args->callback, args->callback_priv);
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

