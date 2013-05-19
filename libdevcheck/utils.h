#ifndef LIBDEVCHECK_UTILS_H
#define LIBDEVCHECK_UTILS_H

#include <pthread.h>

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

#endif // LIBDEVCHECK_UTILS_H
