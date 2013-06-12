#ifndef UI_MUTUAL_H
#define UI_MUTUAL_H

#include "objects_def.h"
#include "device.h"

typedef enum {
    CliAction_eInvalid = -1,
    CliAction_eExit = 0,
    CliAction_eShowSmart = 1,
    CliAction_eSetHpa = 2,
    CliAction_eProcRead = 3,
    CliAction_eProcWriteZeros = 4,
    CliAction_eProcVerify = 5,
    CliAction_eProcCopy = 6,
    CliAction_eMaxValidIndex = 6,
} CliAction;

static struct action {
    CliAction menu_number;
    char name[50];
} actions[] = {
    { CliAction_eExit,           "Exit" },
    { CliAction_eShowSmart,      "Show SMART attributes" },
    { CliAction_eSetHpa,         "Setup Hidden Protected Area" },
    { CliAction_eProcRead,       "posix_read" },
    { CliAction_eProcWriteZeros, "posix_write_zeros" },
    { CliAction_eProcVerify,     "ata_verify_ext" },
    { CliAction_eProcCopy,       "copy" },
    { CliAction_eInvalid,        "" }
};

static const int n_actions = CliAction_eMaxValidIndex + 1;

void ui_dev_descr_format(char *buf, int bufsize, DC_Dev *dev);

#endif  // UI_MUTUAL_H
