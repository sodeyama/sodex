#ifndef _SCSI_H
#define _SCSI_H

#include <sodex/const.h>
#include <sys/types.h>
#include <mstorage.h>

#define SCSI_BLOCK_SIZE 512

#define SCSI_CMD_SIZE 10

#define SCSI_OP_READ  0x28
#define SCSI_OP_WRITE 0x2A

typedef struct _SCSI_CMD {
  u_int8_t op_code;
  u_int8_t option;
  u_int8_t lba[4];
  u_int8_t reserve;
  u_int8_t length[2];
  u_int8_t control;
} SCSI_CMD;

PUBLIC void scsi_init();
PUBLIC int scsi_read(u_int32_t lba, u_int32_t sects, void *buf);
PUBLIC int scsi_write(u_int32_t lba, u_int32_t sects, void *buf);

#endif /* _SCSI_H */
