#include <inttypes.h>
#include "vis.h"

vis_t bs_vis[]   = {
                    { 3000,   L'\u2591', 0, MY_COLOR_GRAY }, // gray light shade
                    { 10000,  L'\u2593', 0, MY_COLOR_GRAY }, // gray medium shade
                    { 50000,  L'\u2588', 0, MY_COLOR_GRAY }, // gray dark shade
                    { 150000, L'\u2588', 0, MY_COLOR_GREEN }, // green full block
                    { 500000, L'\u2588', 0, MY_COLOR_RED }, // red full block
};
vis_t exceed_vis =  { 0,      L'\u2588', 1, MY_COLOR_RED }; // bold red full block
vis_t error_vis  =  { 0,      L'x',      1, MY_COLOR_RED };

void init_my_colors(void) {
    init_pair(MY_COLOR_GRAY, COLOR_WHITE, COLOR_BLACK);
    init_pair(MY_COLOR_GREEN, COLOR_GREEN, COLOR_BLACK);
    init_pair(MY_COLOR_RED, COLOR_RED, COLOR_BLACK);
    init_pair(MY_COLOR_WHITE_ON_BLUE, COLOR_WHITE, COLOR_BLUE);
}

vis_t choose_vis(uint64_t access_time) {
    unsigned int i;
    for (i = 0; i < sizeof(bs_vis)/sizeof(*bs_vis); i++)
        if (access_time < bs_vis[i].access_time)
            return bs_vis[i];
    return exceed_vis;
}


void print_vis(WINDOW *win, vis_t vis) {
    if (vis.attrs)
        wattron(win, A_BOLD);
    else
        wattroff(win, A_BOLD);
    wattron(win, COLOR_PAIR(vis.color_pair));
    wprintw(win, "%lc", vis.vis);
}

void show_legend(WINDOW *win) {
    unsigned int i;
    for (i = 0; i < sizeof(bs_vis)/sizeof(*bs_vis); i++) {
        print_vis(win, bs_vis[i]);
        wattrset(win, A_NORMAL);
        wprintw(win, " <%"PRIu64"ms\n", bs_vis[i].access_time / 1000);
    }
    print_vis(win,exceed_vis);
    wattrset(win, A_NORMAL);
    wprintw(win, " >500ms\n");
    print_vis(win, error_vis);
    wattrset(win, A_NORMAL);
    wprintw(win, " access error\n");
    wrefresh(win);
}

