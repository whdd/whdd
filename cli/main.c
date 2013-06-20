#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "libdevcheck.h"
#include "device.h"
#include "procedure.h"
#include "utils.h"
#include "ui_mutual.h"

static int proc_render_cb(DC_ProcedureCtx *ctx, void *callback_priv);
DC_Procedure *request_and_get_cli_action();
DC_Dev *request_and_get_device();

static int ask_option_value(DC_OptionSetting *setting, DC_ProcedureOption *option) {
    char *suggested_value = setting->value;
    char entered_value[200];
    const char *param_type_str;
    switch (option->type) {
        case DC_ProcedureOptionType_eInt64:
            param_type_str = "numeric";
            break;
        case DC_ProcedureOptionType_eString:
            param_type_str = "string";
            break;
    }
    printf("Please enter %s parameter: %s (%s)\n",
            param_type_str, option->name, option->help);
    printf("Enter empty string for default value '%s'\n", suggested_value);
    char *ret = fgets(entered_value, sizeof(entered_value), stdin);
    if (!ret)
        return 1;
    printf("Got value: '%s'\n", entered_value);
    if (entered_value[0] == '\0' || entered_value[0] == '\n')
        snprintf(entered_value, sizeof(entered_value), "%s", suggested_value);
    printf("Result value: '%s'\n", entered_value);
    setting->value = strdup(entered_value);
    free(suggested_value);
    return 0;
}

int main() {
    printf(WHDD_ABOUT);
    printf("\nATTENTION! whdd-cli utility is purposed for development debugging, and as a fallback if whdd utility somehow fails to work. In other cases, consider using whdd utility, which should provide better usage experience.\n");
    int r;
    char *char_ret;
    // init libdevcheck
    r = dc_init();
    assert(!r);
    // get list of devices
    DC_DevList *devlist = dc_dev_list();
    assert(devlist);
    // show list of devices
    if (dc_dev_list_size(devlist) == 0) {
        printf("No devices found, go buy some :)\n");
        return 0;
    }

    while (1) {
        DC_Dev *chosen_dev = request_and_get_device();
        if (!chosen_dev) {
            printf("Invalid choice\n");
            break;
        }
        DC_Procedure *act = request_and_get_cli_action();
        if (!act)
            break;
        printf("Going to perform test %s (%s)\n", act->name, act->display_name);
        if (act->flags & DC_PROC_FLAG_INVASIVE) {
            printf("This operation is invasive, i.e. it may make your data unreachable or even destroy it completely. Are you sure you want to proceed it on %s (%s)? (y/n)\n",
                    chosen_dev->dev_fs_name, chosen_dev->model_str);
            char ans[10] = "n";
            char_ret = fgets(ans, sizeof(ans), stdin);
            if (!char_ret || ans[0] != 'y')
                continue;

            if (chosen_dev->mounted) {
                printf("This disk is mounted. Are you really sure you want to proceed? (y/n)");
                char_ret = fgets(ans, sizeof(ans), stdin);
                if (!char_ret || ans[0] != 'y')
                    continue;
            }
        }
        DC_OptionSetting *option_set = calloc(act->options_num + 1, sizeof(DC_OptionSetting));
        int i;
        r = 0;
        for (i = 0; i < act->options_num; i++) {
            option_set[i].name = act->options[i].name;
            r = act->suggest_default_value(chosen_dev, &option_set[i]);
            if (r) {
                dc_log(DC_LOG_ERROR, "Failed to get default value suggestion on '%s'", option_set[i].name);
                break;
            }
            r = ask_option_value(&option_set[i], &act->options[i]);
            if (r)
                break;
        }
        if (r)
            continue;
        DC_ProcedureCtx *actctx;
        r = dc_procedure_open(act, chosen_dev, &actctx, option_set);
        if (r) {
            printf("Procedure init fail\n");
            continue;
        }
        if (!act->perform)
            continue;
        printf("Performing on device %s with block size %"PRId64"\n",
                chosen_dev->dev_path, actctx->blk_size);
        procedure_perform_until_interrupt(actctx, proc_render_cb, NULL);
    } // while(1)

    return 0;
}

static int proc_render_cb(DC_ProcedureCtx *ctx, void *callback_priv) {
    if (ctx->progress.num == 1) {  // TODO eliminate such hacks
        printf("on device '%s' with block size of %"PRIu64" bytes\n",
                ctx->dev->dev_fs_name, ctx->blk_size);
    }
    printf("LBA #%"PRIu64" %s in %"PRIu64" mcs. Progress %"PRIu64"/%"PRIu64"\n",
            ctx->report.lba,
            ctx->report.blk_status == 0 ? "OK" : "FAILED",
            ctx->report.blk_access_time,
            ctx->progress.num, ctx->progress.den);
    fflush(stdout);
    return 0;
}

DC_Procedure *request_and_get_cli_action() {
    // print procedures list
    int i;
    DC_Procedure *procedure = NULL;
    printf("\nChoose action #:\n");
    printf("0) Exit\n");
    for (i = 0; i < dc_get_nb_procedures(); i++) {
        procedure = dc_get_next_procedure(procedure);
        printf("%d) %s\n", i + 1, procedure->display_name);
    }
    char input[10];
    int chosen_action_ind;
    char *char_ret = fgets(input, sizeof(input), stdin);
    if (!char_ret) {
        printf("Incorrect input\n");
        return NULL;
    }
    int r = sscanf(input, "%d", &chosen_action_ind);
    if (r != 1 || chosen_action_ind < 0 || chosen_action_ind > dc_get_nb_procedures()) {
        printf("Incorrect input\n");
        return NULL;
    }
    if (chosen_action_ind == 0) {
        printf("Exiting due to user choice\n");
        return NULL;
    }
    return dc_get_procedure_by_index(chosen_action_ind - 1);
}

DC_Dev *request_and_get_device() {
    int i;
    DC_DevList *devlist = dc_dev_list();
    int devs_num = dc_dev_list_size(devlist);
    printf("\nChoose device by #:\n");
    for (i = 0; i < devs_num; i++) {
        DC_Dev *dev = dc_dev_list_get_entry(devlist, i);
        char descr_buf[80];
        ui_dev_descr_format(descr_buf, sizeof(descr_buf), dev);
        printf("#%d: %s %s\n", i, dev->dev_fs_name, descr_buf);
    }
    char input[10];
    int chosen_dev_ind;
    char *char_ret = fgets(input, sizeof(input), stdin);
    if (!char_ret)
        return NULL;
    int r = sscanf(input, "%d", &chosen_dev_ind);
    if (r != 1 || chosen_dev_ind < 0 || chosen_dev_ind >= devs_num)
        return NULL;
    return dc_dev_list_get_entry(devlist, chosen_dev_ind);
}
