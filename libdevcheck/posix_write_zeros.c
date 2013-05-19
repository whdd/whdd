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

struct posix_write_zeros_priv {
    int fd;
    void *buf;
};
typedef struct posix_write_zeros_priv PosixWriteZerosPriv;

#define BLK_SIZE (256 * 512) // FIXME hardcode
static int Open(DC_ProcedureCtx *ctx) {
    int r;
    PosixWriteZerosPriv *priv = ctx->priv;
    ctx->blk_size = BLK_SIZE;
    ctx->blks_total = ctx->dev->capacity / ctx->blk_size;
    if (ctx->dev->capacity % ctx->blk_size)
        ctx->blks_total++;
    ctx->performs_total = ctx->blks_total;

    r = posix_memalign(&priv->buf, sysconf(_SC_PAGESIZE), ctx->blk_size);
    if (r)
        goto fail_buf;
    memset(priv->buf, 0, ctx->blk_size);

    priv->fd = open(ctx->dev->dev_path, O_WRONLY | O_SYNC | O_DIRECT | O_LARGEFILE | O_NOATIME);
    if (priv->fd == -1) {
        dc_log(DC_LOG_FATAL, "open %s fail\n", ctx->dev->dev_path);
        goto fail_open;
    }
    r = ioctl(priv->fd, BLKFLSBUF, NULL);
    if (r == -1)
      dc_log(DC_LOG_WARNING, "Flushing block device buffers failed\n");
    return 0;

fail_open:
    free(priv->buf);
fail_buf:
    return 1;
}

static int Perform(DC_ProcedureCtx *ctx) {
    ssize_t write_ret;
    PosixWriteZerosPriv *priv = ctx->priv;
    write_ret = write(priv->fd, priv->buf, ctx->blk_size);
    ctx->blk_index++;
    if (write_ret != ctx->blk_size) {
        int errno_store;
        errno_store = errno;
        lseek(priv->fd, ctx->blk_size * ctx->blk_index, SEEK_SET);
        errno = errno_store; // dc_procedure_perform() stores errno value to context
    }
    /* trick from hdparm */
    /* access all sectors of buf to ensure the read fully completed */
    unsigned i;
    for (i = 0; i < ctx->blk_size; i += 512)
        ((char*)priv->buf)[i] &= 1;
    return 0;
}

static void Close(DC_ProcedureCtx *ctx) {
    PosixWriteZerosPriv *priv = ctx->priv;
    free(priv->buf);
    close(priv->fd);
}

DC_Procedure posix_write_zeros = {
    .name = "posix_write_zeros",
    .open = Open,
    .perform = Perform,
    .close = Close,
    .priv_data_size = sizeof(PosixWriteZerosPriv),
};

