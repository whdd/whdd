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

enum Api {
    Api_eAta,
    Api_ePosix,
};

struct copy_priv {
    const char *api_str;
    const char *dst_file;
    enum Api api;
    int64_t start_lba;
    int64_t end_lba;
    int64_t lba_to_process;
    int src_fd;
    int dst_fd;
    void *buf;
    AtaCommand ata_command;
    ScsiCommand scsi_command;
    int old_readahead;
    uint64_t blk_index;
};
typedef struct copy_priv CopyPriv;

#define SECTORS_AT_ONCE 256
#define BLK_SIZE (SECTORS_AT_ONCE * 512) // FIXME hardcode

static int SuggestDefaultValue(DC_Dev *dev, DC_OptionSetting *setting) {
    if (!strcmp(setting->name, "api")) {
        setting->value = strdup("ata");
        // TODO Really check if ATA should work for dev in realtime
    } else if (!strcmp(setting->name, "dst_file")) {
        setting->value = strdup("/dev/null");
    } else {
        return 1;
    }
    return 0;
}

static int Open(DC_ProcedureCtx *ctx) {
    int r;
    CopyPriv *priv = ctx->priv;

    // Setting context
    if (!strcmp(priv->api_str, "ata"))
        priv->api = Api_eAta;
    else if (!strcmp(priv->api_str, "posix"))
        priv->api = Api_ePosix;
    else
        return 1;
    ctx->blk_size = BLK_SIZE;
    priv->end_lba = ctx->dev->capacity / 512;
    priv->lba_to_process = priv->end_lba - priv->start_lba;
    ctx->progress.den = priv->lba_to_process / SECTORS_AT_ONCE;
    if (priv->lba_to_process % SECTORS_AT_ONCE)
        ctx->progress.den++;

    r = posix_memalign(&priv->buf, sysconf(_SC_PAGESIZE), ctx->blk_size);
    if (r)
        goto fail_buf;

    int open_flags = priv->api == Api_eAta ? O_RDWR : O_RDONLY | O_DIRECT | O_LARGEFILE | O_NOATIME;
    priv->src_fd = open(ctx->dev->dev_path, open_flags);
    if (priv->src_fd == -1) {
        dc_log(DC_LOG_FATAL, "open %s fail\n", ctx->dev->dev_path);
        goto fail_open;
    }
    lseek(priv->src_fd, 512 * priv->start_lba, SEEK_SET);
    r = ioctl(priv->src_fd, BLKFLSBUF, NULL);
    if (r == -1)
      dc_log(DC_LOG_WARNING, "Flushing block device buffers failed\n");
    r = ioctl(priv->src_fd, BLKRAGET, &priv->old_readahead);
    if (r == -1)
      dc_log(DC_LOG_WARNING, "Getting block device readahead setting failed\n");
    r = ioctl(priv->src_fd, BLKRASET, 0);
    if (r == -1)
      dc_log(DC_LOG_WARNING, "Disabling block device readahead setting failed\n");

    // We use no O_DIRECT to allow output to generic file etc.
    priv->dst_fd = open(priv->dst_file, O_WRONLY | O_LARGEFILE | O_NOATIME | O_CREAT | O_TRUNC);
    if (priv->dst_fd == -1) {
        assert(0);
        dc_log(DC_LOG_FATAL, "open %s fail\n", priv->dst_file);
        goto fail_dst_open;
    }

    return 0;
fail_dst_open:
    close(priv->src_fd);
    r = ioctl(priv->src_fd, BLKRASET, priv->old_readahead);
    if (r == -1)
      dc_log(DC_LOG_WARNING, "Restoring block device readahead setting failed\n");
fail_open:
    free(priv->buf);
fail_buf:
    return 1;
}

static int Perform(DC_ProcedureCtx *ctx) {
    ssize_t read_ret;
    int ioctl_ret;
    int ret = 0;
    CopyPriv *priv = ctx->priv;
    struct timespec pre, post;
    size_t sectors_to_read = (priv->lba_to_process < SECTORS_AT_ONCE) ? priv->lba_to_process : SECTORS_AT_ONCE;
    int r;
    int error_flag = 0;

    // Updating context
    ctx->report.lba = priv->start_lba + SECTORS_AT_ONCE * priv->blk_index;
    ctx->report.blk_status = DC_BlockStatus_eOk;
    priv->blk_index++;

    // Preparing to act
    if (priv->api == Api_eAta) {
        memset(&priv->ata_command, 0, sizeof(priv->ata_command));
        memset(&priv->scsi_command, 0, sizeof(priv->scsi_command));
        prepare_ata_command(&priv->ata_command, /* WIN_READ_DMA_EXT */ 0x25, ctx->report.lba, sectors_to_read);
        prepare_scsi_command_from_ata(&priv->scsi_command, &priv->ata_command);
        priv->scsi_command.io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
        priv->scsi_command.io_hdr.dxferp = priv->buf;
        priv->scsi_command.io_hdr.dxfer_len = ctx->blk_size;
        priv->scsi_command.scsi_cmd[1] = (6 << 1) + 1;  // DMA protocol + EXTEND bit
#if 0
        int i;
        for (i = 0; i < sizeof(priv->scsi_command.scsi_cmd); i++)
          fprintf(stderr, "%02hhx", priv->scsi_command.scsi_cmd[i]);
        fprintf(stderr, "\n");
#endif
    }

    // Timing
    r = clock_gettime(DC_BEST_CLOCK, &pre);
    assert(!r);

    // Acting
    if (priv->api == Api_eAta)
        ioctl_ret = ioctl(priv->src_fd, SG_IO, &priv->scsi_command);
    else
        read_ret = read(priv->src_fd, priv->buf, sectors_to_read * 512);

    // Timing
    r = clock_gettime(DC_BEST_CLOCK, &post);
    assert(!r);

    // Error handling
    if (priv->api == Api_eAta) {
        // Updating context
        if (ioctl_ret) {
            ctx->report.blk_status = DC_BlockStatus_eError;
            ret = 1;
        }
        ctx->report.blk_status = scsi_ata_check_return_status(&priv->scsi_command);
    } else {
        if (read_ret != sectors_to_read * 512) {
            error_flag = 1;
            // Position of fd is undefined. Set fds position to next block
            lseek(priv->src_fd, 512 * priv->start_lba + ctx->blk_size * priv->blk_index, SEEK_SET);
            lseek(priv->dst_fd, 512 * priv->start_lba + ctx->blk_size * priv->blk_index, SEEK_SET);

            // Updating context
            ctx->report.blk_status = DC_BlockStatus_eError;
        }
    }

    // Acting: writing; not timed
    if (!error_flag) {
        int write_ret = write(priv->dst_fd, priv->buf, sectors_to_read * 512);

        // Error handling
        if (write_ret != sectors_to_read * 512) {
            lseek(priv->dst_fd, 512 * priv->start_lba + ctx->blk_size * priv->blk_index, SEEK_SET);
            // Updating context
            ctx->report.blk_status = DC_BlockStatus_eError;
            // TODO Transmit to user info that _write phase_ has failed
        }
    }

    // Updating context
    ctx->progress.num++;
    priv->lba_to_process -= sectors_to_read;
    ctx->report.blk_access_time = (post.tv_sec - pre.tv_sec) * 1000000 +
        (post.tv_nsec - pre.tv_nsec) / 1000;

    return ret;
}

static void Close(DC_ProcedureCtx *ctx) {
    CopyPriv *priv = ctx->priv;
    int r = ioctl(priv->src_fd, BLKRASET, priv->old_readahead);
    if (r == -1)
      dc_log(DC_LOG_WARNING, "Restoring block device readahead setting failed\n");
    free(priv->buf);
    close(priv->src_fd);
    close(priv->dst_fd);
}

static DC_ProcedureOption options[] = {
    { "api", "select operation API: \"posix\" for POSIX read(), \"ata\" for ATA \"READ DMA EXT\" command", offsetof(CopyPriv, api_str), DC_ProcedureOptionType_eString },
    { "dst_file", "set destination file path", offsetof(CopyPriv, dst_file), DC_ProcedureOptionType_eString },
    { NULL }
};

DC_Procedure copy = {
    .name = "copy",
    .long_name = "Device copying",
    .suggest_default_value = SuggestDefaultValue,
    .open = Open,
    .perform = Perform,
    .close = Close,
    .priv_data_size = sizeof(CopyPriv),
    .options = options,
};

