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
#include <linux/hdreg.h>
#include "procedure.h"

typedef struct verify_priv {
    int fd;
} VerifyPriv;

#define SECTORS_AT_ONCE 256
#define LBA28_MAX_ADDRESS (uint64_t)0x0fffffff
#define LBA28_REACHABLE_CAPACITY ((LBA28_MAX_ADDRESS + 1) * 512)
#define BLK_SIZE (SECTORS_AT_ONCE * 512) // FIXME hardcode
static int Open(DC_ProcedureCtx *ctx) {
    VerifyPriv *priv = ctx->priv;
    uint64_t available_capacity = (ctx->dev->capacity > LBA28_REACHABLE_CAPACITY) ? LBA28_REACHABLE_CAPACITY : ctx->dev->capacity;

    // Setting context
    ctx->blk_size = BLK_SIZE;
    ctx->progress.den = available_capacity / ctx->blk_size;
    if (available_capacity % ctx->blk_size)
        ctx->progress.den++;

    priv->fd = open(ctx->dev->dev_path, O_RDWR);
    if (priv->fd == -1) {
        dc_log(DC_LOG_FATAL, "open %s fail\n", ctx->dev->dev_path);
        return 1;
    }
    return 0;
}

static int Perform(DC_ProcedureCtx *ctx) {
    VerifyPriv *priv = ctx->priv;
    struct timespec pre, post;
    int r;
    int ioctl_ret;

    // Updating context
    ctx->report.blk_status = DC_BlockStatus_eOk;

    // Timing
    r = clock_gettime(DC_BEST_CLOCK, &pre);
    assert(!r);

    // Acting
    uint8_t args[7];
    args[0]  = 0x40;                             // Command
    args[1]  = 0;                                // Features
    args[2]  = SECTORS_AT_ONCE          & 0xff;  // Operation size
    args[3]  = ctx->current_lba         & 0xff;  // LBA (7:0)
    args[4]  = (ctx->current_lba >> 8)  & 0xff;  // LBA (15:8)
    args[5]  = (ctx->current_lba >> 16) & 0xff;  // LBA (23:16)
    args[6]  = (ctx->current_lba >> 24) & 0x0f;  // LBA (27:24)
    args[6] |= 0x40; /* LBA flag */

    ioctl_ret = ioctl(priv->fd, HDIO_DRIVE_TASK, args);
#if 0
    fprintf(stderr, "ioctl on LBA %"PRIu64" returned %d, errno %d\n", ctx->current_lba, ioctl_ret, errno);
#endif

    // Timing
    r = clock_gettime(DC_BEST_CLOCK, &post);
    assert(!r);

    // Error handling
    // TODO Check ioctl output registries
    if (ioctl_ret) {
        // Updating context
        ctx->report.blk_status = DC_BlockStatus_eError;
    }

    // Updating context
    ctx->progress.num++;
    ctx->current_lba += SECTORS_AT_ONCE;
    ctx->report.blk_access_time = (post.tv_sec - pre.tv_sec) * 1000000 +
        (post.tv_nsec - pre.tv_nsec) / 1000;

    return ioctl_ret;  // If ioctl() failed, we shouldn't proceed
}

static void Close(DC_ProcedureCtx *ctx) {
    VerifyPriv *priv = ctx->priv;
    close(priv->fd);
}

DC_Procedure hdio_ata_verify = {
    .name = "hdio_ata_verify",
    .long_name = "Test surface for reading, using ATA command VERIFY, via ioctl(HDIO_DRIVE_TASK). Usefulness is limited to LBA28.",
    .open = Open,
    .perform = Perform,
    .close = Close,
    .priv_data_size = sizeof(VerifyPriv),
};

