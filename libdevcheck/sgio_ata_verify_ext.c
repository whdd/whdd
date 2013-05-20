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
#include "ata.h"
#include "scsi.h"

typedef struct verify_priv {
    int fd;
} VerifyPriv;

#define SECTORS_AT_ONCE 256
#define BLK_SIZE (SECTORS_AT_ONCE * 512) // FIXME hardcode
static int Open(DC_ProcedureCtx *ctx) {
    VerifyPriv *priv = ctx->priv;

    // Setting context
    ctx->blk_size = BLK_SIZE;
    ctx->progress.den = ctx->dev->capacity / ctx->blk_size;
    if (ctx->dev->capacity % ctx->blk_size)
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
    int ioctl_ret;
    int r;

    // Updating context
    ctx->report.blk_status = DC_BlockStatus_eOk;

    // Timing
    r = clock_gettime(DC_BEST_CLOCK, &pre);
    assert(!r);

    // Acting
    AtaCommand ata_command;
    prepare_ata_command(&ata_command, WIN_VERIFY_EXT /* 42h */, ctx->current_lba, SECTORS_AT_ONCE);
    ScsiCommand scsi_command;
    prepare_scsi_command_from_ata(&scsi_command, &ata_command);
    ioctl_ret = ioctl(priv->fd, SG_IO, &scsi_command);

    // Timing
    r = clock_gettime(DC_BEST_CLOCK, &post);
    assert(!r);
    ScsiAtaReturnDescriptor scsi_ata_return;
    fill_scsi_ata_return_descriptor(&scsi_ata_return, &scsi_command);
    int sense_key = get_sense_key_from_sense_buffer(scsi_command.sense_buf);
#if 0
    fprintf(stderr, "scsi status: %hhu, msg status %hhu, host status %hu, driver status %hu, duration %u, auxinfo %u\n",
        scsi_command.io_hdr.status,
        scsi_command.io_hdr.msg_status,
        scsi_command.io_hdr.host_status,
        scsi_command.io_hdr.driver_status,
        scsi_command.io_hdr.duration,
        scsi_command.io_hdr.info);
    fprintf(stderr, "sense buffer, in hex: ");
    int i;
    for (i = 0; i < sizeof(scsi_command.sense_buf); i++)
      fprintf(stderr, "%02hhx", scsi_command.sense_buf[i]);
    fprintf(stderr, "\n");
    fprintf(stderr, "sense key is %d", sense_key);
#endif

    // Error handling
    // Updating context
    if (ioctl_ret) {
        ctx->report.blk_status = DC_BlockStatus_eError;
    } else if (scsi_command.io_hdr.duration >= scsi_command.io_hdr.timeout) {
        ctx->report.blk_status = DC_BlockStatus_eTimeout;
    } else if (scsi_ata_return.status.bits.err) {
        if (scsi_ata_return.error.bits.unc)
            ctx->report.blk_status = DC_BlockStatus_eUnc;
        else if (scsi_ata_return.error.bits.idnf)
            ctx->report.blk_status = DC_BlockStatus_eIdnf;
        else if (scsi_ata_return.error.bits.abrt)
            ctx->report.blk_status = DC_BlockStatus_eAbrt;
        else
            ctx->report.blk_status = DC_BlockStatus_eError;
    } else if (scsi_ata_return.status.bits.df) {
        ctx->report.blk_status = DC_BlockStatus_eError;
    } else if (sense_key) {
        if (sense_key == 0x0b)
            ctx->report.blk_status = DC_BlockStatus_eAbrt;
        else
            ctx->report.blk_status = DC_BlockStatus_eError;
    }

    // Updating context
    ctx->progress.num++;
    ctx->current_lba += SECTORS_AT_ONCE;
    // SG_IO builtin timing figured out to be worse that clock_gettime()
    //ctx->report.blk_access_time = scsi_command.io_hdr.duration * 1000;
    ctx->report.blk_access_time = (post.tv_sec - pre.tv_sec) * 1000000 +
        (post.tv_nsec - pre.tv_nsec) / 1000;

    return ioctl_ret;  // If ioctl() failed, we shouldn't proceed
}

static void Close(DC_ProcedureCtx *ctx) {
    VerifyPriv *priv = ctx->priv;
    close(priv->fd);
}

DC_Procedure sgio_ata_verify_ext = {
    .name = "sgio_ata_verify_ext",
    .long_name = "Test surface for reading, using ATA command VERIFY EXT, via ioctl(SG_IO)",
    .open = Open,
    .perform = Perform,
    .close = Close,
    .priv_data_size = sizeof(VerifyPriv),
};

