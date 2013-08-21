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

#define ERROR_BIT_AMNF ((uint8_t)(1 << 0))
#define ERROR_BIT_NM   ((uint8_t)(1 << 1))
#define ERROR_BIT_ABRT ((uint8_t)(1 << 2))
#define ERROR_BIT_MCR  ((uint8_t)(1 << 3))
#define ERROR_BIT_IDNF ((uint8_t)(1 << 4))
#define ERROR_BIT_MC   ((uint8_t)(1 << 5))
#define ERROR_BIT_UNC  ((uint8_t)(1 << 6))
#define ERROR_BIT_NA1  ((uint8_t)(1 << 7))

#define STATUS_BIT_ERR  ((uint8_t)(1 << 0))
#define STATUS_BIT_NA3  ((uint8_t)(1 << 1))
#define STATUS_BIT_NA2  ((uint8_t)(1 << 2))
#define STATUS_BIT_DRQ  ((uint8_t)(1 << 3))
#define STATUS_BIT_NA1  ((uint8_t)(1 << 4))
#define STATUS_BIT_DF   ((uint8_t)(1 << 5))
#define STATUS_BIT_DRDY ((uint8_t)(1 << 6))
#define STATUS_BIT_BSY  ((uint8_t)(1 << 7))

typedef struct scsi_ata_return_descriptor {
    uint8_t descriptor[14];
    uint8_t error;
    uint8_t status;
    uint64_t lba;
} ScsiAtaReturnDescriptor;

void prepare_scsi_command_from_ata(ScsiCommand *scsi_cmd, AtaCommand *ata_cmd);

void fill_scsi_ata_return_descriptor(ScsiAtaReturnDescriptor *scsi_ata_ret, ScsiCommand *scsi_cmd);

int get_sense_key_from_sense_buffer(uint8_t *buf);

DC_BlockStatus scsi_ata_check_return_status(ScsiCommand *scsi_command);

#endif  // SCSI_H
