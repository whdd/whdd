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

void prepare_scsi_command_from_ata(ScsiCommand *scsi_cmd, AtaCommand *ata_cmd);

#endif  // SCSI_H
