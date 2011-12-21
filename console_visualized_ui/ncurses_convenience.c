#include <assert.h>
#include "ncurses_convenience.h"

static WINDOW *my_centered_win(WINDOW *parent, int rows, int cols) {
    int pmr, pmc;
    getmaxyx(parent, pmr, pmc);
    return derwin(parent, rows, cols, (pmr-rows)/2, (pmc-cols)/2);
}

int menu_helper(ITEM **items, char *title) {
    int ind = -1;
    MENU *menu = new_menu(items);
    assert(menu);

    WINDOW *body = derwin(stdscr, LINES-2, COLS, 1, 0); // leave 1st and last lines untouched
    wrefresh(body);

    int mrows, mcols;
    scale_menu(menu, &mrows, &mcols);
    WINDOW *menuwin = my_centered_win(body, mrows+2, mcols+2);
    set_menu_win(menu, menuwin);
    keypad(menuwin, TRUE);
    box(menuwin, 0, 0);
    waddstr(menuwin, title);
    WINDOW *menusub = derwin(menuwin, mrows, mcols, 1, 1);
    set_menu_sub(menu, menusub);

    post_menu(menu);
    wrefresh(menuwin);

    while (1) {
        int ch = wgetch(menuwin);
        if (ch == '\n' || ch == KEY_EXIT)
            break;
        if (ch == KEY_UP)
            menu_driver(menu, REQ_PREV_ITEM);
        if (ch == KEY_DOWN)
            menu_driver(menu, REQ_NEXT_ITEM);
    }
    unpost_menu(menu);
    ITEM *cur = current_item(menu);
    free_menu(menu);

    unsigned int i = 0;
    while (items[i] && items[i] != cur)
        i++;
    assert(items[i] == cur);
    ind = i;

    delwin(menusub);
    delwin(menuwin);
    wclear(body);
    wrefresh(body);
    delwin(body);

    return ind;
}

