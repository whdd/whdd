#define _FILE_OFFSET_BITS 64
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

#include "libdevcheck.h"
#include "device.h"
#include "utils.h"

char *dc_dev_smartctl_text(DC_Dev *dev, char *options) {
    int r;
    char *command_line;
    r = asprintf(&command_line, "smartctl %s %s", options, dev->dev_path);
    if (r == -1)
        return NULL;

    char *smartctl_output = cmd_output(command_line);
    free(command_line);

    return smartctl_output;
}

int dc_dev_readtest(DC_Dev *dev, TestCB callback, void *priv) {
    int r;
    char *buf;
    int fd;
    struct timespec pre, post;
    ssize_t read_ret;
    int errno_store;
    DC_TestReport rep;
    memset(&rep, 0, sizeof(rep));
#define BLK_SIZE (128 * 512) // FIXME hardcode
    r = posix_memalign((void **)&buf, sysconf(_SC_PAGESIZE), BLK_SIZE);
    if (r) {
        return 1;
    }

    rep.blk_size = BLK_SIZE;
    rep.blks_total = dev->capacity / rep.blk_size;

    fd = open(dev->dev_path, O_RDONLY | O_SYNC | O_DIRECT | O_LARGEFILE | O_NOATIME);
    if (fd == -1) {
        return 1;
    }

    while (rep.blk_index < rep.blks_total) {
        clock_gettime(CLOCK_MONOTONIC_RAW, &pre);
        read_ret = read(fd, buf, BLK_SIZE);
        errno_store = errno;
        clock_gettime(CLOCK_MONOTONIC_RAW, &post);
        rep.blk_access_time = (post.tv_sec - pre.tv_sec) * 1000000 +
            (post.tv_nsec - pre.tv_nsec) / 1000;
        if (read_ret == -1) {
            rep.blk_access_errno = errno_store;
        } else {
            rep.blk_access_errno = 0;
        }
        callback(dev, priv, &rep);
        rep.blk_index++;
    }
    return 0;
}

