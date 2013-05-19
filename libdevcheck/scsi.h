#ifndef SCSI_H
#define SCSI_H

#include <unistd.h>
#include <scsi/sg.h>

#include "ata.h"

typedef struct scsi_command {
    sg_io_hdr_t io_hdr;  // Helper struct used with ioctl(SG_IO), has some output members
    uint8_t scsi_cmd[16];  // Command buffer
    uint8_t sense_buf[32];  // Output diagnostic info from device
} ScsiCommand;

typedef struct scsi_ata_return_descriptor {
    uint8_t descriptor[14];
    uint8_t error;
    uint8_t status;
    uint64_t lba;
} ScsiAtaReturnDescriptor;

void prepare_scsi_command_from_ata(ScsiCommand *scsi_cmd, AtaCommand *ata_cmd);

void fill_scsi_ata_return_descriptor(ScsiAtaReturnDescriptor *scsi_ata_ret, ScsiCommand *scsi_cmd);

#endif  // SCSI_H
