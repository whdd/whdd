#ifndef UI_MUTUAL_H
#define UI_MUTUAL_H

typedef enum {
    CliAction_eInvalid = -1,
    CliAction_eExit = 0,
    CliAction_eShowSmart = 1,
    CliAction_eProcRead = 2,
    CliAction_eProcWriteZeros = 3,
    CliAction_eProcVerify = 4,
    CliAction_eMaxValidIndex = 4,
} CliAction;

static struct action {
    CliAction menu_number;
    char name[50];
} actions[] = {
    { CliAction_eExit,           "Exit" },
    { CliAction_eShowSmart,      "Show SMART attributes" },
    { CliAction_eProcRead,       "posix_read" },
    { CliAction_eProcWriteZeros, "posix_write_zeros" },
    { CliAction_eProcVerify,     "sgio_ata_verify_ext" },
    { CliAction_eInvalid,        "" }
};

static int n_actions = CliAction_eMaxValidIndex + 1;

#endif  // UI_MUTUAL_H
