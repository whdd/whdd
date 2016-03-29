#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <time.h>

#include "libdevcheck.h"
#include "procedure.h"
#include "utils.h"

clockid_t DC_BEST_CLOCK;

DC_Ctx *dc_ctx_global = NULL;

int dc_init(void) {
    int r;
    DC_Ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return 1;
    dc_ctx_global = ctx;

    dc_log_set_callback(dc_log_default_func, NULL);
    dc_log_set_level(DC_LOG_INFO);

    struct timespec dummy;
#ifdef CLOCK_MONOTONIC_RAW
    /* determine best available clock */
    DC_BEST_CLOCK = CLOCK_MONOTONIC_RAW;
    r = clock_gettime(DC_BEST_CLOCK, &dummy);
    if (r) {
        dc_log(DC_LOG_WARNING, "CLOCK_MONOTONIC_RAW unavailable, using CLOCK_MONOTONIC\n");
        DC_BEST_CLOCK = CLOCK_MONOTONIC;
    }
#else
    DC_BEST_CLOCK = CLOCK_MONOTONIC;
#endif
    r = clock_gettime(DC_BEST_CLOCK, &dummy);
    if (r) {
        dc_log(DC_LOG_ERROR, "Monotonic POSIX clock unavailable\n");
        return 1;
    }

    dc_realtime_scheduling_enable_with_prio(0);

#define PROCEDURE_REGISTER(x) { \
        extern DC_Procedure x; \
        dc_procedure_register(&x); }
    PROCEDURE_REGISTER(hpa_set);
    PROCEDURE_REGISTER(posix_write_zeros);
    PROCEDURE_REGISTER(copy);
    PROCEDURE_REGISTER(read_test);
    PROCEDURE_REGISTER(smart_show);
#undef PROCEDURE_REGISTER
    return 0;
}

void dc_finish(void) {
    free(dc_ctx_global);
    dc_ctx_global = NULL;
}

static void dev_list_build(DC_DevList *dc_devlist);
static void dev_list_fill_info(DC_DevList *list);

DC_DevList *dc_dev_list(void) {
    DC_DevList *list = calloc(1, sizeof(*list));
    assert(list);
    list->arr = NULL;
    list->arr_size = 0;
    dev_list_build(list);
    dev_list_fill_info(list);
    return list;
}

void dc_dev_list_free(DC_DevList *list) {
    while (list->arr) {
        DC_Dev *next = list->arr->next;
        free(list->arr);
        list->arr = next;
    }
    free(list);
}

int dc_dev_list_size(DC_DevList *list) {
    return list->arr_size;
}

DC_Dev *dc_dev_list_get_entry(DC_DevList *list, int index) {
    DC_Dev *dev = list->arr;
    while (index > 0) {
        if (!dev)
            return NULL;
        dev = dev->next;
        index--;
    }
    return dev;
}


/*
 * try all things in /proc/partitions that look like a full disk
 * Taken from util-linux-2.19.1/fdisk/fdisk.c tryprocpt()
 */
static void dev_list_build(DC_DevList *dc_devlist) {

    int is_whole_disk(const char *name) {
        // taken from util-linux-2.19.1/lib/wholedisk.c
        while (*name)
            name++;
        return !isdigit(name[-1]);
    }

	FILE *procpt;
	char line[128], ptname[128];
	int ma, mi;
	unsigned long long sz;
	int ret;

	procpt = fopen("/proc/partitions", "r");
	if (procpt == NULL) {
		dc_log(DC_LOG_FATAL, "cannot open /proc/partitions\n");
		return;
	}

	while (fgets(line, sizeof(line), procpt)) {
		if (sscanf (line, " %d %d %llu %128[^\n ]",
			    &ma, &mi, &sz, ptname) != 4)
			continue;
		if (is_whole_disk(ptname)) {
            DC_Dev *dc_dev = calloc(1, sizeof(*dc_dev));
            assert(dc_dev);
            dc_dev->dev_fs_name = strdup(ptname);
            assert(dc_dev->dev_fs_name);
            ret = asprintf(&dc_dev->dev_path, "/dev/%s", ptname);
            assert(ret != -1 && dc_dev->dev_path);
            dc_dev->capacity = sz * 1024;
            dc_dev->next = dc_devlist->arr;
            dc_devlist->arr = dc_dev;
            dc_devlist->arr_size++;
		}
	}
	fclose(procpt);
}

static void dev_modelname_fill(DC_Dev *dev);
static void dev_mounted_fill(DC_Dev *dev);

static void dev_list_fill_info(DC_DevList *list) {
    DC_Dev *dev = list->arr;
    while (dev) {
        memset(dev->identify, 0, sizeof(dev->identify));
        dev->ata_capable = !dc_dev_ata_identify(dev->dev_path, dev->identify);
        if (dev->ata_capable) {
            dc_dev_get_capacity(dev->dev_path, &dev->capacity);
            dc_dev_get_native_capacity(dev->dev_path, &dev->native_capacity);
            dev->serial_no = calloc(1, 21);
            dc_ata_ascii_to_c_string(dev->identify + 20, 10, dev->serial_no);
            dev->model_str = calloc(1, 41);
            dc_ata_ascii_to_c_string(dev->identify + 54, 20, dev->model_str);
            // for (int i = 0; i < 512; i++)
            //     fprintf(stderr, "%c", dev->identify[i]);
        }
        if (!dev->model_str)
            dev_modelname_fill(dev);
        dev_mounted_fill(dev);
        dev = dev->next;
    }
}

static void dev_modelname_fill(DC_Dev *dev) {
    // fill model name, if exists
    char *model_file_name;
    int ret;
    ret = asprintf(&model_file_name, "/sys/block/%s/device/model", dev->dev_fs_name);
    assert(ret != -1 && model_file_name);

    FILE *model_file = fopen(model_file_name, "r");
    free(model_file_name);
    if (!model_file)
        return;
    char model[256];
    int r;
    r = fscanf(model_file, "%256[^\n]", model);
    if (r != 1) {
        dc_log(DC_LOG_ERROR, "Outrageous error at scanning model name\n");
        return;
    }
    dev->model_str = strdup(model);
    assert(dev->model_str);
}

static void dev_mounted_fill(DC_Dev *dev) {
    FILE *mtab = fopen("/etc/mtab", "r");
    if (!mtab)
        return;
    char line[200];
    while (fgets(line, sizeof(line), mtab))
        if (!strncmp(line, dev->dev_path, strlen(dev->dev_path))) {
            dev->mounted = 1;
            break;
        }
    fclose(mtab);
}
