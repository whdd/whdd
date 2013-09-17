#ifndef COPY_H
#define COPY_H

#include <stdlib.h>
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
    ReadStrategy_eSkipfail,
    ReadStrategy_eSkipfailNoReverse,
};

typedef struct ReadStrategyImpl ReadStrategyImpl;

struct copy_priv {
    const char *api_str;
    const char *read_strategy_str;
    const char *dst_file;
    const char *use_journal_str;
    int skip_blocks;
    enum Api api;
    enum ReadStrategy read_strategy;
    ReadStrategyImpl *read_strategy_impl;
    int use_journal;
    int64_t start_lba;
    int64_t end_lba;
    int64_t lba_to_process;
    int src_fd;
    int dst_fd;
    int64_t dst_file_end_lba;
    void *buf;
    AtaCommand ata_command;
    ScsiCommand scsi_command;
    int old_readahead;
    uint64_t blk_index;
    Zone *unread_zones;
    int nb_zones;
    Zone *current_zone;
    int current_zone_read_direction_reversive;
    void *read_strategy_priv;
    int journal_fd;
};
typedef struct copy_priv CopyPriv;

struct ReadStrategyImpl {
    const char *name;
    int (*init)(CopyPriv *copy_ctx);
    int (*get_task)(CopyPriv *copy_ctx, int64_t *lba_to_read, size_t *sectors_to_read);
    int (*use_results)(CopyPriv *copy_ctx, int64_t lba_to_read, size_t sectors_to_read, DC_BlockReport *report);
    void (*close)(CopyPriv *copy_ctx);
};

#define SECTORS_AT_ONCE 256
#define BLK_SIZE (SECTORS_AT_ONCE * 512) // FIXME hardcode
#define INDIVISIBLE_DEFECT_ZONE_SIZE_SECTORS 1000*1000  // 500 MB

typedef enum SectorStatus {
    SectorStatus_eUnread = 0,
    SectorStatus_eReadOk = 1,
    SectorStatus_eBlockReadError = 2,
    SectorStatus_eSectorReadError = 3,
} SectorStatus;

#endif  // COPY_H
