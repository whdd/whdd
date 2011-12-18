#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <wchar.h>
#include <locale.h>
#include <errno.h>
#include <unistd.h>
#include "libdevcheck.h"
#include "device.h"
#include "action.h"

static int action_find_start_perform_until_interrupt(DC_Dev *dev, char *act_name,
                ActionDetachedLoopCB callback, void *callback_priv);
static int readtest_cb(DC_ActionCtx *ctx, void *callback_priv);
static int zerofill_cb(DC_ActionCtx *ctx, void *callback_priv);
static void show_legend(void);

DC_Ctx *dc_ctx;

int main() {
    int r;
    setlocale(LC_ALL, "");
    wprintf(L"%s", WHDD_ABOUT);
    // init libdevcheck
    dc_ctx = dc_init();
    assert(dc_ctx);
    // get list of devices
    DC_DevList *devlist = dc_dev_list(dc_ctx);
    assert(devlist);
    // show list of devices
    int devs_num = dc_dev_list_size(devlist);
    if (devs_num == 0) { wprintf(L"No devices found\n"); return 0; }

    while (1) {
    wprintf(L"\n\n");
    int i;
    for (i = 0; i < devs_num; i++) {
        DC_Dev *dev = dc_dev_list_get_entry(devlist, i);
        wprintf(
                L"#%d:" // index
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
    wprintf(L"Choose device by #:\n");
    int chosen_dev_ind;
    r = scanf("%d", &chosen_dev_ind);
    if (r != 1) {
        wprintf(L"Wrong input for device index\n");
        return 1;
    }
    DC_Dev *chosen_dev = dc_dev_list_get_entry(devlist, chosen_dev_ind);
    if (!chosen_dev) {
        wprintf(L"No device with index %d\n", chosen_dev_ind);
        return 1;
    }

    // print actions list
    wprintf(L"\nChoose action #:\n"
            "0) Exit\n"
            "1) Show SMART attributes\n"
            "2) Perform read test\n"
            "3) Perform 'write zeros' test\n"
          );
    int chosen_action_ind;
    r = scanf("%d", &chosen_action_ind);
    if (r != 1) {
        wprintf(L"Wrong input for action index\n");
        return 1;
    }
    if (chosen_action_ind == 0) {
        wprintf(L"Exiting due to chosen action\n");
        return 0;
    }

    switch (chosen_action_ind) {
    case 1:
        ;
        char *text;
        text = dc_dev_smartctl_text(chosen_dev, "-A -i");
        if (text)
            wprintf(L"%s\n", text);
        free(text);
        break;
    case 2:
        show_legend();
        sleep(1);
        action_find_start_perform_until_interrupt(chosen_dev, "readtest", readtest_cb, NULL);
        break;
    case 3:
        wprintf(L"This will destroy all data on device %s (%s). Are you sure? (y/n)\n",
                chosen_dev->dev_fs_name, chosen_dev->model_str);
        char ans = 'n';
        r = scanf("\n%c", &ans);
        if (ans != 'y')
            break;
        show_legend();
        sleep(1);
        action_find_start_perform_until_interrupt(chosen_dev, "zerofill", zerofill_cb, NULL);
        break;
    default:
        wprintf(L"Wrong action index\n");
        break;
    }
    } // while(1)

    return 0;
}

typedef struct vis_t {
    uint64_t access_time; // in mcs
    wchar_t vis; // visual representation
    int attrs;
    int fgcolor;
    int bgcolor;
} vis_t;

static vis_t bs_vis[]   = {
                           { 3000,   L'\u2591', 0, 37, 40 }, // gray light shade
                           { 10000,  L'\u2592', 0, 37, 40 }, // gray medium shade
                           { 50000,  L'\u2593', 0, 37, 40 }, // gray dark shade
                           { 150000, L'\u2588', 0, 32, 40 }, // green full block
                           { 500000, L'\u2588', 0, 31, 40 }, // red full block
};
static vis_t exceed_vis =  { 0,      L'\u2588', 1, 31, 40 }; // bold red full block
static vis_t error_vis  =  { 0,      L'!',      1, 35, 40 }; // pink exclam sign

static vis_t choose_vis(uint64_t access_time) {
    int i;
    for (i = 0; i < sizeof(bs_vis)/sizeof(*bs_vis); i++)
        if (access_time < bs_vis[i].access_time)
            return bs_vis[i];
    return exceed_vis;
}


static void print_vis(vis_t vis) {
    wprintf(L"%c[%d;%d;%dm%lc%c[0m", 0x1B, vis.attrs, vis.fgcolor, vis.bgcolor, vis.vis, 0x1B);
}

static void show_legend(void) {
    wprintf(L"Legend:\n");
    int i;
    for (i = 0; i < sizeof(bs_vis)/sizeof(*bs_vis); i++) {
        print_vis(bs_vis[i]);
        wprintf(L" access time < %"PRIu64" ms\n", bs_vis[i].access_time / 1000);
    }
    print_vis(exceed_vis);
    wprintf(L" access time exceeds any of above\n");
    print_vis(error_vis);
    wprintf(L" access error\n");
}

static int readtest_cb(DC_ActionCtx *ctx, void *callback_priv) {
    if (ctx->performs_executed == 1) {
        wprintf(L"Performing read-test of '%s' with block size of %"PRIu64" bytes\n",
                ctx->dev->dev_fs_name, ctx->blk_size);
    }
    if (ctx->report.blk_access_errno)
        print_vis(error_vis);
    else
        print_vis(choose_vis(ctx->report.blk_access_time));
    fflush(stdout);
    return 0;
}

static int zerofill_cb(DC_ActionCtx *ctx, void *callback_priv) {
    if (ctx->performs_executed == 0) {
        wprintf(L"Performing 'write zeros' test of '%s' with block size of %"PRIu64" bytes\n",
                ctx->dev->dev_fs_name, ctx->blk_size);
    }
    if (ctx->report.blk_access_errno)
        print_vis(error_vis);
    else
        print_vis(choose_vis(ctx->report.blk_access_time));
    fflush(stdout);
    return 0;
}

static int action_find_start_perform_until_interrupt(DC_Dev *dev, char *act_name,
        ActionDetachedLoopCB callback, void *callback_priv
        ) {
    int r;
    siginfo_t siginfo;
    sigset_t set;
    pthread_t tid;
    DC_Action *act = dc_find_action(dc_ctx, act_name);
    assert(act);
    DC_ActionCtx *actctx;
    r = dc_action_open(act, dev, &actctx);
    if (r) {
        wprintf(L"Action init fail\n");
        return 1;
    }

    sigemptyset(&set);
    sigaddset(&set, SIGQUIT);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);
    r = sigprocmask(SIG_BLOCK, &set, NULL);
    if (r) {
        wprintf(L"sigprocmask failed: %d\n", r);
        goto fail;
    }

    r = dc_action_perform_loop_detached(actctx, callback, callback_priv, &tid);
    if (r) {
        wprintf(L"dc_action_perform_loop_detached fail\n");
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
                wprintf(L"sigtimedwait fail, errno %d\n", errno);
        }
    }

    r = pthread_join(tid, NULL);
    assert(!r);

    r = sigprocmask(SIG_UNBLOCK, &set, NULL);
    if (r) {
        wprintf(L"sigprocmask failed: %d\n", r);
        goto fail;
    }

    dc_action_close(actctx);
    return 0;

fail:
    dc_action_close(actctx);
    return 1;
}

