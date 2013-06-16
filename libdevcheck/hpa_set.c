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

static int Open(DC_ProcedureCtx *ctx) {
    HpaSetPriv *priv = ctx->priv;
    dc_dev_set_max_lba(ctx->dev->dev_path, priv->max_lba);  // TODO return?
    return 0;
}

static void Close(DC_ProcedureCtx *ctx) {
    (void)ctx;
}

static DC_ProcedureOption options[] = {
    { "max_lba", "set maximum reachable LBA", offsetof(HpaSetPriv, max_lba), DC_ProcedureOptionType_eInt64, { .i64 = 0 } },
    { NULL }
};

DC_Procedure hpa_set = {
    .name = "hpa_set",
    .long_name = "Set maximum reachable LBA",
    .flags = DC_PROC_FLAG_INVASIVE,
    .open = Open,
    .close = Close,
    .priv_data_size = sizeof(HpaSetPriv),
    .options = options,
};

