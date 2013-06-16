#include <stdlib.h>

#include "procedure.h"
#include "utils.h"

struct smart_show_priv {
};
typedef struct smart_show_priv SmartShowPriv;

static int Open(DC_ProcedureCtx *ctx) {
    char *text = dc_dev_smartctl_text(ctx->dev->dev_path, " -i -s on -A ");
    if (text) {
        dc_log(DC_LOG_INFO, "%s", text);
        free(text);
        return 0;
    } else {
        dc_log(DC_LOG_ERROR, "%s", "Getting SMART attributes failed");
        return 1;
    }
}

static void Close(DC_ProcedureCtx *ctx) {
    (void)ctx;
}

DC_Procedure smart_show = {
    .name = "smart_show",
    .long_name = "Show SMART attributes",
    .open = Open,
    .close = Close,
    .priv_data_size = sizeof(SmartShowPriv),
};
