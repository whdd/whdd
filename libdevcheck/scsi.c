#include <string.h>
#include <assert.h>

#include "scsi.h"

void prepare_scsi_command_from_ata(ScsiCommand *scsi_cmd, AtaCommand *ata_cmd) {
    memset(scsi_cmd, 0, sizeof(ScsiCommand));
    scsi_cmd->io_hdr.interface_id = 'S';
    scsi_cmd->io_hdr.dxfer_direction = SG_DXFER_NONE;
    scsi_cmd->io_hdr.cmd_len = 16;
    scsi_cmd->io_hdr.mx_sb_len = sizeof(scsi_cmd->sense_buf);
    scsi_cmd->io_hdr.iovec_count = 0;
    scsi_cmd->io_hdr.dxfer_len = 0;
    scsi_cmd->io_hdr.dxferp = NULL;
    scsi_cmd->io_hdr.cmdp = scsi_cmd->scsi_cmd;  // Pointer to command
    scsi_cmd->io_hdr.sbp = scsi_cmd->sense_buf;
    scsi_cmd->io_hdr.timeout = 1000;  // In millisec; MAX_UINT is no timeout
    scsi_cmd->io_hdr.flags = SG_FLAG_DIRECT_IO;
    scsi_cmd->io_hdr.pack_id = 0;  // Unused internally
    scsi_cmd->io_hdr.usr_ptr = 0;  // Unused internally

    scsi_cmd->scsi_cmd[0]  = 0x85;  // ATA PASS-THROUGH 16 bytes
    scsi_cmd->scsi_cmd[1]  = (3 << 1);  // Non-data protocol
    scsi_cmd->scsi_cmd[1] |= 1;  // EXTEND flag
    scsi_cmd->scsi_cmd[2]  = 0x20;  // Check condition, no off-line, no data xfer
    scsi_cmd->scsi_cmd[3]  = ata_cmd->task.hob_ports[1];  // features
    scsi_cmd->scsi_cmd[4]  = ata_cmd->task.io_ports[1];
    scsi_cmd->scsi_cmd[5]  = ata_cmd->task.hob_ports[2];  // sectors count
    scsi_cmd->scsi_cmd[6]  = ata_cmd->task.io_ports[2];
    scsi_cmd->scsi_cmd[7]  = ata_cmd->task.hob_ports[3];  // sector number
    scsi_cmd->scsi_cmd[8]  = ata_cmd->task.io_ports[3];
    scsi_cmd->scsi_cmd[9]  = ata_cmd->task.hob_ports[4];  // low cylinder
    scsi_cmd->scsi_cmd[10] = ata_cmd->task.io_ports[4];
    scsi_cmd->scsi_cmd[11] = ata_cmd->task.hob_ports[5];  // high cylinder
    scsi_cmd->scsi_cmd[12] = ata_cmd->task.io_ports[5];
    scsi_cmd->scsi_cmd[13] = ata_cmd->task.io_ports[6];  // device
    scsi_cmd->scsi_cmd[14] = ata_cmd->task.io_ports[7];  // command
}

void fill_scsi_ata_return_descriptor(ScsiAtaReturnDescriptor *scsi_ata_ret, ScsiCommand *scsi_cmd) {
    uint8_t *descr = &scsi_cmd->sense_buf[8];
    memcpy(scsi_ata_ret->descriptor, descr, sizeof(scsi_ata_ret->descriptor));
    scsi_ata_ret->error = descr[3];
    scsi_ata_ret->status = descr[13];
    scsi_ata_ret->lba  = 0;
    scsi_ata_ret->lba |= (uint64_t)descr[7];
    scsi_ata_ret->lba |= (uint64_t)descr[9]  <<  8;
    scsi_ata_ret->lba |= (uint64_t)descr[11] << 16;
    scsi_ata_ret->lba |= (uint64_t)descr[6]  << 24;
    scsi_ata_ret->lba |= (uint64_t)descr[8]  << 32;
    scsi_ata_ret->lba |= (uint64_t)descr[10] << 40;
}
