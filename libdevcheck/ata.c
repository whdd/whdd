#include <endian.h>
#include <string.h>

#include "ata.h"

void prepare_ata_command(AtaCommand *cmd_buffer, int cmd, uint64_t lba, int size_in_sectors) {
    // TODO Optimize lba splitting to bytes
    uint64_t lba_be = htobe64(lba);
    uint8_t *lba_be_bytes = (uint8_t*)&lba_be;
    task_struct_t *io_ports = (task_struct_t*)&cmd_buffer->task.io_ports;
    hob_struct_t *hob_ports = (hob_struct_t*)&cmd_buffer->task.hob_ports;
    memset(&cmd_buffer->task, 0, sizeof(cmd_buffer->task));

    io_ports->command = cmd;
    io_ports->sector_count   = size_in_sectors & 0x00ff;
    hob_ports->sector_count  = (size_in_sectors >> 8) & 0x00ff;
    io_ports->sector_number  = lba_be_bytes[7];  // LBA (7:0)
    io_ports->low_cylinder   = lba_be_bytes[6];  // LBA (15:8)
    io_ports->high_cylinder  = lba_be_bytes[5];  // LBA (23:16)
    hob_ports->sector_number = lba_be_bytes[4];  // LBA (31:24)
    hob_ports->low_cylinder  = lba_be_bytes[3];  // LBA (39:32)
    hob_ports->high_cylinder = lba_be_bytes[2];  // LBA (47:40)
    io_ports->device_head   |= 0x40;  // LBA flag; DEV flag is set correctly in kernel
    cmd_buffer->task.out_flags.all = 0xffff;
    cmd_buffer->task.data_phase = TASKFILE_NO_DATA;
    cmd_buffer->task.req_cmd = IDE_DRIVE_TASK_NO_DATA;
}
