#ifndef LIBDEVCHECK_UTILS_H
#define LIBDEVCHECK_UTILS_H

#include <pthread.h>
#include <inttypes.h>

#include "procedure.h"

/**
 * Execute bash command thru popen()
 * and store full output to dynamic buffer that must be free()d
 */
char *cmd_output(char *command_line);

/*
 * Enable realtime scheduling for calling thread
 *
 * @param prio: 0 for min, 1 for max
 * @return 0 on success
 */
int dc_realtime_scheduling_enable_with_prio(int prio);

char *dc_dev_smartctl_text(char *dev_fs_path, char *options);

char *commaprint(uint64_t n, char *retbuf, size_t bufsize);

int procedure_perform_until_interrupt(DC_ProcedureCtx *actctx,
        ProcedureDetachedLoopCB callback, void *callback_priv);

int64_t dc_dev_get_native_capacity(char *dev_fs_path);

// Difference is unit of capacity
void dc_dev_set_max_capacity(char *dev_fs_path, uint64_t capacity);
void dc_dev_set_max_lba(char *dev_fs_path, uint64_t lba);

#endif // LIBDEVCHECK_UTILS_H
