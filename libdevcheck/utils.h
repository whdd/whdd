#ifndef LIBDEVCHECK_UTILS_H
#define LIBDEVCHECK_UTILS_H

#include <pthread.h>

/**
 * Execute bash command thru popen()
 * and store full output to dynamic buffer that must be free()d
 */
char *cmd_output(char *command_line);

/**
 * Set thread scheduling priority to highest level,
 * applying realtime scheduling policy SCHED_FIFO
 */
void dc_raise_thread_prio(void);

#endif // LIBDEVCHECK_UTILS_H
