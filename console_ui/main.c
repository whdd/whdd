#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "libdevcheck.h"

DC_Ctx *dc_ctx;

static int readtest_cb(DC_Dev *dev, void *priv, DC_TestReport *report);
static int zerofilltest_cb(DC_Dev *dev, void *priv, DC_TestReport *report);

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

    while (1) {
    // print actions list
    printf("\nChoose action #:\n"
            "0) Exit\n"
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
    if (chosen_action_ind == 0) {
        printf("Exiting due to chosen action\n");
        return 0;
    }

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
        dc_dev_readtest(chosen_dev, readtest_cb, NULL);
        break;
    case 3:
        printf("This will destroy all data on device %s (%s). Are you sure? (y/n)\n",
                chosen_dev->dev_fs_name, chosen_dev->model_str);
        char ans = 'n';
        r = scanf("\n%c", &ans);
        if (ans != 'y')
            break;
        dc_dev_zerofilltest(chosen_dev, zerofilltest_cb, NULL);
        break;
    default:
        printf("Wrong action index\n");
        break;
    }
    } // while(1)

    return 0;
}

static int readtest_cb(DC_Dev *dev, void *priv, DC_TestReport *report) {
    if (report->blk_index == 0) {
        printf("Performing read-test of '%s' with block size of %"PRIu64" bytes\n",
                dev->dev_fs_name, report->blk_size);
    }
    printf("Block #%"PRIu64" (total %"PRIu64") read in %"PRIu64" mcs. Errno %d\n",
            report->blk_index, report->blks_total, report->blk_access_time,
            report->blk_access_errno);
    fflush(stdout);
    return 0;
}

static int zerofilltest_cb(DC_Dev *dev, void *priv, DC_TestReport *report) {
    if (report->blk_index == 0) {
        printf("Performing 'write zeros' test of '%s' with block size of %"PRIu64" bytes\n",
                dev->dev_fs_name, report->blk_size);
    }
    printf("Block #%"PRIu64" (total %"PRIu64") written in %"PRIu64" mcs. Errno %d\n",
            report->blk_index, report->blks_total, report->blk_access_time,
            report->blk_access_errno);
    fflush(stdout);
    return 0;
}

