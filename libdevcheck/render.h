#ifndef RENDER_H
#define RENDER_H

#include "procedure.h"

struct dc_renderer_ctx {
    void *priv;
    DC_Renderer *renderer;
    DC_ProcedureCtx *procedure_ctx;
};

struct dc_renderer {
    char *name;
    int priv_data_size;
    int (*open)(DC_RendererCtx *renderer_ctx);
    int (*handle_report)(DC_RendererCtx *renderer_ctx);
    void (*close)(DC_RendererCtx *renderer_ctx);

    DC_Renderer *next;
};


int dc_renderer_register(DC_Renderer *renderer);
#define RENDERER_REGISTER(x) { \
        extern DC_Renderer x; \
        dc_renderer_register(&x); }

DC_Renderer *dc_find_renderer(char *name);

int render_procedure(DC_ProcedureCtx *actctx, DC_Renderer *renderer);

#endif // RENDER_H
