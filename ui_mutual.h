#ifndef UI_MUTUAL_H
#define UI_MUTUAL_H

#include "objects_def.h"
#include "device.h"

typedef enum {
    CliAction_eInvalid = -1,
    CliAction_eExit = 0,
    CliAction_eShowSmart,
    CliAction_eSetHpa,
    CliAction_eProcRead,
    CliAction_eProcWriteZeros,
    CliAction_eProcCopy,
    CliAction_eProcCopyDamaged,
    CliAction_eAfterMaxValidIndex,
} CliAction;

static struct action {
    CliAction menu_number;
    char name[50];
    char display_name[50];
} actions[] = {
    { CliAction_eExit,           "",                  "Exit" },
    { CliAction_eShowSmart,      "",                  "Show SMART attributes" },
    { CliAction_eSetHpa,         "hpa_set",           "Setup Hidden Protected Area" },
    { CliAction_eProcRead,       "read_test",         "Read test" },
    { CliAction_eProcWriteZeros, "posix_write_zeros", "Write zeros" },
    { CliAction_eProcCopy,       "copy",              "Straight device copying" },
    { CliAction_eProcCopyDamaged,"copy_damaged",      "Smart device copying" },
    { CliAction_eInvalid,        "", "" }
};

static const int n_actions = CliAction_eAfterMaxValidIndex;

void ui_dev_descr_format(char *buf, int bufsize, DC_Dev *dev);

#endif  // UI_MUTUAL_H
