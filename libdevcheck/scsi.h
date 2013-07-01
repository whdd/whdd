#ifndef SCSI_H
#define SCSI_H

#include <unistd.h>
#include <scsi/sg.h>

#include "ata.h"
#include "procedure.h"

typedef struct scsi_command {
    sg_io_hdr_t io_hdr;  // Helper struct used with ioctl(SG_IO), has some output members
    uint8_t scsi_cmd[16];  // Command buffer
    uint8_t sense_buf[32];  // Output diagnostic info from device
} ScsiCommand;

typedef struct scsi_ata_return_descriptor {
    uint8_t descriptor[14];
    union {
        uint8_t value;
        struct {
            unsigned amnf: 1;
            unsigned nm:   1;
            unsigned abrt: 1;
            unsigned mcr:  1;
            unsigned idnf: 1;
            unsigned mc:   1;
            unsigned unc:  1;
            unsigned na1:  1;
        } bits;
    } error;
    union {
        uint8_t value;
        struct {
            unsigned err:  1;
            unsigned na3:  1;
            unsigned na2:  1;
            unsigned drq:  1;
            unsigned na1:  1;
            unsigned df:   1;
            unsigned drdy: 1;
            unsigned bsy:  1;
        } bits;
    } status;
    uint64_t lba;
} ScsiAtaReturnDescriptor;

void prepare_scsi_command_from_ata(ScsiCommand *scsi_cmd, AtaCommand *ata_cmd);

void fill_scsi_ata_return_descriptor(ScsiAtaReturnDescriptor *scsi_ata_ret, ScsiCommand *scsi_cmd);

int get_sense_key_from_sense_buffer(uint8_t *buf);

DC_BlockStatus scsi_ata_check_return_status(ScsiCommand *scsi_command);

#endif  // SCSI_H
