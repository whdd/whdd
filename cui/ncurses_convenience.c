#include <assert.h>
#include "ncurses_convenience.h"
#include "vis.h"
#include "libdevcheck.h"

void clear_body(void) {
    WINDOW *body = derwin(stdscr, LINES-2, COLS, 1, 0); // leave 1st and last lines untouched
    wclear(body);
    wrefresh(body);
    delwin(body);

    WINDOW *footer = subwin(stdscr, 1, COLS, LINES-1, 0);
    wbkgd(footer, COLOR_PAIR(MY_COLOR_WHITE_ON_BLUE));
    wprintw(footer, " WHDD rev. " WHDD_VERSION);
    wrefresh(footer);
    delwin(footer);
}

