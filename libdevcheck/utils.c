#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>

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

char *commaprint(uint64_t n, char *retbuf, size_t bufsize) {
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

int procedure_perform_until_interrupt(DC_ProcedureCtx *actctx,
        ProcedureDetachedLoopCB callback, void *callback_priv) {
    int r;
    siginfo_t siginfo;
    sigset_t set;
    pthread_t tid;

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
