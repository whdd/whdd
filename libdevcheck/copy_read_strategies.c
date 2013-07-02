#include <assert.h>
#include <stdio.h>

#include "copy.h"

typedef struct SmartStrategyCtx {
    int stage;
} SmartStrategyCtx;

typedef struct SkipfailStrategyCtx {
    int dummy;
} SkipfailStrategyCtx;

static int common_update_zones(CopyPriv *priv, int64_t lba_to_read, size_t sectors_to_read, DC_BlockReport *report);

static int plain_get_task(CopyPriv *priv, int64_t *lba_to_read, size_t *sectors_to_read) {
    Zone *zone = priv->unread_zones;
    priv->current_zone = zone;
    *lba_to_read = zone->begin_lba;
    *sectors_to_read = zone->end_lba - zone->begin_lba;
    if (*sectors_to_read > SECTORS_AT_ONCE)
        *sectors_to_read = SECTORS_AT_ONCE;
    return 0;
}

static int plain_update_zones(CopyPriv *priv, int64_t lba_to_read, size_t sectors_to_read, DC_BlockReport *report) {
    common_update_zones(priv, lba_to_read, sectors_to_read, report);
    return report->blk_status;
}

static Zone *find_largest_zone(CopyPriv *priv) {
    Zone *largest_zone = priv->unread_zones;
    Zone *entry;
    for (entry = priv->unread_zones->next; entry; entry = entry->next) {
        if ((largest_zone->end_lba - largest_zone->begin_lba)
                < (entry->end_lba - entry->begin_lba))
            largest_zone = entry;
    }
    return largest_zone;
}

static int give_task_proceeding_current_zone(CopyPriv *priv, int64_t *lba_to_read, size_t *sectors_to_read) {
    Zone *entry = priv->current_zone;
    int64_t zone_length_sectors = entry->end_lba - entry->begin_lba;
    *sectors_to_read = (zone_length_sectors < SECTORS_AT_ONCE) ? zone_length_sectors : SECTORS_AT_ONCE;
    if (priv->current_zone_read_direction_reversive)
        *lba_to_read = entry->end_lba - *sectors_to_read;
    else
        *lba_to_read = entry->begin_lba;
    return 0;
}

static int smart_set_first_processable_zone_current(CopyPriv *priv) {
    Zone *entry;
    // Search for zone with non-defective border (beginning or end)
    for (entry = priv->unread_zones; entry; entry = entry->next) {
        if (!entry->begin_lba_defective) {
            priv->current_zone = entry;
            priv->current_zone_read_direction_reversive = 0;
            return 0;
        }
        if (priv->read_strategy == ReadStrategy_eSmartNoReverse)
            continue;
        if (!entry->end_lba_defective) {
            priv->current_zone = entry;
            priv->current_zone_read_direction_reversive = 1;
            return 0;
        }
    }
    return 1;
}

static int smart_get_task(CopyPriv *priv, int64_t *lba_to_read, size_t *sectors_to_read) {
    int r;
    Zone *entry;
    SmartStrategyCtx *smart_ctx = priv->read_strategy_priv;
    assert(priv->unread_zones);  // We should not be there if all space has been read
    assert(priv->nb_zones);

    // If we have current zone and it is ok, proceed with it to avoid jumps
    if (priv->current_zone)
        return give_task_proceeding_current_zone(priv, lba_to_read, sectors_to_read);

    if (smart_ctx->stage == 1) {
        // Consequentially read forward, ignoring errors
        priv->current_zone = priv->unread_zones;
        priv->current_zone_read_direction_reversive = 0;
        return give_task_proceeding_current_zone(priv, lba_to_read, sectors_to_read);
    }

    // Search for zone with non-defective border (beginning or end)
    r = smart_set_first_processable_zone_current(priv);
    if (!r)
        return give_task_proceeding_current_zone(priv, lba_to_read, sectors_to_read);

    // There are only zones with defective borders (both ends, in case of ReadStrategy_eSmart)
    // Find largest unread zone
    entry = find_largest_zone(priv);
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

        r = smart_set_first_processable_zone_current(priv);
        if (r)
            return r;
        return give_task_proceeding_current_zone(priv, lba_to_read, sectors_to_read);
    } else {
        smart_ctx->stage = 1;
        priv->current_zone = priv->unread_zones;
        priv->current_zone_read_direction_reversive = 0;
        return give_task_proceeding_current_zone(priv, lba_to_read, sectors_to_read);
    }
}

static int smart_update_zones(CopyPriv *priv, int64_t lba_to_read, size_t sectors_to_read, DC_BlockReport *report) {
    SmartStrategyCtx *smart_ctx = priv->read_strategy_priv;
    common_update_zones(priv, lba_to_read, sectors_to_read, report);
    if (report->blk_status && (smart_ctx->stage == 0))
        priv->current_zone = NULL;  // Let get_task algorithm re-choose zone and direction
    return 0;
}

static int common_update_zones(CopyPriv *priv, int64_t lba_to_read, size_t sectors_to_read, DC_BlockReport *report) {
    int read_failed = report->blk_status;
    Zone *zone = priv->current_zone;
    assert(zone);
    // Update unread zone bounds
    if (priv->current_zone_read_direction_reversive) {
        zone->end_lba -= sectors_to_read;
        assert(zone->end_lba == lba_to_read);
        zone->end_lba_defective = read_failed;
    } else {
        assert(zone->begin_lba == lba_to_read);
        zone->begin_lba += sectors_to_read;
        zone->begin_lba_defective = read_failed;
    }

    assert(zone->begin_lba <= zone->end_lba);
    // Check if zone got zero length and remove it in such case
    if (zone->begin_lba == zone->end_lba) {
        Zone *prev;
        Zone *entry;
        for (prev = NULL, entry = priv->unread_zones; entry; prev = entry, entry = entry->next) {
            if (entry == zone) {
                if (prev)
                    prev->next = entry->next;
                else
                    priv->unread_zones = entry->next;
                free(entry);
                priv->nb_zones--;
                break;
            }
        }
        priv->current_zone = NULL;
    }
    return 0;
}

static int skipfail_get_task(CopyPriv *priv, int64_t *lba_to_read, size_t *sectors_to_read) {
    Zone *entry;
    SkipfailStrategyCtx *skipfail_ctx = priv->read_strategy_priv;
    (void)skipfail_ctx;
    assert(priv->unread_zones);  // We should not be there if all space has been read
    assert(priv->nb_zones);

    // If we have current zone and it is ok, proceed with it to avoid jumps
    if (priv->current_zone)
        return give_task_proceeding_current_zone(priv, lba_to_read, sectors_to_read);

    // There are only zones with defective borders
    entry = priv->unread_zones;
    while (entry) {
        int64_t zone_length_sectors = entry->end_lba - entry->begin_lba;
        if (!entry->begin_lba_defective) {
            priv->current_zone = entry;
            priv->current_zone_read_direction_reversive = 0;
            return give_task_proceeding_current_zone(priv, lba_to_read, sectors_to_read);
        } else if ((priv->read_strategy != ReadStrategy_eSkipfailNoReverse)
                && !entry->end_lba_defective
                && entry->next /* Don't read from end of disk */) {
            priv->current_zone = entry;
            priv->current_zone_read_direction_reversive = 1;
            return give_task_proceeding_current_zone(priv, lba_to_read, sectors_to_read);
        } else if ((zone_length_sectors > priv->skip_blocks * SECTORS_AT_ONCE)  // Enough big zone to try in middle of it
                ) {
            priv->nb_zones++;
            Zone *newentry = calloc(1, sizeof(Zone));
            assert(newentry);
            newentry->next = entry->next;
            entry->next = newentry;
            newentry->end_lba = entry->end_lba;
            newentry->end_lba_defective = entry->end_lba_defective;
            newentry->begin_lba = entry->begin_lba + priv->skip_blocks * SECTORS_AT_ONCE;
            newentry->begin_lba -= (newentry->begin_lba % SECTORS_AT_ONCE);  // align to block size
            assert((entry->begin_lba < newentry->begin_lba) && (newentry->begin_lba < newentry->end_lba));
            entry->end_lba = newentry->begin_lba;
            entry->end_lba_defective = 0;
            //fprintf(stderr, "Made up new zone. New zones list:\n");
            //for (Zone *iter = priv->unread_zones; iter; iter = iter->next) {
            //    fprintf(stderr, "begin_lba %"PRId64", end_lba %"PRId64"; begin defective: %d, end defective: %d\n", iter->begin_lba, iter->end_lba, iter->begin_lba_defective, iter->end_lba_defective);
            //}
            if (priv->read_strategy == ReadStrategy_eSkipfailNoReverse) {
                priv->current_zone = newentry;
                priv->current_zone_read_direction_reversive = 0;
            } else {
                priv->current_zone = entry;
                priv->current_zone_read_direction_reversive = 1;
            }
            return give_task_proceeding_current_zone(priv, lba_to_read, sectors_to_read);
        }
        entry = entry->next;
    }
    return 1;  // All remaining zones are too small to jump into them
}

static int skipfail_update_zones(CopyPriv *priv, int64_t lba_to_read, size_t sectors_to_read, DC_BlockReport *report) {
    SkipfailStrategyCtx *skipfail_ctx = priv->read_strategy_priv;
    (void)skipfail_ctx;
    common_update_zones(priv, lba_to_read, sectors_to_read, report);
    if (report->blk_status)
        priv->current_zone = NULL;  // Let get_task algorithm re-choose zone and direction
    return 0;
}

int plain_init(CopyPriv *copy_ctx) {
    (void)copy_ctx;
    return 0;
}

void plain_close(CopyPriv *copy_ctx) {
    (void)copy_ctx;
}

int smart_init(CopyPriv *copy_ctx) {
    copy_ctx->read_strategy_priv = calloc(1, sizeof(SmartStrategyCtx));
    assert(copy_ctx->read_strategy_priv);
    SmartStrategyCtx *smart_ctx = copy_ctx->read_strategy_priv;
    smart_ctx->stage = 0;
    return 0;
}

void smart_close(CopyPriv *copy_ctx) {
    free(copy_ctx->read_strategy_priv);
}

int skipfail_init(CopyPriv *copy_ctx) {
    copy_ctx->read_strategy_priv = calloc(1, sizeof(SmartStrategyCtx));
    assert(copy_ctx->read_strategy_priv);
    SkipfailStrategyCtx *skipfail_ctx = copy_ctx->read_strategy_priv;
    (void)skipfail_ctx;
    return 0;
}

void skipfail_close(CopyPriv *copy_ctx) {
    free(copy_ctx->read_strategy_priv);
}

ReadStrategyImpl read_strategy_plain = {
    .name = "plain",
    .init = plain_init,
    .get_task = plain_get_task,
    .use_results = plain_update_zones,
    .close = plain_close,
};

ReadStrategyImpl read_strategy_smart = {
    .name = "smart",
    .init = smart_init,
    .get_task = smart_get_task,
    .use_results = smart_update_zones,
    .close = smart_close,
};

ReadStrategyImpl read_strategy_smart_noreverse = {
    .name = "smart_noreverse",
    .init = smart_init,
    .get_task = smart_get_task,
    .use_results = smart_update_zones,
    .close = smart_close,
};

ReadStrategyImpl read_strategy_skipfail = {
    .name = "skipfail",
    .init = skipfail_init,
    .get_task = skipfail_get_task,
    .use_results = skipfail_update_zones,
    .close = skipfail_close,
};

ReadStrategyImpl read_strategy_skipfail_noreverse = {
    .name = "skipfail_noreverse",
    .init = skipfail_init,
    .get_task = skipfail_get_task,
    .use_results = skipfail_update_zones,
    .close = skipfail_close,
};
