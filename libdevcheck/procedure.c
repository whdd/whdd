#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include "utils.h"
#include "procedure.h"

int dc_procedure_register(DC_Procedure *procedure) {
    procedure->next = dc_ctx_global->procedure_list;
    dc_ctx_global->procedure_list = procedure;
    int options_num = 0;
    while (procedure->options && procedure->options[options_num].name)
        options_num++;
    procedure->options_num = options_num;
    dc_ctx_global->nb_procedures++;
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

int dc_get_nb_procedures() {
    return dc_ctx_global->nb_procedures;
}

DC_Procedure *dc_get_next_procedure(DC_Procedure *prev) {
    return prev ? prev->next : dc_ctx_global->procedure_list;
}

DC_Procedure *dc_get_procedure_by_index(int index) {
    DC_Procedure *entry = dc_ctx_global->procedure_list;
    for (int i = 0; entry && (i < index); i++)
        entry = entry->next;
    return entry;
}

int dc_procedure_open(DC_Procedure *procedure, DC_Dev *dev, DC_ProcedureCtx **ctx_arg, DC_OptionSetting options[]) {
    DC_ProcedureCtx *ctx = calloc(1, sizeof(*ctx));
    int i;
    int ret;
    if (!ctx)
        goto fail_ctx;

    ctx->priv = calloc(1, procedure->priv_data_size);
    if (!ctx->priv)
        goto fail_priv;

    for (i = 0; procedure->options && procedure->options[i].name; i++) {
        DC_ProcedureOption *opt = &procedure->options[i];
        DC_OptionSetting setting;
        setting.name = opt->name;
        setting.value = NULL;
        procedure->suggest_default_value(dev, &setting);
        switch (opt->type) {
            case DC_ProcedureOptionType_eInt64:
                ret = sscanf(setting.value, "%"PRId64, (int64_t*)((uint8_t*)ctx->priv + opt->offset));
                break;
            case DC_ProcedureOptionType_eString:
                *(const char**)((uint8_t*)ctx->priv + opt->offset) = setting.value;
                break;
        }
    }
    if (options) {
        int arg_i;
        for (arg_i = 0; options[arg_i].name; arg_i++) {
            for (i = 0; procedure->options[i].name; i++) {
                DC_ProcedureOption *opt = &procedure->options[i];
                // fprintf(stderr, "looking at argument '%s' and defined option '%s'\n", options[arg_i].name, opt->name);
                if (strcmp(options[arg_i].name, opt->name))
                    continue;
                switch (opt->type) {
                    case DC_ProcedureOptionType_eInt64:
                        ret = sscanf(options[arg_i].value, "%"PRId64, (int64_t*)((uint8_t*)ctx->priv + opt->offset));
                        if (ret != 1)
                            goto fail_priv;
                        break;
                    case DC_ProcedureOptionType_eString:
                        *(const char**)((uint8_t*)ctx->priv + opt->offset) = strdup(options[arg_i].value);
                        break;
                }
            }
        }
    }
    ctx->dev = dev;
    ctx->procedure = procedure;
    *ctx_arg = ctx;
    return procedure->open(ctx);

fail_priv:
    free(ctx);
    // TODO Free option strings buffers
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

void _dc_proc_time_pre(DC_ProcedureCtx *ctx) {
    int r = clock_gettime(DC_BEST_CLOCK, &ctx->time_pre);
    assert(!r);
}

void _dc_proc_time_post(DC_ProcedureCtx *ctx) {
    int r = clock_gettime(DC_BEST_CLOCK, &ctx->time_post);
    assert(!r);
    ctx->report.blk_access_time = (ctx->time_post.tv_sec - ctx->time_pre.tv_sec) * 1000000 +
        (ctx->time_post.tv_nsec - ctx->time_pre.tv_nsec) / 1000;
}
