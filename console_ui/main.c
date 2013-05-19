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

static int procedure_find_start_perform_until_interrupt(DC_Dev *dev, char *act_name,
        ProcedureDetachedLoopCB callback, void *callback_priv);
static int proc_render_cb(DC_ProcedureCtx *ctx, void *callback_priv);

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
    int devs_num = dc_dev_list_size(devlist);
    if (devs_num == 0) { printf("No devices found\n"); return 0; }

    while (1) {
    // print procedures list
    printf("\nChoose procedure #:\n"
            "0) Exit\n"
            "1) Show SMART attributes\n"
            "2) Perform read test\n"
            "3) Perform 'write zeros' test\n"
          );
    int chosen_procedure_ind;
    r = scanf("%d", &chosen_procedure_ind);
    if (r != 1) {
        printf("Wrong input for procedure index\n");
        return 1;
    }
    if (chosen_procedure_ind == 0) {
        printf("Exiting due to chosen procedure\n");
        return 0;
    }

    int i;
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
    r = scanf("%d", &chosen_dev_ind);
    if (r != 1) {
        printf("Wrong input for device index\n");
        return 1;
    }
    DC_Dev *chosen_dev = dc_dev_list_get_entry(devlist, chosen_dev_ind);
    if (!chosen_dev) {
        printf("No device with index %d\n", chosen_dev_ind);
        return 1;
    }

    switch (chosen_procedure_ind) {
    case 1:
        ;
        char *text;
        text = dc_dev_smartctl_text(chosen_dev->dev_path, " -i -s on -A ");
        if (text)
            printf("%s\n", text);
        free(text);
        break;
    case 2:
        printf("Performing read test ");
        procedure_find_start_perform_until_interrupt(chosen_dev, "posix_read", proc_render_cb, NULL);
        break;
    case 3:
        printf("This will destroy all data on device %s (%s). Are you sure? (y/n)\n",
                chosen_dev->dev_fs_name, chosen_dev->model_str);
        char ans = 'n';
        r = scanf("\n%c", &ans);
        if (ans != 'y')
            break;
        printf("Performing zeros write test ");
        procedure_find_start_perform_until_interrupt(chosen_dev, "posix_write_zeros", proc_render_cb, NULL);
        break;
    default:
        printf("Wrong procedure index\n");
        break;
    }
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

static int procedure_find_start_perform_until_interrupt(DC_Dev *dev, char *act_name,
        ProcedureDetachedLoopCB callback, void *callback_priv
        ) {
    int r;
    siginfo_t siginfo;
    sigset_t set;
    pthread_t tid;
    DC_Procedure *act = dc_find_procedure(act_name);
    assert(act);
    DC_ProcedureCtx *actctx;
    r = dc_procedure_open(act, dev, &actctx);
    if (r) {
        printf("Procedure init fail\n");
        return 1;
    }

    sigemptyset(&set);
    sigaddset(&set, SIGQUIT);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);
    r = sigprocmask(SIG_BLOCK, &set, NULL);
    if (r) {
        printf("sigprocmask failed: %d\n", r);
        goto fail;
    }

    r = dc_procedure_perform_loop_detached(actctx, callback, callback_priv, &tid);
    if (r) {
        printf("dc_procedure_perform_loop_detached fail\n");
        goto fail;
    }

    struct timespec finish_check_interval = { .tv_sec = 1, .tv_nsec = 0 };
    while (!actctx->finished) {
        r = sigtimedwait(&set, &siginfo, &finish_check_interval);
        if (r > 0) { // got signal `r`
            actctx->interrupt = 1;
            break;
        } else { // "fail"
            if ((errno == EAGAIN) || // timed out
                    (errno == EINTR)) // interrupted by non-catched signal
                continue;
            else
                printf("sigtimedwait fail, errno %d\n", errno);
        }
    }

    r = pthread_join(tid, NULL);
    assert(!r);

    r = sigprocmask(SIG_UNBLOCK, &set, NULL);
    if (r) {
        printf("sigprocmask failed: %d\n", r);
        goto fail;
    }

    dc_procedure_close(actctx);
    return 0;

fail:
    dc_procedure_close(actctx);
    return 1;
}

