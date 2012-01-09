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

#define REPORTS_BURST 20

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
static char *commaprint(uint64_t n, char *retbuf, size_t bufsize);

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
    scrollok(stdscr, FALSE);
    keypad(stdscr, TRUE);

    WINDOW *footer = subwin(stdscr, 1, COLS, LINES-1, 0);
    wbkgd(footer, COLOR_PAIR(MY_COLOR_WHITE_ON_BLUE));
    wprintw(footer, " WHDD rev. " WHDD_VERSION);

    wrefresh(footer);
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
    items[1] = strdup("Exit");
    items[3] = strdup("Show SMART attributes");
    items[5] = strdup("Perform read test");
    items[7] = strdup("Perform 'write zeros' test");
    int i;
    // this fuckin libdialog makes me code crappy
    for (i = 0; i < 4; i++)
        items[2*i] = strdup("");

    clear_body();
    int chosen_action_ind = my_dialog_menu("Choose action", "", 0, 0, 0, 4, items);
    for (i = 0; i < 8; i++)
        free(items[i]);
    return chosen_action_ind;
}

static void show_smart_attrs(DC_Dev *dev) {
    char *text;
    text = dc_dev_smartctl_text(dev, "-A");
    dialog_msgbox("SMART Attributes", text ? : "Getting attrs failed", LINES-6, COLS, 1);
    if (text)
        free(text);
    refresh();
}

typedef struct blk_report {
    uint64_t seqno;
    int access_errno;
    unsigned int access_time;
} blk_report_t;

typedef struct rwtest_render_priv {
    WINDOW *legend; // not for updating, just to free afterwards
    WINDOW *vis; // window to print vis-char for each block
    WINDOW *access_time_stats;
    WINDOW *avg_speed;
    //WINDOW *cur_speed;
    WINDOW *eta;
    //WINDOW *progress;
    WINDOW *summary;
    WINDOW *w_end_lba;
    WINDOW *w_cur_lba;

    struct timespec start_time;
    uint64_t access_time_stats_accum[7];
    uint64_t avg_processing_speed;
    uint64_t eta_time; // estimated time
    uint64_t cur_lba;

    pthread_t render_thread;
    int order_hangup; // if interrupted or completed, render remainings and end render thread

    // lockless ringbuffer
    blk_report_t reports[100*1000];
    uint64_t next_report_seqno_write;
    uint64_t next_report_seqno_read;
} rwtest_render_priv_t;

static rwtest_render_priv_t *rwtest_render_priv_prepare(void) {
    rwtest_render_priv_t *this = calloc(1, sizeof(*this));
    if (!this)
        return NULL;
    this->legend = derwin(stdscr, 7, LEGEND_WIDTH/2, 4, COLS-LEGEND_WIDTH); // leave 1st and last lines untouched
    assert(this->legend);
    this->access_time_stats = derwin(stdscr, 7, LEGEND_WIDTH/2, 4, COLS-LEGEND_WIDTH/2);
    assert(this->access_time_stats);
    show_legend(this->legend);
    this->vis = derwin(stdscr, LINES-3, COLS-LEGEND_WIDTH-1, 2, 0); // leave 1st and last lines untouched
    assert(this->vis);
    scrollok(this->vis, TRUE);
    wrefresh(this->vis);

    this->avg_speed = derwin(stdscr, 1, LEGEND_WIDTH, 2, COLS-LEGEND_WIDTH);
    assert(this->avg_speed);

    this->eta = derwin(stdscr, 1, LEGEND_WIDTH, 1, COLS-LEGEND_WIDTH);
    assert(this->eta);

    this->summary = derwin(stdscr, 10, LEGEND_WIDTH, 11, COLS-LEGEND_WIDTH);
    assert(this->summary);

    this->w_end_lba = derwin(stdscr, 1, 20, 1, COLS-41);
    assert(this->w_end_lba);

    this->w_cur_lba = derwin(stdscr, 1, 20, 1, COLS-61);
    assert(this->w_cur_lba);

    memset(&this->reports, 1, sizeof(this->reports)); // to fully alloc all pages, unsure if this is necessary, but just int case
    memset(&this->reports, 0, sizeof(this->reports));

    this->reports[0].seqno = 1; // anything but zero

    return this;
}

static void rwtest_render_update_vis(rwtest_render_priv_t *this, blk_report_t *rep);
static void rwtest_render_update_stats(rwtest_render_priv_t *this);

static blk_report_t *blk_rep_get_next_for_write(rwtest_render_priv_t *this) {
    blk_report_t *rep = &this->reports[
        (this->next_report_seqno_write) % (sizeof(this->reports) / sizeof(this->reports[0]))
        ];
    //fprintf(stderr, "giving %p for write\n", rep);
    return rep;
}

static void blk_rep_write_finalize(rwtest_render_priv_t *this, blk_report_t *rep) {
    rep->seqno = this->next_report_seqno_write;
    this->next_report_seqno_write++;
    //fprintf(stderr, "mark %p with seqno %"PRIu64", go to next\n", rep, rep->seqno);
}

static blk_report_t *blk_rep_get_nth_unread(rwtest_render_priv_t *this, int n) {
    blk_report_t *rep = &this->reports[
        (this->next_report_seqno_read + n) % (sizeof(this->reports) / sizeof(this->reports[0]))
        ];
    return rep;
}

static int blk_rep_is_n_avail(rwtest_render_priv_t *this, int n) {
    blk_report_t *nth_ahead = blk_rep_get_nth_unread(this, n);
    //fprintf(stderr, "checking if %p (%dth ahead) finalized as %"PRIu64"\n",
    //        nth_ahead, n, this->next_report_seqno_read + n);
    //fprintf(stderr, "its seqno is %"PRIu64"\n", nth_ahead->seqno);
    if (nth_ahead->seqno == this->next_report_seqno_read + n)
        return 1;
    else
        return 0;
}

static blk_report_t *blk_rep_read(rwtest_render_priv_t *this) {
    if ( ! blk_rep_is_n_avail(this, 1))
        return NULL;
    blk_report_t *rep = blk_rep_get_nth_unread(this, 1);
    this->next_report_seqno_read++;
    return rep;
}

static void *rwtest_render_thread_proc(void *arg) {
    rwtest_render_priv_t *this = arg;
    // TODO block signals in this thread
    while (1) {
        unsigned int reports_on_this_lap = 0;
        blk_report_t *cur_rep;

        if (!this->order_hangup) {
            if ( ! blk_rep_is_n_avail(this, REPORTS_BURST)) {
                //fprintf(stderr, "REPORT_BURST pkts not avail for read yet\n");
                usleep(10*1000);
                continue; // no REPORTS_BURST pkts queued yet
            }
        } else {
            if ( ! blk_rep_is_n_avail(this, 1))
                break;
        }

        while ((reports_on_this_lap++ < REPORTS_BURST) &&
                (cur_rep = blk_rep_read(this))
                ) {
            // if processed REPORTS_BURST blocks, redraw view
            // free processed reports and go next lap
            rwtest_render_update_vis(this, cur_rep);
        }
        rwtest_render_update_stats(this);

        wnoutrefresh(this->vis);
        doupdate();
        sched_yield();
    }
    return NULL;
}

static void rwtest_render_update_vis(rwtest_render_priv_t *this, blk_report_t *rep) {
    if (rep->access_errno)
    {
        print_vis(this->vis, error_vis);
        this->access_time_stats_accum[6]++;
    }
    else
    {
        print_vis(this->vis, choose_vis(rep->access_time));
        unsigned int i;
        for (i = 0; i < 5; i++)
            if (rep->access_time < bs_vis[i].access_time) {
                this->access_time_stats_accum[i]++;
                break;
            }
        if (i == 5)
            this->access_time_stats_accum[5]++; // of exceed
    }
    wnoutrefresh(this->vis);
}

static void rwtest_render_update_stats(rwtest_render_priv_t *this) {
    werase(this->access_time_stats);
    unsigned int i;
    for (i = 0; i < 7; i++)
        wprintw(this->access_time_stats, "%d\n", this->access_time_stats_accum[i]);
    wnoutrefresh(this->access_time_stats);

    werase(this->avg_speed);
    wprintw(this->avg_speed, "SPEED %7"PRIu64" kb/s", this->avg_processing_speed / 1024);
    wnoutrefresh(this->avg_speed);

    unsigned int minute, second;
    second = this->eta_time % 60;
    minute = this->eta_time / 60;
    werase(this->eta);
    wprintw(this->eta, "ETA %11u:%02u", minute, second);
    wnoutrefresh(this->eta);

    werase(this->w_cur_lba);
    char comma_lba_buf[30], *comma_lba_p;
    comma_lba_p = commaprint(this->cur_lba, comma_lba_buf, sizeof(comma_lba_buf));
    wprintw(this->w_cur_lba, "LBA: %14s", comma_lba_p);
    wnoutrefresh(this->w_cur_lba);
}

static int rwtest_render_thread_start(rwtest_render_priv_t *this) {
    return pthread_create(&this->render_thread, NULL, rwtest_render_thread_proc, this);
}

static int rwtest_render_thread_join(rwtest_render_priv_t *this) {
    this->order_hangup = 1;
    return pthread_join(this->render_thread, NULL);
}

void rwtest_render_priv_destroy(rwtest_render_priv_t *this) {
    delwin(this->legend);
    delwin(this->access_time_stats);
    delwin(this->vis);
    delwin(this->avg_speed);
    delwin(this->eta);
    delwin(this->summary);
    delwin(this->w_end_lba);
    delwin(this->w_cur_lba);
    free(this);
    clear_body();
}

static int render_test_read(DC_Dev *dev) {
    int r;
    int interrupted;
    rwtest_render_priv_t *windows = rwtest_render_priv_prepare();
    char comma_lba_buf[30], *comma_lba_p;
    comma_lba_p = commaprint(dev->capacity / 512, comma_lba_buf, sizeof(comma_lba_buf));
    wprintw(windows->w_end_lba, "/ %s", comma_lba_p);
    wnoutrefresh(windows->w_end_lba);
    wprintw(windows->summary,
            "Read test of drive\n"
            "%s (%s)\n"
            "Ctrl+C to abort\n",
            dev->dev_path, dev->model_str);
    wrefresh(windows->summary);
    r = rwtest_render_thread_start(windows);
    if (r)
        return r; // FIXME leak
    r = action_find_start_perform_until_interrupt(dev, "readtest", readtest_cb, (void*)windows, &interrupted);
    if (r)
        return r;
    rwtest_render_thread_join(windows);
    if (interrupted)
        wprintw(windows->summary, "Aborted.\n");
    else
        wprintw(windows->summary, "Completed.\n");
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
    char comma_lba_buf[30], *comma_lba_p;
    comma_lba_p = commaprint(dev->capacity / 512, comma_lba_buf, sizeof(comma_lba_buf));
    wprintw(windows->w_end_lba, "/ %s", comma_lba_p);
    wnoutrefresh(windows->w_end_lba);
    wprintw(windows->summary,
            "Write test of drive\n"
            "%s (%s)\n"
            "Ctrl+C to abort\n",
            dev->dev_path, dev->model_str);
    wrefresh(windows->summary);
    r = rwtest_render_thread_start(windows);
    if (r)
        return r; // FIXME leak
    r = action_find_start_perform_until_interrupt(dev, "zerofill", readtest_cb, (void*)windows, &interrupted);
    if (r)
        return r;
    rwtest_render_thread_join(windows);
    if (interrupted)
        wprintw(windows->summary, "Aborted.\n");
    else
        wprintw(windows->summary, "Completed.\n");
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

    uint64_t bytes_processed = ctx->performs_executed * ctx->blk_size;
    priv->cur_lba = bytes_processed / 512;

    if (ctx->performs_executed == 1) {
        r = clock_gettime(DC_BEST_CLOCK, &priv->start_time);
        assert(!r);
    } else {
        if ((ctx->performs_executed % 1000) == 0) {
            struct timespec now;
            r = clock_gettime(DC_BEST_CLOCK, &now);
            assert(!r);
            uint64_t time_elapsed = now.tv_sec - priv->start_time.tv_sec;
            if (time_elapsed > 0) {
                priv->avg_processing_speed = bytes_processed / time_elapsed; // Byte/s
                // capacity / speed = total_time
                // total_time = elapsed + eta
                // eta = total_time - elapsed
                // eta = capacity / speed  -  elapsed
                priv->eta_time = ctx->dev->capacity / priv->avg_processing_speed - time_elapsed;

            }
        }
    }

    // enqueue block report
    blk_report_t *rep = blk_rep_get_next_for_write(priv);
    assert(rep);
    rep->access_errno = ctx->report.blk_access_errno;
    rep->access_time = ctx->report.blk_access_time;
    blk_rep_write_finalize(priv, rep);
    //fprintf(stderr, "finalized %"PRIu64"\n", priv->next_report_seqno_write-1);

    if ((ctx->performs_executed % REPORTS_BURST) == 0)
        sched_yield();

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
        dialog_msgbox("Error", "Action init fail. Please check privileges, possibly switch to root.", 0, 0, 1);
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

static char *commaprint(uint64_t n, char *retbuf, size_t bufsize) {
    static int comma = ',';
    char *p = &retbuf[bufsize-1];
    int i = 0;

    *p = '\0';
    do {
        if(i%3 == 0 && i != 0)
            *--p = comma;
        *--p = '0' + n % 10;
        n /= 10;
        i++;
    } while(n != 0);

    return p;
}

