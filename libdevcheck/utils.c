#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "utils.h"
#include "log.h"

char *cmd_output(char *command_line) {
    int r;
    char *avoid_stderr;
    r = asprintf(&avoid_stderr, "%s 2>/dev/null", command_line);
    assert(r != -1);
    FILE *pipe;
    pipe = popen(avoid_stderr, "r");
    if (!pipe) {
        dc_log(DC_LOG_FATAL, "Failed to exec '%s'\n", command_line);
        return NULL;
    }

    char *all = NULL;
    char *new_all;
    int all_len = 0;
    char buf[1024];
    while (!feof(pipe)) {
        r = fread(buf, 1, sizeof(buf), pipe);
        // append to buffer
        if (r == 0)
            continue;

        new_all = realloc(all, all_len + r);
        if (!new_all)
            goto fail_buf_form;
        all = new_all;

        memcpy(all + all_len, buf, r);
        all_len += r;
    }

    if (all_len == 0) {
        free(all);
        return NULL;
    }

    // terminate with zero byte
    new_all = realloc(all, all_len + 1);
    if (!new_all)
        goto fail_buf_form;
    all = new_all;
    all[all_len] = '\0';

    return all;

fail_buf_form:
    free(all);
    return NULL;
}

void dc_raise_thread_prio(void) {
    int r;
    struct sched_param sched_param;
    sched_param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    r = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sched_param);
    if (r) {
        dc_log(DC_LOG_ERROR, "pthread_setschedparam fail, ret %d\n", r);
    }
}
