#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <wchar.h>
#include <locale.h>
#include <errno.h>
#include <unistd.h>
#include <curses.h>
#include <dialog.h>
#include "libdevcheck.h"
#include "device.h"
#include "utils.h"
#include "procedure.h"
#include "vis.h"
#include "ncurses_convenience.h"
#include "dialog_convenience.h"
#include "render.h"
#include "ui_mutual.h"

static int global_init(void);
static void global_fini(void);
static int menu_choose_device(DC_DevList *devlist);
static int menu_choose_procedure(DC_Dev *dev);
static void show_smart_attrs(DC_Dev *dev);
static void set_hpa_dialog(DC_Dev *dev);
void log_cb(enum DC_LogLevel level, const char* fmt, va_list vl);

static int ask_option_value(DC_OptionSetting *setting, DC_ProcedureOption *option) {
    char *suggested_value;
    char entered_value[200];
    const char *param_type_str;
    switch (option->type) {
        case DC_ProcedureOptionType_eInt64:
            param_type_str = "numeric";
            asprintf(&suggested_value, "%"PRId64, option->default_val.i64);
            break;
        case DC_ProcedureOptionType_eString:
            param_type_str = "string";
            asprintf(&suggested_value, "%s", option->default_val.str);
            break;
    }
    char prompt[200];
    snprintf(prompt, sizeof(prompt), "Please enter %s parameter: %s (%s)",
            param_type_str, option->name, option->help);

    int r = dialog_inputbox("Input box", prompt, 0, 0, suggested_value, 0);
    if (r != 0) {
        dialog_msgbox("Info", "Action cancelled", 0, 0, 1);
        return 1;
    }
    // Wow, libdialog is awesomely sane and brilliantly documented lib, i fuckin love it
    snprintf(entered_value, sizeof(entered_value), "%s", dialog_vars.input_result);
    if (entered_value[0] == '\0' || entered_value[0] == '\n')
        snprintf(entered_value, sizeof(entered_value), "%s", suggested_value);
    setting->value = strdup(entered_value);
    free(suggested_value);
    return 0;
}

int main() {
    int r;
    dialog_vars.default_button = -1;
    //printf("%d\n", dialog_vars.default_button);
    //return 0;
    r = global_init();
    if (r) {
        fprintf(stderr, "init fail\n");
        return r;
    }
    // get list of devices
    DC_DevList *devlist = dc_dev_list();
    assert(devlist);

    while (1) {
        // draw menu of device choice
        int chosen_dev_ind;
        chosen_dev_ind = menu_choose_device(devlist);
        if (chosen_dev_ind < 0)
            break;

        DC_Dev *chosen_dev = dc_dev_list_get_entry(devlist, chosen_dev_ind);
        if (!chosen_dev) {
            printw("No device with index %d\n", chosen_dev_ind);
            return 1;
        }
        // draw procedures menu
        int chosen_procedure_ind;
        chosen_procedure_ind = menu_choose_procedure(chosen_dev);
        if (chosen_procedure_ind < 0)
            break;

        switch (chosen_procedure_ind) {
        case CliAction_eExit:
            return 0;
        case CliAction_eShowSmart:
            show_smart_attrs(chosen_dev);
            break;
        case CliAction_eSetHpa:
            set_hpa_dialog(chosen_dev);
            break;
        case CliAction_eProcRead:
        case CliAction_eProcWriteZeros:
        case CliAction_eProcVerify:
        case CliAction_eProcCopy:
        {
            char *act_name = actions[chosen_procedure_ind].name;
            DC_Procedure *act = dc_find_procedure(act_name);
            assert(act);
            if (act->flags & DC_PROC_FLAG_DESTRUCTIVE) {
                char *ask;
                r = asprintf(&ask, "This will destroy all data on device %s (%s). Are you sure?",
                        chosen_dev->dev_fs_name, chosen_dev->model_str);
                assert(r != -1);
                r = dialog_yesno("Confirmation", ask, 0, 0);
                // Yes = 0 (FALSE), No = 1, Escape = -1
                free(ask);
                if (/* No */ r)
                    break;
            }
            DC_OptionSetting *option_set = calloc(act->options_num + 1, sizeof(DC_OptionSetting));
            int i;
            r = 0;
            for (i = 0; i < act->options_num; i++) {
                option_set[i].name = act->options[i].name;
                r = ask_option_value(&option_set[i], &act->options[i]);
                if (r)
                    break;
            }
            if (r)
                break;
            DC_ProcedureCtx *actctx;
            r = dc_procedure_open(act, chosen_dev, &actctx, option_set);
            if (r) {
                dialog_msgbox("Error", "Procedure init fail", 0, 0, 1);
                continue;
            }
            render_procedure(actctx);
            break;
        }
        default:
            dialog_msgbox("Error", "Wrong procedure index", 0, 0, 1);
            continue;
        }
    } // while(1)

    return 0;
}

static int global_init(void) {
    int r;
    // TODO check all retcodes
    setlocale(LC_ALL, "");
    initscr();
    init_dialog(stdin, stdout);
    dialog_vars.item_help = 0;

    start_color();
    init_my_colors();
    noecho();
    cbreak();
    scrollok(stdscr, FALSE);
    keypad(stdscr, TRUE);

    WINDOW *footer = subwin(stdscr, 1, COLS, LINES-1, 0);
    wbkgd(footer, COLOR_PAIR(MY_COLOR_WHITE_ON_BLUE));
    wprintw(footer, " WHDD rev. " WHDD_VERSION);

    wrefresh(footer);
    // init libdevcheck
    r = dc_init();
    assert(!r);
    dc_log_set_callback(log_cb);
    r = atexit(global_fini);
    assert(r == 0);
    return 0;
}

static void global_fini(void) {
    clear();
    endwin();
}

static int menu_choose_device(DC_DevList *devlist) {
    int devs_num = dc_dev_list_size(devlist);
    if (devs_num == 0) {
        dialog_msgbox("Info", "No devices found", 0, 0, 1);
        return -1;
    }

    char **items = calloc( 2 * devs_num, sizeof(char*));
    assert(items);

    int i;
    for (i = 0; i < devs_num; i++) {
        DC_Dev *dev = dc_dev_list_get_entry(devlist, i);
        char dev_descr_buf[80];
        ui_dev_descr_format(dev_descr_buf, sizeof(dev_descr_buf), dev);
        items[2*i] = strdup(dev->dev_fs_name);
        items[2*i+1] = strdup(dev_descr_buf);
    }

    clear_body();
    int chosen_dev_ind = my_dialog_menu("Choose device", "", 0, 0, devs_num * 3, devs_num, items);
    for (i = 0; i < devs_num; i++) {
        free(items[2*i]);
        free(items[2*i+1]);
    }
    free(items);

    return chosen_dev_ind;
}

static int menu_choose_procedure(DC_Dev *dev) {
    // TODO fix this awful mess
    char *items[n_actions * 2];
    int i;
    for (i = 0; i < n_actions; i++)
        items[2*i+1] = actions[i].name;
    // this fuckin libdialog makes me code crappy
    for (i = 0; i < n_actions; i++)
        items[2*i] = "";

    clear_body();
    int chosen_procedure_ind = my_dialog_menu("Choose procedure", "", 0, 0, 4 * 3, n_actions, items);
    return chosen_procedure_ind;
}

static void show_smart_attrs(DC_Dev *dev) {
    char *text;
    text = dc_dev_smartctl_text(dev->dev_path, " -i -s on -A ");
    dialog_msgbox("SMART Attributes", text ? : "Getting attrs failed", LINES-6, COLS, 1);
    if (text)
        free(text);
    refresh();
}

static void set_hpa_dialog(DC_Dev *dev) {
    if (dev->native_capacity == -1) {
        dialog_msgbox("Well...", "Querying native max LBA failed on this device. Setting max LBA will surely fail.", 0, 0, 1);
        return;
    }
    int r;
    char *ask;
    r = asprintf(&ask, "This can make your data unreachable on device %s (%s). Are you sure?",
            dev->dev_fs_name, dev->model_str);
    assert(r != -1);
    r = dialog_yesno("Confirmation", ask, 0, 0);
    // Yes = 0 (FALSE), No = 1, Escape = -1
    free(ask);
    if (/* No */ r)
        return;
    uint64_t current_max_lba = dev->capacity / 512 - 1;
    uint64_t native_max_lba = dev->native_capacity / 512 - 1;
    char suggested_input[30];
    snprintf(suggested_input, sizeof(suggested_input), "%"PRIu64, native_max_lba);
    char descr[200];
    snprintf(descr, sizeof(descr), "Enter max addressable LBA. Native max LBA is %"PRIu64", current max LBA is %"PRIu64"\nIn other words: reachable_capacity_in_bytes = (max_LBA + 1) * 512", native_max_lba, current_max_lba);
    r = dialog_inputbox("Input box", descr, 0, 0, suggested_input, 0);
    if (r != 0) {
        dialog_msgbox("Info", "Action cancelled", 0, 0, 1);
        return;
    }
    // Wow, libdialog is awesomely sane and brilliantly documented lib, i fuckin love it
    char *input_result = dialog_vars.input_result;
    uint64_t set_max_lba = strtol(input_result, NULL, 10);
    dc_dev_set_max_lba(dev->dev_path, set_max_lba);
    snprintf(descr, sizeof(descr), "Set to %"PRIu64, set_max_lba);
    dialog_msgbox("Info", descr, 0, 0, 1);
}

void log_cb(enum DC_LogLevel level, const char* fmt, va_list vl) {
    char *msg = dc_log_default_form_string(level, fmt, vl);
    assert(msg);
    if (render_ctx_global) {
        wprintw(render_ctx_global->w_log, "%s", msg);
        wrefresh(render_ctx_global->w_log);
    } else {
        dialog_msgbox("Internal message", msg, 0, 0, 1);
    }
    free(msg);
}
