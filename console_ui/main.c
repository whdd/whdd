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

typedef enum {
    CliAction_eInvalid = -1,
    CliAction_eExit = 0,
    CliAction_eShowSmart = 1,
    CliAction_eProcRead = 2,
    CliAction_eProcWriteZeros = 3,
} CliAction;

static int proc_render_cb(DC_ProcedureCtx *ctx, void *callback_priv);
CliAction request_and_get_cli_action();
DC_Dev *request_and_get_device();

int main() {
    printf(WHDD_ABOUT);
    int r;
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
                DC_Dev *chosen_dev = request_and_get_device();
                if (!chosen_dev) {
                    printf("Invalid choice\n");
                    break;
                }
                char *text;
                text = dc_dev_smartctl_text(chosen_dev->dev_path, " -i -s on -A ");
                if (text)
                    printf("%s\n", text);
                else
                    printf("Failed. Check your permissions.\n");
                free(text);
                break;
            }
            case CliAction_eProcRead:
            case CliAction_eProcWriteZeros:
            {
                DC_Dev *chosen_dev = request_and_get_device();
                if (!chosen_dev) {
                    printf("Invalid choice\n");
                    break;
                }
                char *act_name = action == CliAction_eProcRead ? "posix_read" : "posix_write_zeros";
                DC_Procedure *act = dc_find_procedure(act_name);
                assert(act);
                printf("Going to perform test %s (%s)\n", act->name, act->long_name);
                if (act->flags & DC_PROC_FLAG_DESTRUCTIVE) {
                    printf("This will destroy all data on device %s (%s). Are you sure? (y/n)\n",
                            chosen_dev->dev_fs_name, chosen_dev->model_str);
                    char ans = 'n';
                    r = scanf("\n%c", &ans);
                    if (ans != 'y')
                        break;
                }
                DC_ProcedureCtx *actctx;
                r = dc_procedure_open(act, chosen_dev, &actctx);
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
            ctx->current_lba,
            ctx->report.blk_status == 0 ? "OK" : "FAILED",
            ctx->report.blk_access_time,
            ctx->progress.num, ctx->progress.den);
    fflush(stdout);
    return 0;
}

CliAction request_and_get_cli_action() {
    // print procedures list
    printf("\nChoose action #:\n"
            "0) Exit\n"
            "1) Show SMART attributes\n"
            "2) Perform read test\n"
            "3) Perform 'write zeros' test\n"
          );
    int chosen_action_ind;
    int r = scanf("%d", &chosen_action_ind);
    if (r != 1 || chosen_action_ind < 0 || chosen_action_ind > 3)
        return CliAction_eInvalid;
    return (CliAction)chosen_action_ind;
}

DC_Dev *request_and_get_device() {
    int i;
    DC_DevList *devlist = dc_dev_list();
    int devs_num = dc_dev_list_size(devlist);
    for (i = 0; i < devs_num; i++) {
        DC_Dev *dev = dc_dev_list_get_entry(devlist, i);
        printf(
                "#%d:" // index
                " %s" // /dev/name
                " %s" // model name
                // TODO human-readable size
                " %"PRIu64" bytes" // size
                "\n"
                ,i
                ,dev->dev_fs_name
                ,dev->model_str
                ,dev->capacity
              );
    }
    printf("Choose device by #:\n");
    int chosen_dev_ind;
    int r = scanf("%d", &chosen_dev_ind);
    if (r != 1 || chosen_dev_ind < 0 || chosen_dev_ind >= devs_num)
        return NULL;
    return dc_dev_list_get_entry(devlist, chosen_dev_ind);
}
