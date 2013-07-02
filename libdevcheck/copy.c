#define _FILE_OFFSET_BITS 64
#define _GNU_SOURCE
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
#include <sys/mman.h>
#include <stdio.h>

#include "procedure.h"
#include "scsi.h"
#include "copy.h"

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
    } else if (!strcmp(setting->name, "use_journal")) {
        setting->value = strdup("yes");
    } else if (!strcmp(setting->name, "skip_blocks")) {
        setting->value = strdup("5000");
    } else {
        return 1;
    }
    return 0;
}

static void append_zone(CopyPriv *priv, Zone *current_zone) {
    if (!priv->unread_zones) {
        priv->unread_zones = current_zone;
    } else {
        Zone *last_zone = priv->unread_zones;
        while (last_zone->next)
            last_zone = last_zone->next;
        last_zone->next = current_zone;
    }
    //fprintf(stderr, "appending %dth zone - begin_lba %"PRId64", end_lba %"PRId64"; begin defective: %d, end defective: %d\n", priv->nb_zones, current_zone->begin_lba, current_zone->end_lba, current_zone->begin_lba_defective, current_zone->end_lba_defective);
    priv->nb_zones++;
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
        extern ReadStrategyImpl read_strategy_smart;
        priv->read_strategy_impl = &read_strategy_smart;
    } else if (!strcmp(priv->read_strategy_str, "smart_noreverse")) {
        priv->read_strategy = ReadStrategy_eSmartNoReverse;
        extern ReadStrategyImpl read_strategy_smart_noreverse;
        priv->read_strategy_impl = &read_strategy_smart_noreverse;
    } else if (!strcmp(priv->read_strategy_str, "plain")) {
        priv->read_strategy = ReadStrategy_ePlain;
        extern ReadStrategyImpl read_strategy_plain;
        priv->read_strategy_impl = &read_strategy_plain;
    } else if (!strcmp(priv->read_strategy_str, "skipfail")) {
        priv->read_strategy = ReadStrategy_eSkipfail;
        extern ReadStrategyImpl read_strategy_skipfail;
        priv->read_strategy_impl = &read_strategy_skipfail;
    } else if (!strcmp(priv->read_strategy_str, "skipfail_noreverse")) {
        priv->read_strategy = ReadStrategy_eSkipfailNoReverse;
        extern ReadStrategyImpl read_strategy_skipfail_noreverse;
        priv->read_strategy_impl = &read_strategy_skipfail_noreverse;
    } else {
        return 1;
    }
    priv->read_strategy_impl->init(priv);

    priv->use_journal = !strcmp(priv->use_journal_str, "yes");

    ctx->blk_size = BLK_SIZE;
    priv->end_lba = ctx->dev->capacity / 512;
    priv->lba_to_process = priv->end_lba - priv->start_lba;
    ctx->progress.den = priv->lba_to_process;

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

    if (priv->use_journal) {
        char journal_file_name[100];
	snprintf(journal_file_name, sizeof(journal_file_name), "whdd_copy_journal_%s", ctx->dev->dev_fs_name);  // TODO Include Model name and Serial Number
        struct stat journal_file_stat;
        int journal_file_exists = 1;
        r = stat(journal_file_name, &journal_file_stat);
        if (r == -1) {
            if (errno == ENOENT)
                journal_file_exists = 0;
            else
                goto fail_journal_open;
        }
        if (journal_file_exists) {
            // Check it for sanity - size, maybe sth else
            if (journal_file_stat.st_size != priv->end_lba) {
                dc_log(DC_LOG_ERROR, "Wrong size of journal file");
                goto fail_journal_open;
            }
        }

        // Open file
        priv->journal_fd = open(journal_file_name, O_RDWR | O_NOATIME | O_CREAT | O_LARGEFILE, S_IRUSR | S_IWUSR );
        if (priv->journal_fd == -1) {
            dc_log(DC_LOG_ERROR, "Failed to open journal file");
            goto fail_journal_open;
        }
        if (journal_file_exists) {
            // Reset zones
            priv->nb_zones = 0;
            free(priv->unread_zones);
            priv->unread_zones = NULL;
            ctx->progress.den = 0;

            priv->journal_file_mmapped = mmap(NULL, journal_file_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, priv->journal_fd, 0);
            if (priv->journal_file_mmapped == MAP_FAILED) {
                dc_log(DC_LOG_ERROR, "mmap() failed with errno %d (%s)", errno, strerror(errno));
                goto fail_mmap_journal;
            }
            // Parse it to zones structure
            Zone *current_zone = NULL;
            char prev_sector_status;
            for (int64_t i = 0; i < priv->end_lba; i++) {
                char sector_status = priv->journal_file_mmapped[i];
                switch ((SectorStatus)sector_status) {
                    case SectorStatus_eUnread:
                        if (!current_zone) {
                            current_zone = calloc(1, sizeof(*current_zone));
                            assert(current_zone);
                            current_zone->begin_lba = i;
                            if ((i > 0)
                                    && ((prev_sector_status == SectorStatus_eBlockReadError)
                                        || (prev_sector_status == SectorStatus_eSectorReadError)))
                                current_zone->begin_lba_defective = 1;
                        }
                        break;
                    case SectorStatus_eReadOk:
                    case SectorStatus_eBlockReadError:
                    case SectorStatus_eSectorReadError:
                        if (current_zone) {
                            current_zone->end_lba = i;
                            if ((sector_status == SectorStatus_eBlockReadError)
                                    || (sector_status == SectorStatus_eSectorReadError))
                                current_zone->end_lba_defective = 1;
                            ctx->progress.den += current_zone->end_lba - current_zone->begin_lba;
                            append_zone(priv, current_zone);
                            current_zone = NULL;
                        }
                        break;
                }
                prev_sector_status = sector_status;
            }
            if (current_zone) {  // Zone finishing at end of disk
                current_zone->end_lba = priv->end_lba;
                ctx->progress.den += current_zone->end_lba - current_zone->begin_lba;
                append_zone(priv, current_zone);
                current_zone = NULL;
            }
        } else {
            // Fill appropriately
            char filler[1*1024*1024];
            memset(filler, 0, sizeof(filler));
            for (int i = 0; i < priv->end_lba;) {
                int blocksize = priv->end_lba - i;
                if (blocksize > (int)sizeof(filler))
                    blocksize = sizeof(filler);
                r = write(priv->journal_fd, &filler, blocksize);
                assert(r == blocksize);
                i += blocksize;
            }
            fdatasync(priv->journal_fd);
            priv->journal_file_mmapped = mmap(NULL, priv->end_lba, PROT_READ | PROT_WRITE, MAP_SHARED, priv->journal_fd, 0);
            if (priv->journal_file_mmapped == MAP_FAILED) {
                dc_log(DC_LOG_ERROR, "mmap() failed with errno %d (%s)", errno, strerror(errno));
                goto fail_mmap_journal;
            }
        }
    }

    //fprintf(stderr, "Zones list at beginning of procedure:\n");
    //for (Zone *iter = priv->unread_zones; iter; iter = iter->next) {
    //    fprintf(stderr, "begin_lba %"PRId64", end_lba %"PRId64"; begin defective: %d, end defective: %d\n", iter->begin_lba, iter->end_lba, iter->begin_lba_defective, iter->end_lba_defective);
    //}
    return 0;
fail_mmap_journal:
    close(priv->journal_fd);
fail_journal_open:
    close(priv->dst_fd);
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
    size_t sectors_to_read;
    int64_t lba_to_read;
    int r;
    int error_flag = 0;

    // Updating context
    r = priv->read_strategy_impl->get_task(priv, &lba_to_read, &sectors_to_read);
    if (r)
      return r;
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
    if (priv->use_journal) {
        char sector_status;
        if (error_flag) {
            if (sectors_to_read == 1)
                sector_status = SectorStatus_eSectorReadError;
            else
                sector_status = SectorStatus_eBlockReadError;
        } else {
            sector_status = SectorStatus_eReadOk;
        }
        assert(r != -1);
        for (int64_t i = 0; i < (int64_t)sectors_to_read; i++)
            priv->journal_file_mmapped[lba_to_read + i] = sector_status;
        r = msync(priv->journal_file_mmapped, priv->end_lba, MS_ASYNC);
        assert(!r);
    }
    r = priv->read_strategy_impl->use_results(priv, lba_to_read, sectors_to_read, &ctx->report);
    if (r)
        ret = 1;
    ctx->progress.num += sectors_to_read;
    priv->lba_to_process -= sectors_to_read;

    if (ret)
        dc_log(DC_LOG_ERROR, "returning non-zero from Perform");
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
    if (priv->use_journal) {
        munmap(priv->journal_file_mmapped, priv->end_lba);
        close(priv->journal_fd);
    }
    priv->read_strategy_impl->close(priv);
}

static DC_ProcedureOption options[] = {
    { "api", "select read operation API: \"posix\" for POSIX read(), \"ata\" for ATA \"READ DMA EXT\" command", offsetof(CopyPriv, api_str), DC_ProcedureOptionType_eString },
    { "read_strategy", "select from options: plain, smart, smart_noreverse, skipfail, skipfail_noreverse. See help on copy procedure for details.", offsetof(CopyPriv, read_strategy_str), DC_ProcedureOptionType_eString },
    { "dst_file", "set destination file path", offsetof(CopyPriv, dst_file), DC_ProcedureOptionType_eString },
    { "use_journal", "set whether to generate and use journal for operation resume possibility (yes/no)", offsetof(CopyPriv, use_journal_str), DC_ProcedureOptionType_eString },
    { "skip_blocks", "set jump size in blocks of 256*512 bytes, when read error is met (for skipfail* strategies)", offsetof(CopyPriv, skip_blocks), DC_ProcedureOptionType_eInt64 },
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
	"    skipfail: read sequentially until fail. Then jump skip_blocks blocks (of 256*512 bytes), and read backward up to failure. Then go forward.\n"
	"    skipfail_noreverse: same as \"skipfail\", but after jump data is read forward (the gap is omitted).\n"
        "",
    .suggest_default_value = SuggestDefaultValue,
    .open = Open,
    .perform = Perform,
    .close = Close,
    .priv_data_size = sizeof(CopyPriv),
    .options = options,
};

