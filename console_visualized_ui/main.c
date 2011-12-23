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
#include "dialog_convenience.h"

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
                ActionDetachedLoopCB callback, void *callback_priv, int *interrupted);
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
            dialog_msgbox("Error", "Wrong action index", 0, 0, 1);
            continue;
        }
    } // while(1)

    return 0;
}

static int global_init(void) {
    // TODO check all retcodes
    setlocale(LC_ALL, "");
    initscr();
    init_dialog(stdin, stdout);
    dialog_vars.item_help = 0;

    start_color();
    init_my_colors();
    noecho();
    cbreak();
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
    if (devs_num == 0) {
        dialog_msgbox("Info", "No devices found", 0, 0, 1);
        return -1;
    }

    char **items = calloc( 2 * devs_num, sizeof(char*));
    assert(items);

    int i;
    for (i = 0; i < devs_num; i++) {
        DC_Dev *dev = dc_dev_list_get_entry(devlist, i);
        char *dev_descr;
        r = asprintf(&dev_descr,
                "%s" // model name
                // TODO human-readable size
                " %"PRIu64" bytes" // size
                ,dev->model_str
                ,dev->capacity
              );
        assert(r != -1);
        items[2*i] = strdup(dev->dev_fs_name);
        items[2*i+1] = dev_descr;
    }

    clear_body();
    int chosen_dev_ind = my_dialog_menu("Choose device", "", 0, 0, 0, devs_num, items);
    for (i = 0; i < devs_num; i++) {
        free(items[2*i]);
        free(items[2*i+1]);
    }
    free(items);

    return chosen_dev_ind;
}

static int menu_choose_action(DC_Dev *dev) {
    char *items[4 * 2];
    items[0] = strdup("Exit");
    items[2] = strdup("Show SMART attributes");
    items[4] = strdup("Perform read test");
    items[6] = strdup("Perform 'write zeros' test");
    int i;
    // this fuckin libdialog makes me code crappy
    for (i = 0; i < 4; i++)
        items[2*i+1] = strdup("");

    clear_body();
    int chosen_action_ind = my_dialog_menu("Choose action", "", 0, 0, 0, 4, items);
    for (i = 0; i < 8; i++)
        free(items[i]);
    return chosen_action_ind;
}

static void show_smart_attrs(DC_Dev *dev) {
    char *text;
    text = dc_dev_smartctl_text(dev, "-A -i");
    dialog_msgbox("SMART Attributes", text ? : "Getting attrs failed", LINES-6, 0, 1);
    if (text)
        free(text);
    refresh();
}

typedef struct rwtest_render_priv {
    WINDOW *legend; // not for updating, just to free afterwards
    WINDOW *vis; // window to print vis-char for each block
    //WINDOW *access_time_stats;
    WINDOW *avg_speed;
    //WINDOW *cur_speed;
    WINDOW *eta;
    //WINDOW *progress;
    WINDOW *summary;

    struct timespec start_time;
} rwtest_render_priv_t;

static rwtest_render_priv_t *rwtest_render_priv_prepare(void) {
    rwtest_render_priv_t *this = calloc(1, sizeof(*this));
    if (!this)
        return NULL;
    this->legend = derwin(stdscr, 7, LEGEND_WIDTH, 3, COLS-LEGEND_WIDTH); // leave 1st and last lines untouched
    assert(this->legend);
    show_legend(this->legend);
    wrefresh(this->legend);
    this->vis = derwin(stdscr, LINES-2, COLS-LEGEND_WIDTH-1, 1, 0); // leave 1st and last lines untouched
    assert(this->vis);
    scrollok(this->vis, TRUE);
    wrefresh(this->vis);

    this->avg_speed = derwin(stdscr, 1, LEGEND_WIDTH, 2, COLS-LEGEND_WIDTH);
    assert(this->avg_speed);

    this->eta = derwin(stdscr, 1, LEGEND_WIDTH, 1, COLS-LEGEND_WIDTH);
    assert(this->eta);

    this->summary = derwin(stdscr, 10, LEGEND_WIDTH, 10, COLS-LEGEND_WIDTH);
    assert(this->eta);

    return this;
}
void rwtest_render_flush(rwtest_render_priv_t *this) {
    wrefresh(this->vis);
    wrefresh(this->avg_speed);
    wrefresh(this->eta);
}

void rwtest_render_priv_destroy(rwtest_render_priv_t *this) {
    delwin(this->legend);
    delwin(this->vis);
    delwin(this->avg_speed);
    delwin(this->eta);
    delwin(this->summary);
    free(this);
    clear_body();
}

static int render_test_read(DC_Dev *dev) {
    int r;
    int interrupted;
    rwtest_render_priv_t *windows = rwtest_render_priv_prepare();
    wprintw(windows->summary,
            "Read test of drive\n"
            "%s (%s)\n"
            "Ctrl+C to abort\n",
            dev->dev_path, dev->model_str);
    wrefresh(windows->summary);
    r = action_find_start_perform_until_interrupt(dev, "readtest", readtest_cb, (void*)windows, &interrupted);
    assert(!r);
    rwtest_render_flush(windows);
    if (interrupted)
        wprintw(windows->summary, "Aborted.\n");
    wprintw(windows->summary, "Press any key");
    wrefresh(windows->summary);
    beep();
    getch();
    rwtest_render_priv_destroy(windows);
    return 0;
}

static int render_test_zerofill(DC_Dev *dev) {
    int r;
    int interrupted;
    char *ask;
    r = asprintf(&ask, "This will destroy all data on device %s (%s). Are you sure?",
            dev->dev_fs_name, dev->model_str);
    assert(r != -1);
    r = dialog_yesno("Confirmation", ask, 0, 0);
    // Yes = 0 (FALSE), No = 1, Escape = -1
    free(ask);
    if (/* No */ r)
        return 0;

    rwtest_render_priv_t *windows = rwtest_render_priv_prepare();
    wprintw(windows->summary,
            "Write test of drive\n"
            "%s (%s)\n"
            "Ctrl+C to abort\n",
            dev->dev_path, dev->model_str);
    wrefresh(windows->summary);
    r = action_find_start_perform_until_interrupt(dev, "zerofill", readtest_cb, (void*)windows, &interrupted);
    assert(!r);
    rwtest_render_flush(windows);
    if (interrupted)
        wprintw(windows->summary, "Aborted.\n");
    wprintw(windows->summary, "Press any key");
    wrefresh(windows->summary);
    beep();
    getch();
    rwtest_render_priv_destroy(windows);
    return 0;
}

static int readtest_cb(DC_ActionCtx *ctx, void *callback_priv) {
    int r;
    rwtest_render_priv_t *priv = callback_priv;

    if (ctx->performs_executed == 1) {
        r = clock_gettime(CLOCK_MONOTONIC_RAW, &priv->start_time);
        assert(!r);
    } else {
        if ((ctx->performs_executed % 1000) == 0) {
            struct timespec now;
            r = clock_gettime(CLOCK_MONOTONIC_RAW, &now);
            assert(!r);
            uint64_t bytes_processed = ctx->performs_executed * ctx->blk_size;
            uint64_t time_elapsed = now.tv_sec - priv->start_time.tv_sec;
            if (time_elapsed > 0) {
                uint64_t avg_processing_speed = bytes_processed / time_elapsed; // Byte/s
                // capacity / speed = total_time
                // total_time = elapsed + eta
                // eta = total_time - elapsed
                // eta = capacity / speed  -  elapsed
                uint64_t eta = ctx->dev->capacity / avg_processing_speed - time_elapsed;

                wclear(priv->avg_speed);
                wprintw(priv->avg_speed, "AVG [% 7"PRIu64" kb/s]", avg_processing_speed / 1024);
                wrefresh(priv->avg_speed);

                unsigned int minute, second;
                second = eta % 60;
                minute = eta / 60;
                wclear(priv->eta);
                wprintw(priv->eta, "EST: %10u:%02u", minute, second);
                wrefresh(priv->eta);
            }
        }
    }

    if (ctx->report.blk_access_errno)
        print_vis(priv->vis, error_vis);
    else
        print_vis(priv->vis, choose_vis(ctx->report.blk_access_time));

    if ((ctx->performs_executed % 10) == 0) {
        wrefresh(priv->vis);
    }

    if (ctx->performs_total == ctx->performs_executed) {
        wprintw(priv->summary, "Completed.\n");
        wrefresh(priv->summary);
    }
    return 0;
}

static int action_find_start_perform_until_interrupt(DC_Dev *dev, char *act_name,
        ActionDetachedLoopCB callback, void *callback_priv, int *interrupted
        ) {
    int r;
    siginfo_t siginfo;
    sigset_t set;
    pthread_t tid;
    DC_Action *act = dc_find_action(dc_ctx, act_name);
    assert(act);
    DC_ActionCtx *actctx;
    *interrupted = 0;
    r = dc_action_open(act, dev, &actctx);
    if (r) {
        dialog_msgbox("Error", "Action init fail", 0, 0, 1);
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
            *interrupted = 1;
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

