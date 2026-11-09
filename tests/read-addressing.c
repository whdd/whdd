/* Regression tests for legacy ATA geometry and both Read test backends. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libdevcheck/ata.h"
#include "libdevcheck/procedure.h"
#include "libdevcheck/scsi.h"
#include "libdevcheck/utils.h"

extern DC_Procedure read_test;

static uint8_t identify[512], last_cdb[16];
static uint64_t kernel_bytes;
static int kernel_available, fail_readahead, open_error;
static int open_calls, identify_calls, read_calls, readahead_sets;
static char last_log[256];

static void put_le(unsigned offset, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; i++)
        identify[offset + i] = value >> (8 * i);
}

static void reset(void) {
    memset(identify, 0, sizeof(identify));
    memset(last_cdb, 0, sizeof(last_cdb));
    put_le(166, 0x7b09, 2); /* Valid command sets, without LBA48. */
    put_le(120, 80293248, 4);
    kernel_bytes = UINT64_C(41110142976);
    kernel_available = 1;
    fail_readahead = open_error = 0;
    open_calls = identify_calls = read_calls = readahead_sets = 0;
    last_log[0] = 0;
}

int __wrap_open(const char *path, int flags, ...) {
    (void)path;
    (void)flags;
    open_calls++;
    if (open_error) {
        errno = open_error;
        return -1;
    }
    return 7;
}

int __wrap_open64(const char *path, int flags, ...) {
    return __wrap_open(path, flags);
}

int __wrap_close(int fd) { assert(fd == 7); return 0; }
off_t __wrap_lseek(int fd, off_t offset, int whence) {
    assert(fd == 7 && whence == SEEK_SET);
    return offset;
}
int64_t __wrap_lseek64(int fd, int64_t offset, int whence) {
    assert(fd == 7 && whence == SEEK_SET);
    return offset;
}
ssize_t __wrap_read(int fd, void *buffer, size_t bytes) {
    assert(fd == 7);
    memset(buffer, 0, bytes);
    read_calls++;
    return bytes;
}

int __wrap_ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    void *arg = NULL;
    assert(fd == 7);
    if (request == BLKGETSIZE64 || request == BLKRAGET || request == SG_IO) {
        va_start(ap, request);
        arg = va_arg(ap, void *);
        va_end(ap);
    }
    if (request == BLKGETSIZE64) {
        if (!kernel_available) { errno = ENOTTY; return -1; }
        *(uint64_t *)arg = kernel_bytes;
    } else if (request == BLKRAGET) {
        if (fail_readahead) { errno = ENOTTY; return -1; }
        *(long *)arg = 128;
    } else if (request == BLKRASET) {
        readahead_sets++;
    } else if (request == SG_IO) {
        sg_io_hdr_t *hdr = arg;
        if (hdr->cmdp[14] == WIN_IDENTIFY) {
            memcpy(hdr->dxferp, identify, sizeof(identify));
            hdr->sbp[0] = 0x72;
            identify_calls++;
        } else {
            assert(hdr->cmdp[14] == WIN_VERIFY || hdr->cmdp[14] == WIN_VERIFY_EXT);
            memcpy(last_cdb, hdr->cmdp, sizeof(last_cdb));
        }
        hdr->status = 0;
    } else {
        assert(request == BLKFLSBUF);
    }
    return 0;
}

void dc_log(enum DC_LogLevel level, const char *fmt, ...) {
    va_list ap;
    (void)level;
    va_start(ap, fmt);
    vsnprintf(last_log, sizeof(last_log), fmt, ap);
    va_end(ap);
}
void _dc_proc_time_pre(DC_ProcedureCtx *ctx) { (void)ctx; }
void _dc_proc_time_post(DC_ProcedureCtx *ctx) { ctx->report.blk_access_time = 1; }

static void setup(DC_ProcedureCtx *ctx, DC_Dev *dev, const char *api, int64_t start) {
    memset(ctx, 0, sizeof(*ctx));
    memset(dev, 0, sizeof(*dev));
    memcpy(dev->identify, identify, sizeof(identify));
    dev->capacity = kernel_bytes;
    dev->ata_capable = 1;
    dev->dev_path = "/mock/disk";
    ctx->dev = dev;
    ctx->priv = calloc(1, read_test.priv_data_size);
    assert(ctx->priv);
    for (DC_ProcedureOption *opt = read_test.options; opt->name; opt++) {
        void *value = (char *)ctx->priv + opt->offset;
        if (!strcmp(opt->name, "api")) *(const char **)value = api;
        if (!strcmp(opt->name, "start_lba")) *(int64_t *)value = start;
    }
}

static void test_geometry(void) {
    uint64_t value;
    reset();
    assert(!ata_identify_has_lba48(identify));
    assert(ata_identify_sector_count(identify) == 80293248);
    assert(!dc_dev_get_max_lba("/mock/disk", &value) && value == 80293247);
    identify_calls = 0;
    assert(!dc_dev_get_capacity("/mock/disk", &value));
    assert(value == kernel_bytes && !identify_calls);
    kernel_available = 0;
    assert(!dc_dev_get_capacity("/mock/disk", &value) && value == kernel_bytes);
    put_le(166, 0x4400, 2);
    put_le(200, (UINT64_C(1) << 33) + 7, 8);
    assert(ata_identify_has_lba48(identify));
    assert(!dc_dev_get_capacity("/mock/disk", &value));
    assert(value == ((UINT64_C(1) << 33) + 7) * 512);
    put_le(200, 0, 8);
    assert(ata_identify_sector_count(identify) == 80293248);
    put_le(166, 0xffff, 2);
    put_le(200, UINT64_MAX, 8);
    assert(!ata_identify_has_lba48(identify));
    assert(ata_identify_sector_count(identify) == 80293248);
    memset(identify, 0, sizeof(identify));
    value = 123;
    assert(dc_dev_get_capacity("/mock/disk", &value) && value == 123);
}

static void test_backends(void) {
    DC_ProcedureCtx ctx;
    DC_Dev dev;
    reset();
    setup(&ctx, &dev, "posix", 0);
    assert(!read_test.open(&ctx));
    assert(!read_test.perform(&ctx) && read_calls == 1);
    assert(ctx.report.blk_status == DC_BlockStatus_eOk);
    read_test.close(&ctx);
    assert(readahead_sets == 2);
    free(ctx.priv);

    reset();
    kernel_bytes = (UINT64_C(1) << 28) * 512;
    setup(&ctx, &dev, "ata", 0x0abcdef0);
    assert(!read_test.open(&ctx) && !read_test.perform(&ctx));
    assert(last_cdb[14] == WIN_VERIFY && !(last_cdb[1] & 1));
    assert(last_cdb[13] == 0xea && last_cdb[12] == 0xbc);
    assert(last_cdb[10] == 0xde && last_cdb[8] == 0xf0);
    assert(!last_cdb[3] && !last_cdb[5] && !last_cdb[7]);
    assert(!last_cdb[9] && !last_cdb[11] && !last_cdb[6]);
    read_test.close(&ctx);
    free(ctx.priv);

    reset();
    put_le(166, 0x4400, 2);
    kernel_bytes = (UINT64_C(1) << 34) * 512;
    setup(&ctx, &dev, "ata", (INT64_C(1) << 32) + 10);
    assert(!read_test.open(&ctx) && !read_test.perform(&ctx));
    assert(last_cdb[14] == WIN_VERIFY_EXT && (last_cdb[1] & 1));
    assert(last_cdb[9] == 1 && last_cdb[8] == 10);
    read_test.close(&ctx);
    free(ctx.priv);

    reset();
    setup(&ctx, &dev, "posix", -1);
    assert(read_test.open(&ctx) && !open_calls);
    assert(strstr(last_log, "Invalid read range"));
    free(ctx.priv);
    setup(&ctx, &dev, "posix", 0);
    dev.capacity = 0;
    assert(read_test.open(&ctx) && !open_calls);
    free(ctx.priv);

    reset();
    fail_readahead = 1;
    setup(&ctx, &dev, "posix", 0);
    assert(!read_test.open(&ctx));
    read_test.close(&ctx);
    assert(!readahead_sets);
    free(ctx.priv);
}

int main(void) {
    test_geometry();
    test_backends();
    puts("ATA geometry, kernel capacity, and POSIX/LBA28/LBA48 read tests passed");
    return 0;
}
