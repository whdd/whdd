#define _FILE_OFFSET_BITS 64
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include "procedure.h"

struct read_priv {
    int64_t start_lba;
    int64_t end_lba;
    int64_t lba_to_process;
    int fd;
    void *buf;
    int old_readahead;
    uint64_t blk_index;
};
typedef struct read_priv PosixReadPriv;

#define SECTORS_AT_ONCE 256
#define BLK_SIZE (SECTORS_AT_ONCE * 512) // FIXME hardcode
static int Open(DC_ProcedureCtx *ctx) {
    int r;
    PosixReadPriv *priv = ctx->priv;

    // Setting context
    ctx->blk_size = BLK_SIZE;
    ctx->current_lba = priv->start_lba;
    priv->end_lba = ctx->dev->capacity / 512;
    priv->lba_to_process = priv->end_lba - priv->start_lba;
    ctx->progress.den = priv->lba_to_process / SECTORS_AT_ONCE;
    if (priv->lba_to_process % SECTORS_AT_ONCE)
        ctx->progress.den++;

    r = posix_memalign(&priv->buf, sysconf(_SC_PAGESIZE), ctx->blk_size);
    if (r)
        goto fail_buf;

    priv->fd = open(ctx->dev->dev_path, O_RDONLY | O_SYNC | O_DIRECT | O_LARGEFILE | O_NOATIME);
    if (priv->fd == -1) {
        dc_log(DC_LOG_FATAL, "open %s fail\n", ctx->dev->dev_path);
        goto fail_open;
    }
    lseek(priv->fd, 512 * priv->start_lba, SEEK_SET);
    r = ioctl(priv->fd, BLKFLSBUF, NULL);
    if (r == -1)
      dc_log(DC_LOG_WARNING, "Flushing block device buffers failed\n");
    r = ioctl(priv->fd, BLKRAGET, &priv->old_readahead);
    if (r == -1)
      dc_log(DC_LOG_WARNING, "Getting block device readahead setting failed\n");
    r = ioctl(priv->fd, BLKRASET, 0);
    if (r == -1)
      dc_log(DC_LOG_WARNING, "Disabling block device readahead setting failed\n");

    return 0;

fail_open:
    free(priv->buf);
fail_buf:
    return 1;
}

static int Perform(DC_ProcedureCtx *ctx) {
    ssize_t read_ret;
    PosixReadPriv *priv = ctx->priv;
    struct timespec pre, post;
    size_t sectors_to_read = (priv->lba_to_process < SECTORS_AT_ONCE) ? priv->lba_to_process : SECTORS_AT_ONCE;
    int r;

    // Updating context
    ctx->report.blk_status = DC_BlockStatus_eOk;
    priv->blk_index++;

    // Timing
    r = clock_gettime(DC_BEST_CLOCK, &pre);
    assert(!r);

    // Acting
    read_ret = read(priv->fd, priv->buf, sectors_to_read * 512);

    // Error handling
    if (read_ret != sectors_to_read * 512) {
        // Position of fd is undefined. Set fd position to read next block
        lseek(priv->fd, 512 * priv->start_lba + ctx->blk_size * priv->blk_index, SEEK_SET);

        // Updating context
        ctx->report.blk_status = DC_BlockStatus_eError;
    }

    // Timing
    r = clock_gettime(DC_BEST_CLOCK, &post);
    assert(!r);

    // Updating context
    ctx->progress.num++;
    priv->lba_to_process -= sectors_to_read;
    ctx->current_lba += sectors_to_read;
    ctx->report.blk_access_time = (post.tv_sec - pre.tv_sec) * 1000000 +
        (post.tv_nsec - pre.tv_nsec) / 1000;

    return 0;
}

static void Close(DC_ProcedureCtx *ctx) {
    PosixReadPriv *priv = ctx->priv;
    int r = ioctl(priv->fd, BLKRASET, priv->old_readahead);
    if (r == -1)
      dc_log(DC_LOG_WARNING, "Restoring block device readahead setting failed\n");
    free(priv->buf);
    close(priv->fd);
}

static DC_ProcedureOption options[] = {
    { "start_lba", "set LBA address to begin from", offsetof(PosixReadPriv, start_lba), DC_ProcedureOptionType_eInt64, { .i64 = 0 } },
    { NULL }
};

DC_Procedure posix_read = {
    .name = "posix_read",
    .long_name = "Read from device, using POSIX read() call, in direct mode",
    .open = Open,
    .perform = Perform,
    .close = Close,
    .priv_data_size = sizeof(PosixReadPriv),
    .options = options,
};

