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
CliAction request_and_get_cli_action();
DC_Dev *request_and_get_device();
void set_hpa_dialog(DC_Dev *dev);

static int ask_option_value(DC_OptionSetting *setting, DC_ProcedureOption *option) {
    char *suggested_value;
    char entered_value[200];
    const char *param_type_str;
    switch (option->type) {
        case DC_ProcedureOptionType_eInt64:
            param_type_str = "numeric";
            asprintf(&suggested_value, "%"PRId64, option->default_val.i64);
            break;
        case DC_ProcedureOptionType_eString:
            param_type_str = "string";
            asprintf(&suggested_value, "%s", option->default_val.str);
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
        CliAction action = request_and_get_cli_action();
        switch (action) {
            case CliAction_eInvalid: {
                printf("Invalid choice\n");
                break;
            }
            case CliAction_eExit: {
                printf("Exiting due to choice\n");
                return 0;
            }
            case CliAction_eShowSmart: {
                char *text;
                text = dc_dev_smartctl_text(chosen_dev->dev_path, " -i -s on -A ");
                if (text)
                    printf("%s\n", text);
                else
                    printf("Failed. Check your permissions.\n");
                free(text);
                break;
            }
            case CliAction_eSetHpa: {
                set_hpa_dialog(chosen_dev);
                break;
            }
            case CliAction_eProcRead:
            case CliAction_eProcWriteZeros:
            case CliAction_eProcCopy:
            case CliAction_eProcCopyDamaged:
            {
                char *act_name = actions[action].name;
                DC_Procedure *act = dc_find_procedure(act_name);
                assert(act);
                printf("Going to perform test %s (%s)\n", act->name, act->long_name);
                if (act->flags & DC_PROC_FLAG_DESTRUCTIVE) {
                    printf("This will destroy all data on device %s (%s). Are you sure? (y/n)\n",
                            chosen_dev->dev_fs_name, chosen_dev->model_str);
                    char ans[10] = "n";
                    char_ret = fgets(ans, sizeof(ans), stdin);
                    if (!char_ret || ans[0] != 'y')
                        break;
                }
                DC_OptionSetting *option_set = calloc(act->options_num + 1, sizeof(DC_OptionSetting));
                int i;
                r = 0;
                for (i = 0; i < act->options_num; i++) {
                    option_set[i].name = act->options[i].name;
                    r = ask_option_value(&option_set[i], &act->options[i]);
                    if (r)
                        break;
                }
                if (r)
                    break;
                DC_ProcedureCtx *actctx;
                r = dc_procedure_open(act, chosen_dev, &actctx, option_set);
                if (r) {
                    printf("Procedure init fail\n");
                    return 1;
                }
                printf("Performing on device %s with block size %"PRId64"\n",
                        chosen_dev->dev_path, actctx->blk_size);
                procedure_perform_until_interrupt(actctx, proc_render_cb, NULL);
                break;
            }
        }  // switch (action)
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

CliAction request_and_get_cli_action() {
    // print procedures list
    printf("\nChoose action #:\n");
    int i;
    for (i = 0; i < CliAction_eAfterMaxValidIndex; i++)
        printf("%d) %s\n", (int)actions[i].menu_number, actions[i].name);
    char input[10];
    int chosen_action_ind;
    char *char_ret = fgets(input, sizeof(input), stdin);
    if (!char_ret)
        return CliAction_eInvalid;
    int r = sscanf(input, "%d", &chosen_action_ind);
    if (r != 1 || chosen_action_ind < 0 || chosen_action_ind >= CliAction_eAfterMaxValidIndex)
        return CliAction_eInvalid;
    return (CliAction)chosen_action_ind;
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

void set_hpa_dialog(DC_Dev *dev) {
    if (dev->native_capacity == -1) {
        printf("Querying native max LBA failed on this device. Setting max LBA will surely fail.");
        return;
    }
    printf("This can make your data unreachable on device %s (%s). Are you sure? (y/n)\n",
            dev->dev_fs_name, dev->model_str);
    char ans[30] = "n";
    char *char_ret = fgets(ans, sizeof(ans), stdin);
    if (!char_ret || ans[0] != 'y')
        return;
    uint64_t current_max_lba = dev->capacity / 512 - 1;
    uint64_t native_max_lba = dev->native_capacity / 512 - 1;
    char suggested_input[30];
    snprintf(suggested_input, sizeof(suggested_input), "%"PRIu64, native_max_lba);
    char descr[200];
    printf("Enter max addressable LBA. Native max LBA is %"PRIu64", current max LBA is %"PRIu64"\nIn other words: reachable_capacity_in_bytes = (max_LBA + 1) * 512\n", native_max_lba, current_max_lba);
    uint64_t set_max_lba;
    char_ret = fgets(ans, sizeof(ans), stdin);
    if (!char_ret)
        return;
    int r = sscanf(ans, "%"PRIu64, &set_max_lba);
    if (r != 1) {
        printf("Invalid input\n");
        return;
    }
    dc_dev_set_max_lba(dev->dev_path, set_max_lba);
}
