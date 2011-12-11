#ifndef LIBDEVCHECK_UTILS_H
#define LIBDEVCHECK_UTILS_H

/**
 * Execute bash command thru popen()
 * and store full output to dynamic buffer that must be free()d
 */
char *cmd_output(char *command_line);

#endif // LIBDEVCHECK_UTILS_H
