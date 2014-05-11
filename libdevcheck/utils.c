#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "utils.h"
#include "log.h"
#include "scsi.h"

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
        pclose(pipe);
        return NULL;
    }

    // terminate with zero byte
    new_all = realloc(all, all_len + 1);
    if (!new_all)
        goto fail_buf_form;
    all = new_all;
    all[all_len] = '\0';

    pclose(pipe);
    return all;

fail_buf_form:
    free(all);
    pclose(pipe);
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

int dc_dev_get_native_capacity(char *dev_fs_path, uint64_t *capacity) {
    int ret = dc_dev_get_native_max_lba(dev_fs_path, capacity);
    if (!ret)
        *capacity = (*capacity + 1) * 512;
    return ret;
}

int dc_dev_get_native_max_lba(char *dev_fs_path, uint64_t *max_lba) {
    int ioctl_ret;
    int fd = open(dev_fs_path, O_RDWR);
    if (fd == -1)
        return -1;
    AtaCommand ata_command;
    prepare_ata_command(&ata_command, WIN_READ_NATIVE_MAX_EXT /* 27h */, 0, 0);
    ScsiCommand scsi_command;
    prepare_scsi_command_from_ata(&scsi_command, &ata_command);
    ioctl_ret = ioctl(fd, SG_IO, &scsi_command);
    close(fd);
    if (ioctl_ret)
        return -1;
    ScsiAtaReturnDescriptor scsi_ata_ret;
    fill_scsi_ata_return_descriptor(&scsi_ata_ret, &scsi_command);
    *max_lba = scsi_ata_ret.lba;
    return 0;
}

int dc_dev_get_capacity(char *dev_fs_path, uint64_t *capacity) {
    int ret = dc_dev_get_max_lba(dev_fs_path, capacity);
    if (!ret)
        *capacity = (*capacity + 1) * 512;
    return ret;
}

int dc_dev_get_max_lba(char *dev_fs_path, uint64_t *max_lba) {
    int ret;
    uint8_t buf[512];
    ret = dc_dev_ata_identify(dev_fs_path, buf);
    if (ret)
        return ret;
    *max_lba = (uint64_t)buf[200]
        | ((uint64_t)buf[201] << 8)
        | ((uint64_t)buf[202] << 16)
        | ((uint64_t)buf[203] << 24)
        | ((uint64_t)buf[204] << 32)
        | ((uint64_t)buf[205] << 40);
    (*max_lba)--;
    return 0;
}

int dc_dev_set_max_capacity(char *dev_fs_path, uint64_t capacity) {
    return dc_dev_set_max_lba(dev_fs_path, capacity / 512 - 1);
}

int dc_dev_set_max_lba(char *dev_fs_path, uint64_t lba) {
    int ioctl_ret;
    uint64_t old_max_lba;
    dc_dev_get_native_capacity(dev_fs_path, &old_max_lba);  // Have read from hdparm.c that this is required by standard before setting
    int fd = open(dev_fs_path, O_RDWR);
    if (fd == -1)
        return -1;
    AtaCommand ata_command;
    prepare_ata_command(&ata_command, WIN_SET_MAX_EXT /* 37h */, lba, 1 /* value volatile bit */);
    ScsiCommand scsi_command;
    prepare_scsi_command_from_ata(&scsi_command, &ata_command);
    ioctl_ret = ioctl(fd, SG_IO, &scsi_command);
    close(fd);
    if (ioctl_ret)
        return ioctl_ret;

    // Parse response
    ScsiAtaReturnDescriptor scsi_ata_ret;
    fill_scsi_ata_return_descriptor(&scsi_ata_ret, &scsi_command);
    int sense_key = get_sense_key_from_sense_buffer(scsi_command.sense_buf);
    if (scsi_ata_ret.status & STATUS_BIT_ERR || sense_key > 0x01)
        return -1;
    return 0;
}

int dc_dev_ata_capable(char *dev_fs_path) {
    uint64_t dummy;
    return !dc_dev_get_max_lba(dev_fs_path, &dummy);
}

int dc_dev_ata_identify(char *dev_fs_path, uint8_t identify[512]) {
    int ioctl_ret;
    int fd = open(dev_fs_path, O_RDWR);
    if (fd == -1)
        return -1;
    AtaCommand ata_command;
    ScsiCommand scsi_command;
    uint8_t buf[512];
    memset(&ata_command, 0, sizeof(ata_command));
    memset(&scsi_command, 0, sizeof(scsi_command));
    prepare_ata_command(&ata_command, WIN_IDENTIFY /* ECh */, 0, 0);
    prepare_scsi_command_from_ata(&scsi_command, &ata_command);
    scsi_command.io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    scsi_command.io_hdr.dxferp = identify;
    scsi_command.io_hdr.dxfer_len = sizeof(buf);
    scsi_command.scsi_cmd[1] = (4 << 1) + 1;  // PIO_IN protocol + EXTEND bit
    scsi_command.scsi_cmd[2] = 0x2e;  // CK_COND=1 T_DIR=1 BYTE_BLOCK=1 T_LENGTH=10b
#if 0
    int i;
    for (i = 0; i < sizeof(scsi_command.scsi_cmd); i++)
        fprintf(stderr, "%02hhx", scsi_command.scsi_cmd[i]);
    fprintf(stderr, "\n");
#endif
    ioctl_ret = ioctl(fd, SG_IO, &scsi_command);
    close(fd);
    if (ioctl_ret)
        return -1;

    // Parse response
    ScsiAtaReturnDescriptor scsi_ata_ret;
    fill_scsi_ata_return_descriptor(&scsi_ata_ret, &scsi_command);
    int sense_key = get_sense_key_from_sense_buffer(scsi_command.sense_buf);
    if (scsi_ata_ret.status & STATUS_BIT_ERR || sense_key > 0x01)
        return -1;
    return 0;
}

void dc_ata_ascii_to_c_string(uint8_t *ata_ascii_string, unsigned int ata_length_in_words, char *dst) {
    uint16_t *p = (uint16_t*)ata_ascii_string;
    int length = ata_length_in_words;
    uint8_t ii;
    char cl;

    /* find first non-space & print it */
    for (ii = 0; ii< length; ii++) {
        if(((char) 0x00ff&((*p)>>8)) != ' ') break;
        if((cl = (char) 0x00ff&(*p)) != ' ') {
            if(cl != '\0') *dst++ = cl;
            p++; ii++;
            break;
        }
        p++;
    }
    /* print the rest */
    for (; ii < length; ii++) {
        uint8_t c;
        /* some older devices have NULLs */
        c = (*p) >> 8;
        if (c) *dst++ = c;
        c = (*p);
        if (c) *dst++ = c;
        p++;
    }
    *dst = '\0';

    // Erase trailing spaces
    while (1) {
        dst--;
        if (*dst != ' ')
            break;
        *dst = '\0';
    }
}
