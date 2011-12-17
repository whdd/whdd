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
#include "action.h"

struct read_priv {
    int fd;
    void *buf;
};
typedef struct read_priv ReadPriv;

#define BLK_SIZE (128 * 512) // FIXME hardcode
static int Open(DC_ActionCtx *ctx) {
    int r;
    ReadPriv *priv = ctx->priv;
    ctx->blk_size = BLK_SIZE;
    ctx->blks_total = ctx->dev->capacity / ctx->blk_size;
    ctx->performs_total = ctx->blks_total;

    r = posix_memalign(&priv->buf, sysconf(_SC_PAGESIZE), ctx->blk_size);
    if (r)
        goto fail_buf;

    priv->fd = open(ctx->dev->dev_path, O_RDONLY | O_SYNC | O_DIRECT | O_LARGEFILE | O_NOATIME);
    if (priv->fd == -1) {
        printf("open %s fail\n", ctx->dev->dev_path);
        goto fail_open;
    }

    return 0;

fail_open:
    free(priv->buf);
fail_buf:
    return 1;
}

static int Perform(DC_ActionCtx *ctx) {
    ssize_t read_ret;
    ReadPriv *priv = ctx->priv;
    read_ret = read(priv->fd, priv->buf, ctx->blk_size);
    ctx->blk_index++;
    return 0;
}

static void Close(DC_ActionCtx *ctx) {
    ReadPriv *priv = ctx->priv;
    free(priv->buf);
    close(priv->fd);
}

DC_Action readtest = {
    .name = "readtest",
    .open = Open,
    .perform = Perform,
    .close = Close,
    .priv_data_size = sizeof(ReadPriv),
};

