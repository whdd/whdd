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
#include "utils.h"

struct hpa_set_priv {
    int64_t max_lba;
};
typedef struct hpa_set_priv HpaSetPriv;

static int SuggestDefaultValue(DC_Dev *dev, DC_OptionSetting *setting) {
    if (!strcmp(setting->name, "max_lba")) {
        int64_t native_max_lba = dev->native_capacity / 512 - 1;  // TODO Request via ATA
        char *string;
        int r = asprintf(&string, "%"PRId64, native_max_lba);
        assert(r != -1);
        setting->value = string;
    } else {
        return 1;
    }
    return 0;
}

static int Open(DC_ProcedureCtx *ctx) {
    HpaSetPriv *priv = ctx->priv;

    dc_dev_set_max_lba(ctx->dev->dev_path, ctx->dev->native_capacity / 512 - 1);
    int ret = dc_dev_set_max_lba(ctx->dev->dev_path, priv->max_lba);
    if (ret)
        dc_log(DC_LOG_ERROR, "Command SET MAX ADDRESS EXT failed");
    return 0;
}

static void Close(DC_ProcedureCtx *ctx) {
    (void)ctx;
}

static DC_ProcedureOption options[] = {
    { "max_lba", "set maximum reachable LBA", offsetof(HpaSetPriv, max_lba), DC_ProcedureOptionType_eInt64 },
    { NULL }
};

DC_Procedure hpa_set = {
    .name = "hpa_set",
    .display_name = "Setup Hidden Protected Area",
    .help = "Sets maximum reachable LBA",
    .flags = DC_PROC_FLAG_INVASIVE,
    .suggest_default_value = SuggestDefaultValue,
    .open = Open,
    .close = Close,
    .priv_data_size = sizeof(HpaSetPriv),
    .options = options,
};

