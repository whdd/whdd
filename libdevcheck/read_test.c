#define _FILE_OFFSET_BITS 64
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

struct read_priv {
    const char *api_str;
    int64_t start_lba;
    enum Api api;
    int64_t end_lba;
    int64_t lba_to_process;
    int fd;
    void *buf;
    AtaCommand ata_command;
    ScsiCommand scsi_command;
    long old_readahead;
    int readahead_valid;
    int use_lba48;
    uint64_t current_lba;
};
typedef struct read_priv ReadPriv;

#define SECTORS_AT_ONCE 256
#define BLK_SIZE (SECTORS_AT_ONCE * 512) // FIXME hardcode

static int SuggestDefaultValue(DC_Dev *dev, DC_OptionSetting *setting) {
    (void)dev;
    if (!strcmp(setting->name, "api")) {
        if (dev->ata_capable)
            setting->value = strdup("ata");
        else
            setting->value = strdup("posix");
    } else if (!strcmp(setting->name, "start_lba")) {
        setting->value = strdup("0");
    } else {
        return 1;
    }
    return 0;
}

static int Open(DC_ProcedureCtx *ctx) {
    int r;
    int open_flags;
    ReadPriv *priv = ctx->priv;

    // Setting context
    if (!strcmp(priv->api_str, "ata"))
        priv->api = Api_eAta;
    else if (!strcmp(priv->api_str, "posix"))
        priv->api = Api_ePosix;
    else {
        dc_log(DC_LOG_FATAL, "Unknown read API: %s\n", priv->api_str);
        return 1;
    }
    if (priv->api == Api_eAta && !ctx->dev->ata_capable) {
        dc_log(DC_LOG_FATAL, "ATA access is unavailable; select the posix API\n");
        return 1;
    }
    ctx->blk_size = BLK_SIZE;
    priv->current_lba = priv->start_lba;
    priv->end_lba = ctx->dev->capacity / 512;
    if (priv->start_lba < 0 || priv->start_lba >= priv->end_lba) {
        dc_log(DC_LOG_FATAL, "Invalid read range: start LBA=%"PRId64", capacity=%"PRIu64" bytes\n",
                priv->start_lba, ctx->dev->capacity);
        return 1;
    }
    priv->lba_to_process = priv->end_lba - priv->start_lba;
    ctx->progress.den = priv->lba_to_process / SECTORS_AT_ONCE;
    if (priv->lba_to_process % SECTORS_AT_ONCE)
        ctx->progress.den++;

    if (priv->api == Api_eAta) {
        priv->use_lba48 = ata_identify_has_lba48(ctx->dev->identify);
        if (!priv->use_lba48 && priv->end_lba > (1LL << 28)) {
            dc_log(DC_LOG_FATAL, "Device capacity exceeds the 28-bit ATA address range\n");
            return 1;
        }
        open_flags = O_RDWR;
    } else {
        r = posix_memalign(&priv->buf, sysconf(_SC_PAGESIZE), ctx->blk_size);
        if (r) {
            dc_log(DC_LOG_FATAL, "Read buffer allocation failed: %s\n", strerror(r));
            return 1;
        }

        open_flags = O_RDONLY | O_DIRECT | O_LARGEFILE | O_NOATIME;
    }

    priv->fd = open(ctx->dev->dev_path, open_flags);
    if (priv->fd == -1) {
        dc_log(DC_LOG_FATAL, "Cannot open %s: %s\n", ctx->dev->dev_path, strerror(errno));
        free(priv->buf);
        priv->buf = NULL;
        return 1;
    }

    if (lseek(priv->fd, 512 * priv->start_lba, SEEK_SET) == (off_t)-1) {
        dc_log(DC_LOG_FATAL, "Cannot seek %s: %s\n", ctx->dev->dev_path, strerror(errno));
        free(priv->buf);
        priv->buf = NULL;
        close(priv->fd);
        return 1;
    }
    r = ioctl(priv->fd, BLKFLSBUF, NULL);
    if (r == -1)
      dc_log(DC_LOG_WARNING, "Flushing block device buffers failed\n");
    r = ioctl(priv->fd, BLKRAGET, &priv->old_readahead);
    priv->readahead_valid = r != -1;
    if (!priv->readahead_valid)
      dc_log(DC_LOG_WARNING, "Getting block device readahead setting failed\n");
    else if (ioctl(priv->fd, BLKRASET, 0) == -1)
      dc_log(DC_LOG_WARNING, "Disabling block device readahead setting failed\n");

    return 0;
}

static int Perform(DC_ProcedureCtx *ctx) {
    ssize_t read_ret = -1;
    int ioctl_ret = -1;
    int ret = 0;
    ReadPriv *priv = ctx->priv;
    size_t sectors_to_read = (priv->lba_to_process < SECTORS_AT_ONCE) ? priv->lba_to_process : SECTORS_AT_ONCE;

    // Updating context
    ctx->report.lba = priv->current_lba;
    ctx->report.sectors_processed = sectors_to_read;
    ctx->report.blk_status = DC_BlockStatus_eOk;

    // Preparing to act
    if (priv->api == Api_eAta) {
        memset(&priv->ata_command, 0, sizeof(priv->ata_command));
        memset(&priv->scsi_command, 0, sizeof(priv->scsi_command));
        int command = priv->use_lba48 ? WIN_VERIFY_EXT : WIN_VERIFY;
        prepare_ata_command(&priv->ata_command, command, priv->current_lba, sectors_to_read);
        prepare_scsi_command_from_ata(&priv->scsi_command, &priv->ata_command);
    }

    // Timing
    _dc_proc_time_pre(ctx);

    // Acting
    if (priv->api == Api_eAta)
        ioctl_ret = ioctl(priv->fd, SG_IO, &priv->scsi_command);
    else
        read_ret = read(priv->fd, priv->buf, sectors_to_read * 512);

    // Timing
    _dc_proc_time_post(ctx);

    // Error handling
    if (priv->api == Api_eAta) {
        // Updating context
        if (ioctl_ret) {
            ctx->report.blk_status = DC_BlockStatus_eError;
            ret = 1;
        }
        else
            ctx->report.blk_status = scsi_ata_check_return_status(&priv->scsi_command);
    } else {
        if ((int)read_ret != (int)sectors_to_read * 512) {
            // Position of fd is undefined. Set fd position to read next block
            lseek(priv->fd, 512 * priv->current_lba, SEEK_SET);

            // Updating context
            ctx->report.blk_status = DC_BlockStatus_eError;
        }
    }

    // Updating context
    ctx->progress.num++;
    priv->lba_to_process -= sectors_to_read;
    priv->current_lba += sectors_to_read;

    return ret;
}

static void Close(DC_ProcedureCtx *ctx) {
    ReadPriv *priv = ctx->priv;
    if (priv->readahead_valid && ioctl(priv->fd, BLKRASET, priv->old_readahead) == -1)
      dc_log(DC_LOG_WARNING, "Restoring block device readahead setting failed\n");
    free(priv->buf);
    close(priv->fd);
}

static const char * const api_choices[] = {"ata", "posix", NULL};
static DC_ProcedureOption options[] = {
    { "api", "select operation API: \"posix\" for POSIX read(), \"ata\" for ATA \"READ VERIFY EXT\" command", offsetof(ReadPriv, api_str), DC_ProcedureOptionType_eString, api_choices },
    { "start_lba", "set LBA address to begin from", offsetof(ReadPriv, start_lba), DC_ProcedureOptionType_eInt64 },
    { NULL }
};


DC_Procedure read_test = {
    .name = "read_test",
    .display_name = "Read test",
    .help = "Verifies entire device with reading. It reads data sequentially, from given start LBA up to end. To get data from source device, it may use ATA \"READ VERIFY EXT\" command, or POSIX read() function, by user choice.",
    .suggest_default_value = SuggestDefaultValue,
    .open = Open,
    .perform = Perform,
    .close = Close,
    .priv_data_size = sizeof(ReadPriv),
    .options = options,
};

