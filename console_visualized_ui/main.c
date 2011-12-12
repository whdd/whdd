#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "libdevcheck.h"

DC_Ctx *dc_ctx;

static int readtest_cb(DC_Dev *dev, void *priv, DC_TestReport *report);
static int zerofilltest_cb(DC_Dev *dev, void *priv, DC_TestReport *report);
static void show_legend(void);

int main() {
    int r;
    // init libdevcheck
    dc_ctx = dc_init();
    assert(dc_ctx);
    // get list of devices
    DC_DevList *devlist = dc_dev_list(dc_ctx);
    assert(devlist);
    // show list of devices
    int devs_num = dc_dev_list_size(devlist);
    if (devs_num == 0) { printf("No devices found\n"); return 0; }

    int i;
    for (i = 0; i < devs_num; i++) {
        DC_Dev *dev = dc_dev_list_get_entry(devlist, i);
        printf(
                "#%d:" // index
                " %s" // /dev/name
                " %s" // model name
                // TODO human-readable size
                " %"PRIu64" bytes" // size
                "\n"
                ,i
                ,dev->dev_fs_name
                ,dev->model_str
                ,dev->capacity
              );
    }
    printf("Choose device by #:\n");
    int chosen_dev_ind;
    r = scanf("%d", &chosen_dev_ind);
    if (r != 1) {
        printf("Wrong input for device index\n");
        return 1;
    }
    DC_Dev *chosen_dev = dc_dev_list_get_entry(devlist, chosen_dev_ind);
    if (!chosen_dev) {
        printf("No device with index %d\n", chosen_dev_ind);
        return 1;
    }

    // print actions list
    printf("\nChoose action #:\n"
            "1) Show SMART attributes\n"
            "2) Perform read test\n"
            "3) Perform 'write zeros' test\n"
          );
    int chosen_action_ind;
    r = scanf("%d", &chosen_action_ind);
    if (r != 1) {
        printf("Wrong input for action index\n");
        return 1;
    }
    switch (chosen_action_ind) {
    case 1:
        ;
        char *text;
        text = dc_dev_smartctl_text(chosen_dev, "-A -i");
        if (text)
            printf("%s\n", text);
        free(text);
        break;
    case 2:
        show_legend();
        dc_dev_readtest(chosen_dev, readtest_cb, NULL);
        break;
    case 3:
        show_legend();
        dc_dev_zerofilltest(chosen_dev, zerofilltest_cb, NULL);
        break;
    default:
        printf("Wrong action index\n");
        break;
    }

    return 0;
}

struct block_speed_vis {
    uint64_t access_time; // in mcs
    char vis; // visual representation
};

struct block_speed_vis bs_vis[] = {
    { 1000, '`' },
    { 2000, '.' },
    { 5000, ':' },
    { 10000, '=' },
};
char exceed_vis = '#';
char error_vis = '!';

char choose_vis(uint64_t access_time) {
    int i;
    for (i = 0; i < sizeof(bs_vis)/sizeof(*bs_vis); i++)
        if (access_time <= bs_vis[i].access_time)
            return bs_vis[i].vis;
    return exceed_vis;
}

static void show_legend(void) {
    int i;
    for (i = 0; i < sizeof(bs_vis)/sizeof(*bs_vis); i++)
        printf(" -- %c -- access time <= %"PRIu64" microseconds\n", bs_vis[i].vis, bs_vis[i].access_time);
    printf(" -- %c -- access time exceeds any of above\n", exceed_vis);
    printf(" -- %c -- access error\n", error_vis);
}

static int readtest_cb(DC_Dev *dev, void *priv, DC_TestReport *report) {
    if (report->blk_index == 0) {
        printf("Performing read-test of '%s' with block size of %"PRIu64" bytes\n",
                dev->dev_fs_name, report->blk_size);
    }
    if (report->blk_access_errno)
        putchar(error_vis);
    else
        putchar(choose_vis(report->blk_access_time));
    fflush(stdout);
    return 0;
}

static int zerofilltest_cb(DC_Dev *dev, void *priv, DC_TestReport *report) {
    if (report->blk_index == 0) {
        printf("Performing 'write zeros' test of '%s' with block size of %"PRIu64" bytes\n",
                dev->dev_fs_name, report->blk_size);
    }
    if (report->blk_access_errno)
        putchar(error_vis);
    else
        putchar(choose_vis(report->blk_access_time));
    fflush(stdout);
    return 0;
}

