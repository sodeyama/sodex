#include <vga.h>
#include <memory.h>
#include <lib.h>
#include <uhci-hcd.h>
#include <mstorage.h>

PRIVATE int tag_out_count = 0;
PRIVATE int tag_in_count = 0;

PUBLIC void mst_data_out(void *cmd, int cmd_len, void *data, int data_len)
{
  CBW *cbw = kalloc(sizeof(CBW));
  memset(cbw, 0, sizeof(CBW));
  cbw->signature = CBW_SIGNATURE;
  cbw->tag = tag_out_count++;
  cbw->length = data_len;
  cbw->flags = CBW_FLAG_OUT;
  cbw->lun = 0;
  cbw->cblength = cmd_len;
  memcpy(cbw->cbwcb, cmd, cmd_len);

  bulk_out(cbw, CBW_SIZE);
  bulk_out((char*)data, data_len);
  bulk_out(NULL, 0);

  CSW *csw = kalloc(sizeof(CSW));
  memset(csw, 0, sizeof(CSW));
  csw->signature = CSW_SIGNATURE;
  bulk_in(csw, CSW_SIZE);
}

PUBLIC void mst_data_in(void *cmd, int cmd_len, void *data, int data_len)
{
  CBW *cbw = kalloc(sizeof(CBW));
  memset(cbw, 0, sizeof(CBW));
  cbw->signature = CBW_SIGNATURE;
  cbw->tag = tag_in_count++;
  cbw->length = data_len;
  cbw->flags = CBW_FLAG_IN;
  cbw->lun = 0;
  cbw->cblength = cmd_len;
  memcpy(cbw->cbwcb, cmd, cmd_len);

  bulk_out(cbw, CBW_SIZE);
  bulk_in((char*)data, data_len);
  bulk_in(NULL, 0);
  
  CSW *csw = kalloc(sizeof(CSW));
  memset(csw, 0, sizeof(CSW));
  csw->signature = CSW_SIGNATURE;
  bulk_in(csw, CSW_SIZE);
}
