#ifndef _PCI_H
#define _PCI_H

#include <sodex/const.h>
#include <sys/types.h>

#define PCI_CONF_ADDR_PORT  0x0cf8
#define PCI_CONF_DATA_PORT  0x0cfc

#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_REVISION        0x08
#define PCI_API             0x09
#define PCI_SUBCLASS        0x0a
#define PCI_BASECLASS       0x0b
#define PCI_HEADER          0x0e
#define PCI_BASE_ADDRESS0   0x10
#define PCI_BASE_ADDRESS1   0x14
#define PCI_BASE_ADDRESS2   0x18
#define PCI_BASE_ADDRESS3   0x1C
#define PCI_BASE_ADDRESS4   0x20
#define PCI_BASE_ADDRESS5   0x24
#define PCI_IRQ_LINE        0x3C
#define PCI_IRQ_PIN         0x3D

/* base address */
#define PCI_BASE_KIND       0x0
#define PCI_BASE_KIND_MEM   0
#define PCI_BASE_KIND_IO    1


#define ADDR_PORT_ENABLE_BIT (1<<31)

#define BUS_NUM_MAX     255
#define DEVICE_NUM_MAX  32
#define FUNC_NUM_MAX     8


/* vendor */
#define NO_VENDOR           0xffff
#define PCI_VENDOR_INTEL    0x8086
#define PCI_VENDOR_REALTEK  0x10ec
#define PCI_VENDOR_CIRRUS   0x1013
#define MAX_VENDORS 4

/* nic device */
#define NIC_REALTEK 0x802910ec // for qemu
#define MAX_NIC_DEVICES 1

/* usb device */
#define UHC_QEMU        0x70208086 // for qemu
#define UHC_IBM_X60S_1  0x27C88086 // for X60s
#define UHC_IBM_X60S_2  0x27C98086 // for X60s
#define UHC_IBM_X60S_3  0x27CA8086 // for X60s
#define UHC_IBM_X60S_4  0x27CB8086 // for X60s
#define MAX_USB_DEVICES 2
#define PCI_CFG_USB_CLASS   0x09
#define PCI_CFG_USB_BASE    0x20
#define PCI_CFG_USB_SBRN    0x60

#define MAX_PCI_DEVICE 64
#define MAX_VENDOR_NAME 32
#define MAX_DEVICE_NAME 32

struct pci_info {
  u_int16_t vendor_id;
  u_int16_t device_id;
  char vendor_name[MAX_VENDOR_NAME];
  char device_name[MAX_DEVICE_NAME];
  u_int8_t  base_addr_kind;
  u_int32_t base_addr;
  u_int8_t irq;
};

typedef union {
  u_int32_t command;
  struct {
    unsigned none       : 2;
    unsigned reg        : 6;
    unsigned function   : 3;
    unsigned device     : 5;
    unsigned bus        : 8;
    unsigned reserve    : 7;
    unsigned enabled    : 1;
  } p;
} pci_param;

PUBLIC struct pci_info *pci_info;

PUBLIC void init_pci();
PUBLIC struct pci_info* search_pci_device();
PUBLIC u_int32_t pci_read_config(u_int8_t bus, u_int8_t device,
                                 u_int8_t function, u_int8_t reg, u_int8_t size);

#endif /* pci.h */
