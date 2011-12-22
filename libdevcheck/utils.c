#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

char *cmd_output(char *command_line) {
    int r;
    FILE *pipe;
    pipe = popen(command_line, "r");
    if (!pipe) {
        fprintf(stderr, "Failed to exec '%s'\n", command_line);
        return NULL;
    }

    char *all = NULL;
    char *new_all;
    int all_len = 0;
    char buf[1024];
    while (!feof(pipe)) {
        r = fread(buf, 1, sizeof(buf), pipe);
        // append to buffer
        if (r == 0)
            continue;

        new_all = realloc(all, all_len + r);
        if (!new_all)
            goto fail_buf_form;
        all = new_all;

        memcpy(all + all_len, buf, r);
        all_len += r;
    }

    if (all_len == 0) {
        free(all);
        return NULL;
    }

    // terminate with zero byte
    new_all = realloc(all, all_len + 1);
    if (!new_all)
        goto fail_buf_form;
    all = new_all;
    all[all_len] = '\0';

    return all;

fail_buf_form:
    free(all);
    return NULL;
}

