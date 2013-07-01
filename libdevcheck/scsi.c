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
    scsi_ata_ret->error.value = descr[3];
    scsi_ata_ret->status.value = descr[13];
    scsi_ata_ret->lba  = 0;
    scsi_ata_ret->lba |= (uint64_t)descr[7];
    scsi_ata_ret->lba |= (uint64_t)descr[9]  <<  8;
    scsi_ata_ret->lba |= (uint64_t)descr[11] << 16;
    scsi_ata_ret->lba |= (uint64_t)descr[6]  << 24;
    scsi_ata_ret->lba |= (uint64_t)descr[8]  << 32;
    scsi_ata_ret->lba |= (uint64_t)descr[10] << 40;
}

int get_sense_key_from_sense_buffer(uint8_t *buf) {
    switch (buf[0]) {
        case 0x70:
        case 0x71:
            return buf[2] & 0x0f;
        case 0x72:
        case 0x73:
            return buf[1] & 0x0f;
        default:
            return -1;
    }
}

DC_BlockStatus scsi_ata_check_return_status(ScsiCommand *scsi_command) {
    if (scsi_command->io_hdr.status == 0)
        return DC_BlockStatus_eOk;
    if (scsi_command->io_hdr.status != 0x02 /* CHECK_CONDITION */)
        return DC_BlockStatus_eError;
    int sense_key = get_sense_key_from_sense_buffer(scsi_command->sense_buf);
    ScsiAtaReturnDescriptor scsi_ata_return;
#if 0
    fprintf(stderr, "scsi status: %hhu, msg status %hhu, host status %hu, driver status %hu, duration %u, auxinfo %u\n",
            scsi_command->io_hdr.status,
            scsi_command->io_hdr.msg_status,
            scsi_command->io_hdr.host_status,
            scsi_command->io_hdr.driver_status,
            scsi_command->io_hdr.duration,
            scsi_command->io_hdr.info);
    fprintf(stderr, "sense buffer, in hex: ");
    int i;
    for (i = 0; i < sizeof(scsi_command->sense_buf); i++)
        fprintf(stderr, "%02hhx", scsi_command->sense_buf[i]);
    fprintf(stderr, "\n");
    fprintf(stderr, "sense key is %d", sense_key);
#endif

    fill_scsi_ata_return_descriptor(&scsi_ata_return, scsi_command);
    if (scsi_ata_return.status.bits.err) {
        if (scsi_ata_return.error.bits.unc)
            return DC_BlockStatus_eUnc;
        else if (scsi_ata_return.error.bits.idnf)
            return DC_BlockStatus_eIdnf;
        else if (scsi_ata_return.error.bits.abrt)
            return DC_BlockStatus_eAbrt;
        else
            return DC_BlockStatus_eError;
    } else if (scsi_ata_return.status.bits.df) {
        return DC_BlockStatus_eError;
    } else if (sense_key) {
        if (sense_key == 0x0b)
            return DC_BlockStatus_eAbrt;
        else
            return DC_BlockStatus_eError;
    } else if (scsi_command->io_hdr.duration >= scsi_command->io_hdr.timeout) {
        return DC_BlockStatus_eTimeout;
    }

    return DC_BlockStatus_eOk;
}
