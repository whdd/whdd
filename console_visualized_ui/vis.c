#include <inttypes.h>
#include "vis.h"

vis_t bs_vis[]   = {
                    { 3000,   L'\u2591', 0, MY_COLOR_GRAY }, // gray light shade
                    { 10000,  L'\u2592', 0, MY_COLOR_GRAY }, // gray medium shade
                    { 50000,  L'\u2593', 0, MY_COLOR_GRAY }, // gray dark shade
                    { 150000, L'\u2588', 0, MY_COLOR_GREEN }, // green full block
                    { 500000, L'\u2588', 0, MY_COLOR_RED }, // red full block
};
vis_t exceed_vis =  { 0,      L'\u2588', 1, MY_COLOR_RED }; // bold red full block
vis_t error_vis  =  { 0,      L'!',      1, MY_COLOR_PINK }; // pink exclam sign

void init_my_colors(void) {
    init_pair(MY_COLOR_GRAY, COLOR_WHITE, COLOR_BLACK);
    init_pair(MY_COLOR_GREEN, COLOR_GREEN, COLOR_BLACK);
    init_pair(MY_COLOR_RED, COLOR_RED, COLOR_BLACK);
    init_pair(MY_COLOR_PINK, COLOR_MAGENTA, COLOR_BLACK);
}

vis_t choose_vis(uint64_t access_time) {
    unsigned int i;
    for (i = 0; i < sizeof(bs_vis)/sizeof(*bs_vis); i++)
        if (access_time < bs_vis[i].access_time)
            return bs_vis[i];
    return exceed_vis;
}


void print_vis(vis_t vis) {
    if (vis.attrs)
        attron(A_BOLD);
    else
        attroff(A_BOLD);
    attron(COLOR_PAIR(vis.color_pair));
    printw("%lc", vis.vis);
    refresh();
}

void show_legend(void) {
    printw("Legend:\n");
    unsigned int i;
    for (i = 0; i < sizeof(bs_vis)/sizeof(*bs_vis); i++) {
        print_vis(bs_vis[i]);
        attrset(A_NORMAL);
        printw(" access time < %"PRIu64" ms\n", bs_vis[i].access_time / 1000);
    }
    print_vis(exceed_vis);
    attrset(A_NORMAL);
    printw(" access time exceeds any of above\n");
    print_vis(error_vis);
    attrset(A_NORMAL);
    printw(" access error\n");
    refresh();
}

