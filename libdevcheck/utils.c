#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

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

int dc_realtime_scheduling_enable_with_prio(int prio) {
    int r;
    struct sched_param sched_param;
    if (prio)
        sched_param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    else
        sched_param.sched_priority = sched_get_priority_min(SCHED_FIFO);
    r = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sched_param);
    if (r)
        dc_log(DC_LOG_WARNING, "Failed to enable realtime scheduling, pthread_setschedparam errno %d\n", errno);
    return r;
}

char *dc_dev_smartctl_text(char *dev_fs_path, char *options) {
    int r;
    char *command_line;
    r = asprintf(&command_line, "smartctl %s %s", options, dev_fs_path);
    if (r == -1)
        return NULL;

    char *smartctl_output = cmd_output(command_line);
    free(command_line);

    return smartctl_output;
}
