#include <assert.h>
#include "ncurses_convenience.h"

void clear_body(void) {
    WINDOW *body = derwin(stdscr, LINES-2, COLS, 1, 0); // leave 1st and last lines untouched
    wclear(body);
    wrefresh(body);
    delwin(body);
}

