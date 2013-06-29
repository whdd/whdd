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
#include "scsi.h"

typedef struct zone {
    // begin_lba < end_lba
    int64_t begin_lba;
    int64_t end_lba;  // LBA of the first sector beyond zone
    int begin_lba_defective;  // Whether reading near begin_lba failed
    int end_lba_defective;  // Whether reading near end_lba failed
    struct zone *next;
} Zone;

enum ReadStrategy {
    ReadStrategy_ePlain,
    ReadStrategy_eSmart,
    ReadStrategy_eSmartNoReverse,
};

struct copy_priv {
    const char *api_str;
    const char *read_strategy_str;
    const char *dst_file;
    enum Api api;
    enum ReadStrategy read_strategy;
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
    Zone *unread_zones;
    int nb_zones;
    Zone *current_zone;
    int current_zone_read_direction_reversive;
};
typedef struct copy_priv CopyPriv;

#define SECTORS_AT_ONCE 256
#define BLK_SIZE (SECTORS_AT_ONCE * 512) // FIXME hardcode
#define INDIVISIBLE_DEFECT_ZONE_SIZE_SECTORS 1000*1000  // 500 MB

static int SuggestDefaultValue(DC_Dev *dev, DC_OptionSetting *setting) {
    (void)dev;
    if (!strcmp(setting->name, "api")) {
        if (dev->ata_capable)
            setting->value = strdup("ata");
        else
            setting->value = strdup("posix");
    } else if (!strcmp(setting->name, "read_strategy")) {
        setting->value = strdup("smart_noreverse");
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
    if (!strcmp(priv->api_str, "ata")) {
        priv->api = Api_eAta;
    } else if (!strcmp(priv->api_str, "posix")) {
        priv->api = Api_ePosix;
    } else {
        return 1;
    }

    if (priv->api == Api_eAta && !ctx->dev->ata_capable)
        return 1;

    if (!strcmp(priv->read_strategy_str, "smart")) {
        priv->read_strategy = ReadStrategy_eSmart;
    } else if (!strcmp(priv->read_strategy_str, "smart_noreverse")) {
        priv->read_strategy = ReadStrategy_eSmartNoReverse;
    } else if (!strcmp(priv->read_strategy_str, "plain")) {
        priv->read_strategy = ReadStrategy_ePlain;
    } else {
        return 1;
    }
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
    priv->dst_fd = open(priv->dst_file, O_WRONLY | O_LARGEFILE | O_NOATIME | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (priv->dst_fd == -1) {
        assert(0);
        dc_log(DC_LOG_FATAL, "open %s fail\n", priv->dst_file);
        goto fail_dst_open;
    }

    priv->nb_zones = 1;
    priv->unread_zones = calloc(1, sizeof(Zone));
    assert(priv->unread_zones);
    priv->unread_zones->begin_lba = priv->start_lba;
    priv->unread_zones->end_lba = priv->end_lba;

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

static int get_task(CopyPriv *priv, int64_t *lba_to_read, size_t *sectors_to_read) {
    if (priv->read_strategy == ReadStrategy_ePlain) {
        *lba_to_read = priv->blk_index * SECTORS_AT_ONCE;
        *sectors_to_read = (priv->end_lba - *lba_to_read) < SECTORS_AT_ONCE ? (priv->end_lba - *lba_to_read) : SECTORS_AT_ONCE;
        return 0;
    }
    Zone *entry;
    assert(priv->unread_zones);  // We should not be there if all space has been read
    assert(priv->nb_zones);

    // Search for zone with non-defective border (beginning or end)
    for (entry = priv->unread_zones; entry; entry = entry->next) {
        int64_t zone_length_sectors = entry->end_lba - entry->begin_lba;
        if (!entry->begin_lba_defective) {
            priv->current_zone = entry;
            priv->current_zone_read_direction_reversive = 0;
            *sectors_to_read = (zone_length_sectors < SECTORS_AT_ONCE) ? zone_length_sectors : SECTORS_AT_ONCE;
            *lba_to_read = entry->begin_lba;
            return 0;
        }
        if (priv->read_strategy == ReadStrategy_eSmartNoReverse)
            continue;
        if (!entry->end_lba_defective) {
            priv->current_zone = entry;
            priv->current_zone_read_direction_reversive = 1;
            *sectors_to_read = (zone_length_sectors < SECTORS_AT_ONCE) ? zone_length_sectors : SECTORS_AT_ONCE;
            *lba_to_read = entry->end_lba - *sectors_to_read;
            return 0;
        }
    }
    // There are only zones with defective borders (both ends, in case of ReadStrategy_eSmart)
    // Find largest unread zone
    Zone *largest_zone = priv->unread_zones;
    for (entry = priv->unread_zones->next; entry; entry = entry->next) {
        if ((largest_zone->end_lba - largest_zone->begin_lba)
                < (entry->end_lba - entry->begin_lba))
            largest_zone = entry;
    }
    entry = largest_zone;
    assert(entry->begin_lba_defective);
    assert((priv->read_strategy == ReadStrategy_eSmartNoReverse) || entry->end_lba_defective);
    int64_t zone_length_sectors = entry->end_lba - entry->begin_lba;
    if ((zone_length_sectors > INDIVISIBLE_DEFECT_ZONE_SIZE_SECTORS)  // Enough big zone to try in middle of it
            && (priv->nb_zones < 1000)) {  // And we won't get in trouble of inflation of zones list
        priv->nb_zones++;
        Zone *newentry = calloc(1, sizeof(Zone));
        assert(newentry);
        newentry->next = entry->next;
        entry->next = newentry;
        newentry->end_lba = entry->end_lba;
        newentry->end_lba_defective = entry->end_lba_defective;
        newentry->begin_lba = entry->begin_lba + (zone_length_sectors / 2);
        newentry->begin_lba -= (newentry->begin_lba % SECTORS_AT_ONCE);  // align to block size
        assert((entry->begin_lba < newentry->begin_lba) && (newentry->begin_lba < newentry->end_lba));
        entry->end_lba = newentry->begin_lba;
        entry->end_lba_defective = 0;
        return get_task(priv, lba_to_read, sectors_to_read);  // It will go by first branch in this function
    }

    priv->current_zone = entry;
    priv->current_zone_read_direction_reversive = 0;
    *sectors_to_read = (zone_length_sectors < SECTORS_AT_ONCE) ? zone_length_sectors : SECTORS_AT_ONCE;
    *lba_to_read = entry->begin_lba;
    return 0;
}

static int update_zones(CopyPriv *priv, int64_t lba_to_read, size_t sectors_to_read, int read_failed) {
    if (priv->read_strategy == ReadStrategy_ePlain)
        return 0;
    Zone *prev;
    Zone *entry;
    if (priv->current_zone->begin_lba_defective
            && (priv->read_strategy == ReadStrategy_eSmartNoReverse || priv->current_zone->end_lba_defective)) {
        // We're reading straight ahead and ignoring failures
        assert(lba_to_read == priv->current_zone->begin_lba);
        assert(priv->current_zone_read_direction_reversive == 0);
        priv->current_zone->begin_lba += sectors_to_read;
    } else if (!priv->current_zone->begin_lba_defective) {
        // Reading "ok" zone until failure
        assert(lba_to_read == priv->current_zone->begin_lba);
        assert(priv->current_zone_read_direction_reversive == 0);
        priv->current_zone->begin_lba += sectors_to_read;
        priv->current_zone->begin_lba_defective = read_failed;
    } else {
        // Reading zone with defective beginning in reverse direction
        assert(priv->read_strategy == ReadStrategy_eSmart);
        assert(!priv->current_zone->end_lba_defective);
        assert(lba_to_read == (int64_t)(priv->current_zone->end_lba - sectors_to_read));
        assert(priv->current_zone_read_direction_reversive == 1);
        priv->current_zone->end_lba -= sectors_to_read;
        priv->current_zone->end_lba_defective = read_failed;
    }
    // Check zones list and eliminate zero-length ones
    for (prev = NULL, entry = priv->unread_zones; entry; prev = entry, entry = entry->next) {
        if (entry->begin_lba == entry->end_lba) {
            if (prev)
                prev->next = entry->next;
            else
                priv->unread_zones = entry->next;
            free(entry);
            priv->nb_zones--;
        }
    }
    return 0;
}

static int Perform(DC_ProcedureCtx *ctx) {
    ssize_t read_ret;
    int ioctl_ret;
    int ret = 0;
    CopyPriv *priv = ctx->priv;
    size_t sectors_to_read;
    int64_t lba_to_read;
    int r;
    int error_flag = 0;

    // Updating context
    r = get_task(priv, &lba_to_read, &sectors_to_read);
    assert(!r);
    lseek(priv->src_fd, 512 * lba_to_read, SEEK_SET);
    lseek(priv->dst_fd, 512 * lba_to_read, SEEK_SET);
    ctx->report.lba = lba_to_read;
    ctx->report.sectors_processed = sectors_to_read;
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
        priv->scsi_command.scsi_cmd[2] = 0x0e;  // CK_COND=0 T_DIR=1 BYTE_BLOCK=1 T_LENGTH=10b
#if 0
        int i;
        for (i = 0; i < sizeof(priv->scsi_command.scsi_cmd); i++)
          fprintf(stderr, "%02hhx", priv->scsi_command.scsi_cmd[i]);
        fprintf(stderr, "\n");
#endif
    }

    // Timing
    _dc_proc_time_pre(ctx);

    // Acting
    if (priv->api == Api_eAta)
        ioctl_ret = ioctl(priv->src_fd, SG_IO, &priv->scsi_command);
    else
        read_ret = read(priv->src_fd, priv->buf, sectors_to_read * 512);

    // Timing
    _dc_proc_time_post(ctx);

    // Error handling
    if (priv->api == Api_eAta) {
        // Updating context
        if (ioctl_ret) {
            ctx->report.blk_status = DC_BlockStatus_eError;
            ret = 1;
        }
        ctx->report.blk_status = scsi_ata_check_return_status(&priv->scsi_command);
        if (ctx->report.blk_status)
            error_flag = 1;
    } else {
        if (read_ret != (ssize_t)sectors_to_read * 512) {
            error_flag = 1;

            // Updating context
            ctx->report.blk_status = DC_BlockStatus_eError;
        }
    }
    if (error_flag && priv->read_strategy == ReadStrategy_ePlain)
        ret = 1;  // Abort plain copying on first read failure

    // Acting: writing; not timed
    if (!error_flag) {
        int write_ret = write(priv->dst_fd, priv->buf, sectors_to_read * 512);

        // Error handling
        if (write_ret != (ssize_t)sectors_to_read * 512) {
            // Updating context
            ctx->report.blk_status = DC_BlockStatus_eError;
            // TODO Transmit to user info that _write phase_ has failed
        }
    }

    // Updating context
    r = update_zones(priv, lba_to_read, sectors_to_read, error_flag);
    assert(!r);
    ctx->progress.num++;
    priv->lba_to_process -= sectors_to_read;

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
    { "api", "select read operation API: \"posix\" for POSIX read(), \"ata\" for ATA \"READ DMA EXT\" command", offsetof(CopyPriv, api_str), DC_ProcedureOptionType_eString },
    { "read_strategy", "select read strategy: \"plain\" for sequential reading, \"smart\" for algorithm which defers reading defect zones and copies normal zones first, \"smart_noreverse\" for same as smart but not reading in reverse direction", offsetof(CopyPriv, read_strategy_str), DC_ProcedureOptionType_eString },
    { "dst_file", "set destination file path", offsetof(CopyPriv, dst_file), DC_ProcedureOptionType_eString },
    { NULL }
};


DC_Procedure copy = {
    .name = "copy",
    .display_name = "Device copying",
    .help = "Copies entire device to given destination (another device or generic file).\n"
        "Parameters:\n"
        "api: choose API used to read data from source device.\n"
        "    ata: use ATA \"READ DMA EXT\" command.\n"
        "    posix: use POSIX read() in direct mode.\n"
        "\n"
        "read_strategy: choose read strategy. All strategies are designed to make least possible harm to defective source device.\n"
        "    plain: read sequentially, abort on first read fail.\n"
        "    smart: read sequentially until read error is met. Then it reads from another end of disk space. When this ends with read error, too, it jumps to the middle of unread zone and reads forward from there. This results in having two zones of unread data. This way it jumps into middle of unread zones until there are < 1000 of them in table, and they are > 500 MB. When it cannot further jump into zones, it just reads sequentially remaining unread zones. Thus reading near failure points is delayed.\n"
        "    smart_noreverse: same as \"smart\", but reverse reading is prohibited; jump into middle of zone is considered on forward read failure.\n"
        "",
    .suggest_default_value = SuggestDefaultValue,
    .open = Open,
    .perform = Perform,
    .close = Close,
    .priv_data_size = sizeof(CopyPriv),
    .options = options,
};

