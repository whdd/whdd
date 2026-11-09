#ifndef ATA_H
#define ATA_H

#include <linux/hdreg.h>
#include <inttypes.h>

typedef struct ata_command {
    ide_task_request_t task;
    // This struct is not suitable for HDIO_ commands which involve data transfer to userspace.
    // Because you need buffer right below `task`.
} AtaCommand;

int ata_identify_has_lba48(const uint8_t identify[512]);
uint64_t ata_identify_sector_count(const uint8_t identify[512]);

void prepare_ata_command(AtaCommand *cmd_buffer, int cmd, uint64_t lba, int size_in_sectors);

#endif  // ATA_H
