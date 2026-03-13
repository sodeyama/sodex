/*
 *  @File        ata.c
 *  @Brief       ATA PIO mode disk driver for QEMU IDE
 *
 *  @Author      Sodex
 *  @License     BSD License
 */

#include <sodex/const.h>
#include <sys/types.h>
#include <io.h>
#include <vga.h>

/* ATA I/O ports (primary channel) */
#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECT_COUNT 0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE_HEAD 0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_DEV_CTRL   0x3F6

/* Status bits */
#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

/* Commands */
#define ATA_CMD_READ_PIO  0x20
#define ATA_CMD_WRITE_PIO 0x30

PRIVATE void ata_wait_bsy()
{
  while (in8(ATA_STATUS) & ATA_SR_BSY)
    ;
}

PRIVATE void ata_wait_drq()
{
  while (!(in8(ATA_STATUS) & ATA_SR_DRQ))
    ;
}

PUBLIC int ata_read(u_int32_t lba, u_int32_t sects, void *buf)
{
  u_int16_t *ptr = (u_int16_t *)buf;
  u_int32_t i, j;

  for (i = 0; i < sects; i++) {
    u_int32_t cur_lba = lba + i;

    ata_wait_bsy();
    out8(ATA_DRIVE_HEAD, 0xE0 | ((cur_lba >> 24) & 0x0F));
    out8(ATA_SECT_COUNT, 1);
    out8(ATA_LBA_LO, cur_lba & 0xFF);
    out8(ATA_LBA_MID, (cur_lba >> 8) & 0xFF);
    out8(ATA_LBA_HI, (cur_lba >> 16) & 0xFF);
    out8(ATA_COMMAND, ATA_CMD_READ_PIO);

    ata_wait_bsy();

    u_int8_t status = in8(ATA_STATUS);
    if (status & ATA_SR_ERR) {
      u_int8_t err = in8(ATA_ERROR);
      _kprintf(" ATA: read error lba=%x status=%x err=%x\n", cur_lba, status, err);
      return -1;
    }
    if (!(status & ATA_SR_DRQ)) {
      _kprintf(" ATA: no DRQ lba=%x status=%x\n", cur_lba, status);
      return -1;
    }

    for (j = 0; j < 256; j++) {
      *ptr++ = in16(ATA_DATA);
    }
  }

  return sects * 512;
}

PUBLIC int ata_write(u_int32_t lba, u_int32_t sects, void *buf)
{
  u_int16_t *ptr = (u_int16_t *)buf;
  u_int32_t i, j;

  for (i = 0; i < sects; i++) {
    u_int32_t cur_lba = lba + i;

    ata_wait_bsy();
    out8(ATA_DRIVE_HEAD, 0xE0 | ((cur_lba >> 24) & 0x0F));
    out8(ATA_SECT_COUNT, 1);
    out8(ATA_LBA_LO, cur_lba & 0xFF);
    out8(ATA_LBA_MID, (cur_lba >> 8) & 0xFF);
    out8(ATA_LBA_HI, (cur_lba >> 16) & 0xFF);
    out8(ATA_COMMAND, ATA_CMD_WRITE_PIO);

    ata_wait_bsy();
    ata_wait_drq();

    for (j = 0; j < 256; j++) {
      out16(ATA_DATA, *ptr++);
    }
  }

  return sects * 512;
}

PUBLIC void ata_init()
{
  /* Disable ATA interrupts (nIEN bit) for PIO polling mode */
  out8(ATA_DEV_CTRL, 0x02);

  /* Select drive 0 and check if device exists */
  out8(ATA_DRIVE_HEAD, 0xE0);
  u_int8_t status = in8(ATA_STATUS);
  _kprintf(" ATA: status=%x\n", status);

  if (status == 0xFF || status == 0x00) {
    _kputs(" ATA: No drive detected on primary channel\n");
  } else {
    _kputs(" ATA: Initializing IDE controller\n");
  }
}
