#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <wchar.h>
#include <locale.h>
#include <errno.h>
#include <unistd.h>
#include <curses.h>
#include <dialog.h>
#include "libdevcheck.h"
#include "device.h"
#include "action.h"
#include "vis.h"
#include "ncurses_convenience.h"

#define ACT_EXIT 0
#define ACT_ATTRS 1
#define ACT_READ 2
#define ACT_ZEROFILL 3

static int global_init(void);
static void global_fini(void);
static int menu_choose_device(DC_DevList *devlist);
static int menu_choose_action(DC_Dev *dev);
static void show_smart_attrs(DC_Dev *dev);
static int render_test_read(DC_Dev *dev);
static int render_test_zerofill(DC_Dev *dev);

static int action_find_start_perform_until_interrupt(DC_Dev *dev, char *act_name,
                ActionDetachedLoopCB callback, void *callback_priv);
static int readtest_cb(DC_ActionCtx *ctx, void *callback_priv);

DC_Ctx *dc_ctx;

int main() {
    int r;
    r = global_init();
    if (r) {
        fprintf(stderr, "init fail\n");
        return r;
    }
    // get list of devices
    DC_DevList *devlist = dc_dev_list(dc_ctx);
    assert(devlist);

    while (1) {
        // draw menu of device choice
        int chosen_dev_ind;
        chosen_dev_ind = menu_choose_device(devlist);
        if (chosen_dev_ind < 0)
            break;

        DC_Dev *chosen_dev = dc_dev_list_get_entry(devlist, chosen_dev_ind);
        if (!chosen_dev) {
            printw("No device with index %d\n", chosen_dev_ind);
            return 1;
        }
        // draw actions menu
        int chosen_action_ind;
        chosen_action_ind = menu_choose_action(chosen_dev);
        if (chosen_action_ind < 0)
            break;

        switch (chosen_action_ind) {
        case ACT_EXIT:
            return 0;
        case ACT_ATTRS:
            show_smart_attrs(chosen_dev);
            break;
        case ACT_READ:
            render_test_read(chosen_dev);
            break;
        case ACT_ZEROFILL:
            render_test_zerofill(chosen_dev);
            break;
        default:
            printw("Wrong action index\n");
            break;
        }
    } // while(1)

    return 0;
}

static int global_init(void) {
    // TODO check all retcodes
    setlocale(LC_ALL, "");
    initscr();
    init_dialog(stdin, stdout);

    start_color();
    init_my_colors();
    noecho();
    setscrreg(0, 0);
    scrollok(stdscr, TRUE);
    keypad(stdscr, TRUE);

    WINDOW *footer = subwin(stdscr, 1, COLS, LINES-1, 0);
    wbkgd(footer, COLOR_PAIR(MY_COLOR_WHITE_ON_BLUE));
    wprintw(footer, " WHDD rev. " WHDD_VERSION);

    refresh();
    // init libdevcheck
    dc_ctx = dc_init();
    assert(dc_ctx);
    assert(atexit(global_fini) == 0);
    return 0;
}

static void global_fini(void) {
    clear();
    endwin();
}

static int menu_choose_device(DC_DevList *devlist) {
    int r;
    int devs_num = dc_dev_list_size(devlist);
    if (devs_num == 0) { printw("No devices found\n"); return -1; }

    ITEM **items = calloc(devs_num + 1, sizeof(ITEM*));
    assert(items);
    items[devs_num] = NULL;

    int i;
    for (i = 0; i < devs_num; i++) {
        DC_Dev *dev = dc_dev_list_get_entry(devlist, i);
        char *dev_descr;
        r = asprintf(&dev_descr,
                "#%d:" // index
                " %s" // /dev/name
                " %s" // model name
                // TODO human-readable size
                " %"PRIu64" bytes" // size
                ,i
                ,dev->dev_fs_name
                ,dev->model_str
                ,dev->capacity
              );
        assert(r != -1);
        items[i] = new_item(dev_descr, "");
        assert(items[i]);
    }

    int chosen_dev_ind = menu_helper(items, "Choose device");
    i = 0;
    while (items[i]) { free_item(items[i]); i++; }
    free(items);
    return chosen_dev_ind;
}

static int menu_choose_action(DC_Dev *dev) {
    ITEM *items[5];
    items[0] = new_item("Exit", "");
    assert(items[0]);
    items[1] = new_item("Show SMART attributes", "");
    assert(items[1]);
    items[2] = new_item("Perform read test", "");
    assert(items[2]);
    items[3] = new_item("Perform 'write zeros' test", "");
    assert(items[3]);
    items[4] = NULL;

    int chosen_action_ind = menu_helper(items, "Choose action");
    int i = 0;
    while (items[i]) { free_item(items[i]); i++; }
    return chosen_action_ind;
}

static void show_smart_attrs(DC_Dev *dev) {
    char *text;
    text = dc_dev_smartctl_text(dev, "-A -i");
    if (text)
        printw("%s\n", text);
    free(text);
    refresh();
}

static int render_test_read(DC_Dev *dev) {
    WINDOW *legend = derwin(stdscr, LINES-2, LEGEND_WIDTH, 1, COLS-LEGEND_WIDTH); // leave 1st and last lines untouched
    show_legend(legend);
    // TODO print info about device
    wrefresh(legend);
    WINDOW *vis = derwin(stdscr, LINES-2, COLS-LEGEND_WIDTH-1, 1, 0); // leave 1st and last lines untouched
    scrollok(vis, TRUE);
    wrefresh(vis);

    action_find_start_perform_until_interrupt(dev, "readtest", readtest_cb, (void*)vis);

    wclear(vis);
    wclear(legend);
    delwin(vis);
    delwin(legend);
    return 0;
}

static int render_test_zerofill(DC_Dev *dev) {
    int r;
    char *ask;
    r = asprintf(&ask, "This will destroy all data on device %s (%s). Are you sure?",
            dev->dev_fs_name, dev->model_str);
    assert(r != -1);
    r = dialog_yesno("Confirmation", ask, 0, 0);
    // Yes = 0 (FALSE), No = 1, Escape = -1
    free(ask);
    if (/* No */ r)
        return 0;

    WINDOW *legend = derwin(stdscr, LINES-2, LEGEND_WIDTH, 1, COLS-LEGEND_WIDTH); // leave 1st and last lines untouched
    show_legend(legend);
    // TODO print info about device
    wrefresh(legend);
    WINDOW *vis = derwin(stdscr, LINES-2, COLS-LEGEND_WIDTH-1, 1, 0); // leave 1st and last lines untouched
    scrollok(vis, TRUE);
    wrefresh(vis);

    action_find_start_perform_until_interrupt(dev, "zerofill", readtest_cb, (void*)vis);

    wclear(vis);
    wclear(legend);
    delwin(vis);
    delwin(legend);
    return 0;
}

static int readtest_cb(DC_ActionCtx *ctx, void *callback_priv) {
    if (ctx->report.blk_access_errno)
        print_vis((WINDOW*)callback_priv, error_vis);
    else
        print_vis((WINDOW*)callback_priv, choose_vis(ctx->report.blk_access_time));
    // TODO update avg speed, stats on access time
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
        printw("Action init fail\n");
        return 1;
    }

    sigemptyset(&set);
    sigaddset(&set, SIGQUIT);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);
    r = sigprocmask(SIG_BLOCK, &set, NULL);
    if (r) {
        printw("sigprocmask failed: %d\n", r);
        goto fail;
    }

    r = dc_action_perform_loop_detached(actctx, callback, callback_priv, &tid);
    if (r) {
        printw("dc_action_perform_loop_detached fail\n");
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
                printw("sigtimedwait fail, errno %d\n", errno);
        }
    }

    r = pthread_join(tid, NULL);
    assert(!r);

    r = sigprocmask(SIG_UNBLOCK, &set, NULL);
    if (r) {
        printw("sigprocmask failed: %d\n", r);
        goto fail;
    }

    dc_action_close(actctx);
    return 0;

fail:
    dc_action_close(actctx);
    return 1;
}

