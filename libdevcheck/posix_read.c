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
#include "action.h"

struct read_priv {
    int fd;
    void *buf;
    int old_readahead;
};
typedef struct read_priv PosixReadPriv;

#define BLK_SIZE (128 * 512) // FIXME hardcode
static int Open(DC_ActionCtx *ctx) {
    int r;
    PosixReadPriv *priv = ctx->priv;
    ctx->blk_size = BLK_SIZE;
    ctx->blks_total = ctx->dev->capacity / ctx->blk_size;
    if (ctx->dev->capacity % ctx->blk_size)
        ctx->blks_total++;
    ctx->performs_total = ctx->blks_total;

    r = posix_memalign(&priv->buf, sysconf(_SC_PAGESIZE), ctx->blk_size);
    if (r)
        goto fail_buf;

    priv->fd = open(ctx->dev->dev_path, O_RDONLY | O_SYNC | O_DIRECT | O_LARGEFILE | O_NOATIME);
    if (priv->fd == -1) {
        dc_log(DC_LOG_FATAL, "open %s fail\n", ctx->dev->dev_path);
        goto fail_open;
    }
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

static int Perform(DC_ActionCtx *ctx) {
    ssize_t read_ret;
    PosixReadPriv *priv = ctx->priv;
    read_ret = read(priv->fd, priv->buf, ctx->blk_size);
    ctx->blk_index++;
    if (read_ret != ctx->blk_size) {
        int errno_store;
        /* Set read position appropriately for the case it somehow reads non-full block
         *
         * From "man 2 read":
         * On error, -1 is returned, and errno is set appropriately. In this case it is
         * left unspecified whether the file position (if any) changes.
         *
         * So we set read position as appropriate
         */
        errno_store = errno;
        lseek(priv->fd, ctx->blk_size * ctx->blk_index, SEEK_SET);
        errno = errno_store; // dc_action_perform() stores errno value to context
    }
    /* trick from hdparm */
    /* access all sectors of buf to ensure the read fully completed */
    unsigned i;
    for (i = 0; i < ctx->blk_size; i += 512)
        ((char*)priv->buf)[i] &= 1;
    return 0;
}

static void Close(DC_ActionCtx *ctx) {
    PosixReadPriv *priv = ctx->priv;
    int r = ioctl(priv->fd, BLKRASET, priv->old_readahead);
    if (r == -1)
      dc_log(DC_LOG_WARNING, "Restoring block device readahead setting failed\n");
    free(priv->buf);
    close(priv->fd);
}

DC_Action posix_read = {
    .name = "posix_read",
    .open = Open,
    .perform = Perform,
    .close = Close,
    .priv_data_size = sizeof(PosixReadPriv),
};

