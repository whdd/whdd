#define _GNU_SOURCE
#include <stdio.h>
#include <wchar.h>
#include <curses.h>
#include <dialog.h>
#include <assert.h>

#include "render.h"
#include "utils.h"
#include "ncurses_convenience.h"
#include "procedure.h"
#include "vis.h"

typedef struct blk_report {
    uint64_t seqno;
    DC_BlockReport report;
} blk_report_t;

typedef struct {
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
    uint64_t error_stats_accum[7]; // 0th is unused, the rest are as in DC_BlockStatus enum
    uint64_t bytes_processed;
    uint64_t avg_processing_speed;
    uint64_t eta_time; // estimated time
    uint64_t cur_lba;

    pthread_t render_thread;
    int order_hangup; // if interrupted or completed, render remainings and end render thread

    // lockless ringbuffer
    blk_report_t reports[100*1000];
    uint64_t next_report_seqno_write;
    uint64_t next_report_seqno_read;
} SlidingWindow;



static void *render_thread_proc(void *arg);
static void blk_rep_write_finalize(SlidingWindow *priv, blk_report_t *rep);
static blk_report_t *blk_rep_get_next_for_write(SlidingWindow *priv);
static void render_update_vis(SlidingWindow *priv, blk_report_t *rep);
static void render_update_stats(SlidingWindow *priv);

static blk_report_t *blk_rep_get_next_for_write(SlidingWindow *priv) {
    blk_report_t *rep = &priv->reports[
        (priv->next_report_seqno_write) % (sizeof(priv->reports) / sizeof(priv->reports[0]))
        ];
    //fprintf(stderr, "giving %p for write\n", rep);
    return rep;
}

static void blk_rep_write_finalize(SlidingWindow *priv, blk_report_t *rep) {
    rep->seqno = priv->next_report_seqno_write;
    priv->next_report_seqno_write++;
    //fprintf(stderr, "mark %p with seqno %"PRIu64", go to next\n", rep, rep->seqno);
}

static blk_report_t *blk_rep_get_unread(SlidingWindow *priv) {
    blk_report_t *rep = &priv->reports[
        priv->next_report_seqno_read % (sizeof(priv->reports) / sizeof(priv->reports[0]))
        ];
    return rep;
}

static blk_report_t *blk_rep_read(SlidingWindow *priv) {
    blk_report_t *rep = blk_rep_get_unread(priv);
    priv->next_report_seqno_read++;
    return rep;
}

static int get_queue_length(SlidingWindow *priv) {
    return priv->next_report_seqno_write - priv->next_report_seqno_read;
}

static void render_queued(SlidingWindow *priv) {
    int queue_length = get_queue_length(priv);
    while (queue_length) {
        blk_report_t *cur_rep = blk_rep_read(priv);
        render_update_vis(priv, cur_rep);
        queue_length--;
    }
    render_update_stats(priv);
    wnoutrefresh(priv->vis);
    doupdate();
}

static void *render_thread_proc(void *arg) {
    SlidingWindow *priv = arg;
    // TODO block signals in priv thread
    while (!priv->order_hangup) {
        render_queued(priv);
        usleep(40000);  // 25 Hz should be nice
    }
    render_queued(priv);
    return NULL;
}

static void render_update_vis(SlidingWindow *priv, blk_report_t *rep) {
    if (rep->report.blk_status)
    {
        print_vis(priv->vis, error_vis[rep->report.blk_status]);
        priv->error_stats_accum[rep->report.blk_status]++;
    }
    else
    {
        print_vis(priv->vis, choose_vis(rep->report.blk_access_time));
        unsigned int i;
        for (i = 0; i < 5; i++)
            if (rep->report.blk_access_time < bs_vis[i].access_time) {
                priv->access_time_stats_accum[i]++;
                break;
            }
        if (i == 5)
            priv->access_time_stats_accum[5]++; // of exceed
    }
    wnoutrefresh(priv->vis);
}

static void render_update_stats(SlidingWindow *priv) {
    werase(priv->access_time_stats);
    unsigned int i;
    for (i = 0; i < 6; i++)
        wprintw(priv->access_time_stats, "%d\n", priv->access_time_stats_accum[i]);
    for (i = 1; i < 7; i++)
        wprintw(priv->access_time_stats, "%d\n", priv->error_stats_accum[i]);
    wnoutrefresh(priv->access_time_stats);

    if (priv->avg_processing_speed != 0) {
        werase(priv->avg_speed);
        wprintw(priv->avg_speed, "SPEED %7"PRIu64" kb/s", priv->avg_processing_speed / 1024);
        wnoutrefresh(priv->avg_speed);
    }

    if (priv->eta_time != 0) {
        unsigned int minute, second;
        second = priv->eta_time % 60;
        minute = priv->eta_time / 60;
        werase(priv->eta);
        wprintw(priv->eta, "ETA %11u:%02u", minute, second);
        wnoutrefresh(priv->eta);
    }

    werase(priv->w_cur_lba);
    char comma_lba_buf[30], *comma_lba_p;
    comma_lba_p = commaprint(priv->cur_lba, comma_lba_buf, sizeof(comma_lba_buf));
    wprintw(priv->w_cur_lba, "LBA: %14s", comma_lba_p);
    wnoutrefresh(priv->w_cur_lba);
}

/*
25x80

                                                             <--LEGEND_WIDTH=20->
+--------------------------------------------------------------------------------+
|                   LBA:       xxx,xxx / xxx,xxx,xxx         ETA           xx:xx |
|xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx SPEED    xxxxx kb/s |
|xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx                     |
|xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx x <3ms     xxxx     |^
|xxxxxxxxxxxxxxxxxxxxxxxxx                                   x <10ms    xxx      ||
|                                                            x <50ms    xx       ||
|                                                            x <150ms   x        ||
|                                                            x <500ms   x        ||
|                                                            x >500ms   x        || LEGEND_HEIGHT=12
|                                                            x ERR      x        ||
|                                                            ? TIME     x        ||
|                                                            x UNC      x        ||
|                                                            S IDNF     x        ||
|                                                            ! ABRT     x        ||
|                                                            A AMNF     x        |v
|                                                                                |
|                                                            Read test /dev/XXX  |
|                                                            Block = 131072 bytes|
|                                                                                |
|                                                            Ctrl+C to abort     |
|                                                                                |
|                                                                                |
|                                                                                |
|                                                                                |
| WHDD rev. X.X-X-gXXXXXXX                                                       |
+--------------------------------------------------------------------------------+
*/
static int Open(DC_RendererCtx *ctx) {
    SlidingWindow *priv = ctx->priv;
    DC_ProcedureCtx *actctx = ctx->procedure_ctx;

    // TODO Raise error message
    if (LINES < 25 || COLS < 80)
        return -1;

#define LBA_WIDTH 20
#define LEGEND_WIDTH 20
#define LEGEND_HEIGHT 12
#define LEGEND_VERT_OFFSET 3 /* ETA & SPEED are above, 1 for spacing */

    priv->w_cur_lba = derwin(stdscr, 1, LBA_WIDTH, 0 /* at the top */, COLS - LEGEND_WIDTH - 1 - (LBA_WIDTH * 2) );
    assert(priv->w_cur_lba);
    wbkgd(priv->w_cur_lba, COLOR_PAIR(MY_COLOR_GRAY));

    priv->w_end_lba = derwin(stdscr, 1, LBA_WIDTH, 0 /* at the top */, COLS - LEGEND_WIDTH - 1 - LBA_WIDTH);
    assert(priv->w_end_lba);
    wbkgd(priv->w_end_lba, COLOR_PAIR(MY_COLOR_GRAY));

    priv->eta = derwin(stdscr, 1, LEGEND_WIDTH, 0 /* at the top */, COLS-LEGEND_WIDTH);
    assert(priv->eta);
    wbkgd(priv->eta, COLOR_PAIR(MY_COLOR_GRAY));

    priv->avg_speed = derwin(stdscr, 1, LEGEND_WIDTH, 1 /* ETA is above */, COLS-LEGEND_WIDTH);
    assert(priv->avg_speed);
    wbkgd(priv->avg_speed, COLOR_PAIR(MY_COLOR_GRAY));

    priv->legend = derwin(stdscr, LEGEND_HEIGHT, LEGEND_WIDTH/2, LEGEND_VERT_OFFSET, COLS-LEGEND_WIDTH);
    assert(priv->legend);
    wbkgd(priv->legend, COLOR_PAIR(MY_COLOR_GRAY));

    priv->access_time_stats = derwin(stdscr, LEGEND_HEIGHT, LEGEND_WIDTH/2, LEGEND_VERT_OFFSET, COLS-LEGEND_WIDTH/2);
    assert(priv->access_time_stats);
    wbkgd(priv->access_time_stats, COLOR_PAIR(MY_COLOR_GRAY));
    show_legend(priv->legend);

#define SUMMARY_VERT_OFFSET ( 1 /* ETA */ + 1 + /* SPEED */ + 1 /* spacing */ + LEGEND_HEIGHT + 1 /* spacing */ )
#define SUMMARY_HEIGHT ( LINES - SUMMARY_VERT_OFFSET - 1 /* don't touch bottom line */ )
    priv->summary = derwin(stdscr, SUMMARY_HEIGHT, LEGEND_WIDTH, SUMMARY_VERT_OFFSET, COLS-LEGEND_WIDTH);
    assert(priv->summary);
    wbkgd(priv->summary, COLOR_PAIR(MY_COLOR_GRAY));

    priv->vis = derwin(stdscr, LINES-2 /* version is below, LBA is above */, COLS-LEGEND_WIDTH-1, 1 /* LBA is above */, 0);
    assert(priv->vis);
    scrollok(priv->vis, TRUE);
    wrefresh(priv->vis);

    priv->reports[0].seqno = 1; // anything but zero

    char comma_lba_buf[30], *comma_lba_p;
    comma_lba_p = commaprint(actctx->dev->capacity / 512, comma_lba_buf, sizeof(comma_lba_buf));
    wprintw(priv->w_end_lba, "/ %s", comma_lba_p);
    wnoutrefresh(priv->w_end_lba);
    wprintw(priv->summary,
            "%s %s\n"
            "Block = %d bytes\n"
            "Ctrl+C to abort\n",
            actctx->procedure->display_name, actctx->dev->dev_path, actctx->blk_size);
    wrefresh(priv->summary);
    int r = pthread_create(&priv->render_thread, NULL, render_thread_proc, priv);
    if (r)
        return r; // FIXME leak
    return 0;
}

static int HandleReport(DC_RendererCtx *ctx) {
    int r;
    SlidingWindow *priv = ctx->priv;
    DC_ProcedureCtx *actctx = ctx->procedure_ctx;

    priv->bytes_processed += actctx->report.sectors_processed * 512;
    priv->cur_lba = actctx->report.lba + actctx->report.sectors_processed;

    if (actctx->progress.num == 1) {  // TODO fix priv hack
        r = clock_gettime(DC_BEST_CLOCK, &priv->start_time);
        assert(!r);
    } else {
        if ((actctx->progress.num % 10) == 0) {
            struct timespec now;
            r = clock_gettime(DC_BEST_CLOCK, &now);
            assert(!r);
            uint64_t time_elapsed_ms = now.tv_sec * 1000 + now.tv_nsec / (1000*1000)
                - priv->start_time.tv_sec * 1000 - priv->start_time.tv_nsec / (1000*1000);
            if (time_elapsed_ms > 0) {
                priv->avg_processing_speed = priv->bytes_processed * 1000 / time_elapsed_ms; // Byte/s
                // capacity / speed = total_time
                // total_time = elapsed + eta
                // eta = total_time - elapsed
                // eta = capacity / speed  -  elapsed
                priv->eta_time = actctx->dev->capacity / priv->avg_processing_speed - time_elapsed_ms / 1000;

            }
        }
    }

    // enqueue block report
    blk_report_t *rep = blk_rep_get_next_for_write(priv);
    assert(rep);
    rep->report = actctx->report;
    blk_rep_write_finalize(priv, rep);
    //fprintf(stderr, "finalized %"PRIu64"\n", priv->next_report_seqno_write-1);
    return 0;
}

static void Close(DC_RendererCtx *ctx) {
    SlidingWindow *priv = ctx->priv;
    DC_ProcedureCtx *actctx = ctx->procedure_ctx;

    priv->order_hangup = 1;
    pthread_join(priv->render_thread, NULL);
    if (actctx->interrupt)
        wprintw(priv->summary, "Aborted.\n");
    else
        wprintw(priv->summary, "Completed.\n");
    wprintw(priv->summary, "Press any key");
    wrefresh(priv->summary);
    beep();
    getch();
    delwin(priv->legend);
    delwin(priv->access_time_stats);
    delwin(priv->vis);
    delwin(priv->avg_speed);
    delwin(priv->eta);
    delwin(priv->summary);
    delwin(priv->w_end_lba);
    delwin(priv->w_cur_lba);
    clear_body();
}

DC_Renderer sliding_window = {
    .name = "sliding_window",
    .open = Open,
    .handle_report = HandleReport,
    .close = Close,
    .priv_data_size = sizeof(SlidingWindow),
};
