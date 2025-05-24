#ifndef _MSTORAGE_H
#define _MSTORAGE_H

#include <sodex/const.h>
#include <sys/types.h>

#define CBW_SIGNATURE 0x43425355
#define CSW_SIGNATURE 0x53425355

#define CBW_SIZE    0x1F
#define CSW_SIZE    0x0D

#define CBW_FLAG_OUT 0x00
#define CBW_FLAG_IN  0x80


typedef struct _CBW {
  u_int32_t signature;
  u_int32_t tag;
  u_int32_t length;
  u_int8_t flags;
  u_int8_t lun;
  u_int8_t cblength;
  u_int8_t cbwcb[16];
} CBW;

typedef struct _CSW {
  u_int32_t signature;
  u_int32_t tag;
  u_int32_t dataResidue;
  u_int8_t status;
} CSW;

PUBLIC void mst_data_out(void *cmd, int cmd_len, void *data, int data_len);
PUBLIC void mst_data_in(void *cmd, int cmd_len, void *data, int data_len);

#endif /* _MSTORAGE_H */
