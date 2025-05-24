#include <vga.h>
#include <memory.h>
#include <lib.h>
#include <mstorage.h>
#include <scsi.h>
#include <ext3fs.h>

PUBLIC void scsi_init()
{
  rawdev.raw_read = scsi_read;
  rawdev.raw_write = scsi_write;
}

PUBLIC int scsi_read(u_int32_t lba, u_int32_t sects, void *buf)
{
  SCSI_CMD cmd;
  memset(&cmd, 0, sizeof(SCSI_CMD));
  cmd.op_code = SCSI_OP_READ;
  cmd.option = 0;
  cmd.lba[3] = lba & 0xff;
  cmd.lba[2] = (lba>>8) & 0xff;
  cmd.lba[1] = (lba>>16) & 0xff;
  cmd.lba[0] = (lba>>24) & 0xff;
  cmd.length[1] = sects & 0xff;
  cmd.length[0] = (sects>>8) & 0xff;
  cmd.control = 0;
  mst_data_in(&cmd, SCSI_CMD_SIZE, buf, sects * SCSI_BLOCK_SIZE);

  return sects * SCSI_BLOCK_SIZE;
}

PUBLIC int scsi_write(u_int32_t lba, u_int32_t sects, void *buf)
{
  SCSI_CMD cmd;
  memset(&cmd, 0, sizeof(SCSI_CMD));
  cmd.op_code = SCSI_OP_WRITE;
  cmd.option = 0;
  cmd.lba[3] = lba & 0xff;
  cmd.lba[2] = (lba>>8) & 0xff;
  cmd.lba[1] = (lba>>16) & 0xff;
  cmd.lba[0] = (lba>>24) & 0xff;
  cmd.length[1] = sects & 0xff;
  cmd.length[0] = (sects>>8) & 0xff;
  cmd.control = 0;
  mst_data_out(&cmd, SCSI_CMD_SIZE, buf, sects * SCSI_BLOCK_SIZE);  
  
  return sects * SCSI_BLOCK_SIZE;
}
