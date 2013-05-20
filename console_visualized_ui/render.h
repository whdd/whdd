#ifndef RENDER_H
#define RENDER_H

#include <wchar.h>
#include <curses.h>
#include <dialog.h>
#include <assert.h>

#include "procedure.h"
#include "vis.h"

typedef struct blk_report {
    uint64_t seqno;
    DC_BlockStatus blk_status;
    unsigned int access_time;
} blk_report_t;

typedef struct render_priv {
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
    WINDOW *w_log;

    struct timespec start_time;
    uint64_t access_time_stats_accum[6];
    uint64_t error_stats_accum[6]; // 0th is unused, the rest are as in DC_BlockStatus enum
    uint64_t avg_processing_speed;
    uint64_t eta_time; // estimated time
    uint64_t cur_lba;

    pthread_t render_thread;
    int order_hangup; // if interrupted or completed, render remainings and end render thread

    // lockless ringbuffer
    blk_report_t reports[100*1000];
    uint64_t next_report_seqno_write;
    uint64_t next_report_seqno_read;
} render_priv_t;

extern render_priv_t *render_ctx_global;

void blk_rep_write_finalize(render_priv_t *this, blk_report_t *rep);
blk_report_t *blk_rep_get_next_for_write(render_priv_t *this);
int render_procedure(DC_ProcedureCtx *actctx);

#endif // RENDER_H
