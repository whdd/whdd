#ifndef VIS_H
#define VIS_H

#include <stdint.h>
#include <wchar.h>
#include <curses.h>

// start numbers from 40 because libdialog uses up to 40 items starting from 1
#define MY_COLOR_GRAY 41
#define MY_COLOR_GREEN 42
#define MY_COLOR_RED 43
#define MY_COLOR_WHITE_ON_BLUE 44

#define LEGEND_WIDTH 20

typedef struct vis_t {
    uint64_t access_time; // in mcs
    wchar_t vis; // visual representation
    int attrs;
    int color_pair;
} vis_t;

extern vis_t bs_vis[];
extern vis_t exceed_vis;
extern vis_t error_vis[]; // 0th is unused, rest go as in enum

void init_my_colors(void);
vis_t choose_vis(uint64_t access_time);
void print_vis(WINDOW *win, vis_t vis);
void show_legend(WINDOW *win);

#endif // VIS_H
