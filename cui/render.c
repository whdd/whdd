#define _GNU_SOURCE
#include <stdio.h>

#include "render.h"
#include "utils.h"
#include "ncurses_convenience.h"
#include "dialog_convenience.h"

render_priv_t *render_ctx_global = NULL;

static render_priv_t *render_priv_prepare(void) {
    render_priv_t *this = calloc(1, sizeof(*this));
    if (!this)
        return NULL;
    this->legend = derwin(stdscr, 11 /* legend win height */, LEGEND_WIDTH/2, 4, COLS-LEGEND_WIDTH); // leave 1st and last lines untouched
    assert(this->legend);
    this->access_time_stats = derwin(stdscr, 11 /* height */, LEGEND_WIDTH/2, 4, COLS-LEGEND_WIDTH/2);
    assert(this->access_time_stats);
    show_legend(this->legend);
    this->vis = derwin(stdscr, LINES-5, COLS-LEGEND_WIDTH-1, 2, 0); // leave 1st and last lines untouched
    assert(this->vis);
    scrollok(this->vis, TRUE);
    wrefresh(this->vis);

    this->avg_speed = derwin(stdscr, 1, LEGEND_WIDTH, 2, COLS-LEGEND_WIDTH);
    assert(this->avg_speed);

    this->eta = derwin(stdscr, 1, LEGEND_WIDTH, 1, COLS-LEGEND_WIDTH);
    assert(this->eta);

    this->summary = derwin(stdscr, 10, LEGEND_WIDTH, 16, COLS-LEGEND_WIDTH);
    assert(this->summary);

    this->w_end_lba = derwin(stdscr, 1, 20, 1, COLS-41);
    assert(this->w_end_lba);

    this->w_cur_lba = derwin(stdscr, 1, 20, 1, COLS-61);
    assert(this->w_cur_lba);

    this->w_log = derwin(stdscr, 2, COLS, LINES-3, 0);
    assert(this->w_log);
    scrollok(this->w_log, TRUE);

    this->reports[0].seqno = 1; // anything but zero

    return this;
}

static void render_update_vis(render_priv_t *this, blk_report_t *rep);
static void render_update_stats(render_priv_t *this);

blk_report_t *blk_rep_get_next_for_write(render_priv_t *this) {
    blk_report_t *rep = &this->reports[
        (this->next_report_seqno_write) % (sizeof(this->reports) / sizeof(this->reports[0]))
        ];
    //fprintf(stderr, "giving %p for write\n", rep);
    return rep;
}

void blk_rep_write_finalize(render_priv_t *this, blk_report_t *rep) {
    rep->seqno = this->next_report_seqno_write;
    this->next_report_seqno_write++;
    //fprintf(stderr, "mark %p with seqno %"PRIu64", go to next\n", rep, rep->seqno);
}

static blk_report_t *blk_rep_get_unread(render_priv_t *this) {
    blk_report_t *rep = &this->reports[
        this->next_report_seqno_read % (sizeof(this->reports) / sizeof(this->reports[0]))
        ];
    return rep;
}

static blk_report_t *blk_rep_read(render_priv_t *this) {
    blk_report_t *rep = blk_rep_get_unread(this);
    this->next_report_seqno_read++;
    return rep;
}

static int get_queue_length(render_priv_t *this) {
    return this->next_report_seqno_write - this->next_report_seqno_read;
}

static void render_queued(render_priv_t *this) {
    int queue_length = get_queue_length(this);
    while (queue_length) {
        blk_report_t *cur_rep = blk_rep_read(this);
        render_update_vis(this, cur_rep);
        queue_length--;
    }
    render_update_stats(this);
    wnoutrefresh(this->vis);
    doupdate();
}

static void *render_thread_proc(void *arg) {
    render_priv_t *this = arg;
    // TODO block signals in this thread
    while (!this->order_hangup) {
        render_queued(this);
        usleep(40000);  // 25 Hz should be nice
    }
    render_queued(this);
    return NULL;
}

static void render_update_vis(render_priv_t *this, blk_report_t *rep) {
    if (rep->blk_status)
    {
        print_vis(this->vis, error_vis[rep->blk_status]);
        this->error_stats_accum[rep->blk_status]++;
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

static void render_update_stats(render_priv_t *this) {
    werase(this->access_time_stats);
    unsigned int i;
    for (i = 0; i < 6; i++)
        wprintw(this->access_time_stats, "%d\n", this->access_time_stats_accum[i]);
    for (i = 1; i < 6; i++)
        wprintw(this->access_time_stats, "%d\n", this->error_stats_accum[i]);
    wnoutrefresh(this->access_time_stats);

    if (this->avg_processing_speed != 0) {
        werase(this->avg_speed);
        wprintw(this->avg_speed, "SPEED %7"PRIu64" kb/s", this->avg_processing_speed / 1024);
        wnoutrefresh(this->avg_speed);
    }

    if (this->eta_time != 0) {
        unsigned int minute, second;
        second = this->eta_time % 60;
        minute = this->eta_time / 60;
        werase(this->eta);
        wprintw(this->eta, "ETA %11u:%02u", minute, second);
        wnoutrefresh(this->eta);
    }

    werase(this->w_cur_lba);
    char comma_lba_buf[30], *comma_lba_p;
    comma_lba_p = commaprint(this->cur_lba, comma_lba_buf, sizeof(comma_lba_buf));
    wprintw(this->w_cur_lba, "LBA: %14s", comma_lba_p);
    wnoutrefresh(this->w_cur_lba);
}

void render_priv_destroy(render_priv_t *this) {
    delwin(this->legend);
    delwin(this->access_time_stats);
    delwin(this->vis);
    delwin(this->avg_speed);
    delwin(this->eta);
    delwin(this->summary);
    delwin(this->w_end_lba);
    delwin(this->w_cur_lba);
    delwin(this->w_log);
    free(this);
    clear_body();
}

static int handle_reports(DC_ProcedureCtx *ctx, void *callback_priv) {
    int r;
    render_priv_t *priv = callback_priv;

    uint64_t bytes_processed = ctx->current_lba * 512;
    if (bytes_processed > ctx->dev->capacity)
        bytes_processed = ctx->dev->capacity;
    priv->cur_lba = ctx->current_lba;

    if (ctx->progress.num == 1) {  // TODO fix this hack
        r = clock_gettime(DC_BEST_CLOCK, &priv->start_time);
        assert(!r);
    } else {
        if ((ctx->progress.num % 1000) == 0) {
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
    rep->blk_status = ctx->report.blk_status;
    rep->access_time = ctx->report.blk_access_time;
    blk_rep_write_finalize(priv, rep);
    //fprintf(stderr, "finalized %"PRIu64"\n", priv->next_report_seqno_write-1);

    return 0;
}

int render_procedure(DC_ProcedureCtx *actctx) {
    int r;
    render_priv_t *windows = render_priv_prepare();
    char comma_lba_buf[30], *comma_lba_p;
    comma_lba_p = commaprint(actctx->dev->capacity / 512, comma_lba_buf, sizeof(comma_lba_buf));
    wprintw(windows->w_end_lba, "/ %s", comma_lba_p);
    wnoutrefresh(windows->w_end_lba);
    wprintw(windows->summary,
            "%s %s bs=%d\n"
            "Ctrl+C to abort\n",
            actctx->procedure->name, actctx->dev->dev_path, actctx->blk_size);
    wrefresh(windows->summary);
    render_ctx_global = windows;
    r = pthread_create(&windows->render_thread, NULL, render_thread_proc, windows);
    if (r)
        return r; // FIXME leak
    r = procedure_perform_until_interrupt(actctx, handle_reports, (void*)windows);
    if (r)
        return r;
    windows->order_hangup = 1;
    pthread_join(windows->render_thread, NULL);
    if (actctx->interrupt)
        wprintw(windows->summary, "Aborted.\n");
    else
        wprintw(windows->summary, "Completed.\n");
    wprintw(windows->summary, "Press any key");
    wrefresh(windows->summary);
    beep();
    getch();
    render_priv_destroy(windows);
    render_ctx_global = NULL;
    return 0;
}
