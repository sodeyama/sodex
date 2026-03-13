#ifndef _ATA_H
#define _ATA_H

#include <sodex/const.h>
#include <sys/types.h>

PUBLIC void ata_init();
PUBLIC int ata_read(u_int32_t lba, u_int32_t sects, void *buf);
PUBLIC int ata_write(u_int32_t lba, u_int32_t sects, void *buf);

#endif /* _ATA_H */
