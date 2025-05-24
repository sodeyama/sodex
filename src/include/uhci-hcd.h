#ifndef _UHCI_HCD_H
#define _UHCI_UCD_H

#include <sodex/const.h>
#include <sys/types.h>
#include <ihandlers.h>
#include <pci.h>

#define UHC_HOST_MAX    16

#define USBSOF_DEFAULT	0x40	/* Frame length is exactly 1 ms */

/* registers */
#define USBCMD_R    0x00
#define USBSTS_R    0x02
#define USBINTR_R   0x04
#define USBFRNUM_R  0x06
#define FRBASEADD_R 0x08
#define SOFMOD_R    0x0C
#define PORTSC1_R   0x10
#define PORTSC2_R   0x12


#define   USBPORTSC_CCS		0x0001	/* Current Connect Status */
#define   USBPORTSC_CSC		0x0002	/* Connect Status Change */
#define   USBPORTSC_PE		0x0004	/* Port Enable */
#define   USBPORTSC_PEC		0x0008	/* Port Enable Change */
#define   USBPORTSC_DPLUS	0x0010	/* D+ high (line status) */
#define   USBPORTSC_DMINUS	0x0020	/* D- high (line status) */
#define   USBPORTSC_RD		0x0040	/* Resume Detect */
#define   USBPORTSC_RES1	0x0080	/* reserved, always 1 */
#define   USBPORTSC_LSDA	0x0100	/* Low Speed Device Attached */
#define   USBPORTSC_PR		0x0200	/* Port Reset */
/* OC and OCC from Intel 430TX and later (not UHCI 1.1d spec) */
#define   USBPORTSC_OC		0x0400	/* Over Current condition */
#define   USBPORTSC_OCC		0x0800	/* Over Current Change R/WC */
#define   USBPORTSC_SUSP	0x1000	/* Suspend */
#define   USBPORTSC_RES2	0x2000	/* reserved, write zeroes */
#define   USBPORTSC_RES3	0x4000	/* reserved, write zeroes */
#define   USBPORTSC_RES4	0x8000	/* reserved, write zeroes */

#define RWC_BITS	(USBPORTSC_OCC | USBPORTSC_PEC | USBPORTSC_CSC)
#define WZ_BITS		(USBPORTSC_RES2 | USBPORTSC_RES3 | USBPORTSC_RES4)
#define RWC_BITS	(USBPORTSC_OCC | USBPORTSC_PEC | USBPORTSC_CSC)
#define SUSPEND_BITS	(USBPORTSC_SUSP | USBPORTSC_RD)

#define USBSTS_INT      0x1
#define USBSTS_ERR_INT  0x2
#define USBSTS_RESUME   0x4
#define USBSTS_SYS_ERR  0x8
#define USBSTS_PROC_ERR 0x10
#define USBSTS_HALT     0x20


#define UHCI_FRAME_SIZE 1024


/* The parameter of Frame list pointer */
#define QH_T    0x01
#define QH_Q    0x02
#define TD_T    0x01
#define TD_Q    0x02
#define TD_DEPTH_FIRST      0x04

#define TD_ISSUE_IOC        1
#define TD_PID_SETUP        0x2D
#define TD_PID_IN           0x69
#define TD_PID_OUT          0xE1
#define TD_PID_ACK          0xD2
#define TD_PID_NAK          0x5A
#define TD_PID_STALL        0x1E

#define TD_STATUS_ACTIVE    0x80

/* STANDARD DEVICE DESCRIPTOR */
#define GET_STATUS          0x00
#define SET_ADDRESS         0x05
#define GET_DESCRIPTOR      0x06
#define GET_CONFIGURATION   0x08
#define SET_CONFIGURATION   0x09

#define UHCI_USBCMD_UCRESET 0x0002


#define USB_MASS_STORAGE_ADDR   1

#define MAX_BULKTRANS_SIZE  512 //0x500 // 1280Byte

#define UHCI_WAIT_COUNTMAX  30000000

/* RH is Root Hub. */
#define CLR_RH_PORTSTAT(x, port_addr)    \
	status = in16(port_addr);            \
	status &= ~(RWC_BITS|WZ_BITS);       \
	status &= ~(x);                      \
	status |= RWC_BITS & (x);            \
	out16(port_addr, status)

#define SET_RH_PORTSTAT(x, port_addr)    \
	status = in16(port_addr);            \
	status |= (x);                       \
	status &= ~(RWC_BITS|WZ_BITS);       \
	out16(port_addr, status)

/* 
 * USBCOMMAND REGISTER
 * IO_BASE 00h - 01h (16bit)
 */
typedef union usbcmd {
  u_int16_t cmd;
  struct {
    u_int16_t rs      : 1;
    u_int16_t hcreset : 1;
    u_int16_t greset  : 1;
    u_int16_t egsm    : 1;
    u_int16_t fgr     : 1;
    u_int16_t swdbg   : 1;
    u_int16_t cf      : 1;
    u_int16_t maxp    : 1;
    u_int16_t none    : 8;
  } s;
} USBCMD;

/* 
 * USB INTERRUPT ENABLE REGISTER
 * IO_BASE 04h - 05h (16bit)
 */
typedef union usbintr {
  u_int16_t cmd;
  struct {
    u_int16_t crc_enable    : 1;
    u_int16_t resume_enable : 1;
    u_int16_t ioc_enable    : 1;
    u_int16_t sp_enable     : 1;
    u_int16_t none          : 12;
  } s;
} USBINTR;


typedef union uhci_frame_list {
  u_int32_t frame_list_pointer;
  struct {
    unsigned t    : 1;
    unsigned q    : 1;
    unsigned none : 30;
  } s;
} UHCI_FRAME_LIST;

typedef struct uhci_td {
  u_int32_t link_pointer;
  struct {
    unsigned actlen : 11;
    unsigned r2     : 5;
    unsigned status : 8;
    unsigned ioc    : 1;
    unsigned ios    : 1;
    unsigned ls     : 1;
    unsigned c_err  : 2;
    unsigned spd    : 1;
    unsigned r      : 2;
  } control;
  struct {
    unsigned pid            : 8;
    unsigned dev_addr       : 7;
    unsigned endpoint       : 4;
    unsigned data_toggle    : 1;
    unsigned r              : 1;
    unsigned maxlen         : 11;
  } token;
  u_int32_t buffer_pointer;
} UHCI_TD;

typedef struct uhci_qh {
  u_int32_t head_link;
  u_int32_t elem_link;
} UHCI_QH;

typedef struct qh_free_list {
  struct qh_free_list *qh_next;
  UHCI_QH *qh;
} QH_FREE_LIST;

typedef struct td_free_list {
  struct td_free_list *td_next;
  UHCI_TD *td;
} TD_FREE_LIST;

typedef struct usb_hc_info {
  struct pci_info* pci_info;
  int numports;
} USB_HC_INFO;

typedef struct std_device_request {
  u_int8_t bmRequestType;
  u_int8_t bRequest;
  u_int16_t wValue;
  u_int16_t wIndex;
  u_int16_t wLength;
} STD_DEVICE_REQUEST;

USB_HC_INFO* usb_info[UHC_HOST_MAX];

UHCI_FRAME_LIST *frame_list;

PUBLIC void init_uhci(void);
PUBLIC void bulk_out(char *data, int length);
PUBLIC void bulk_in(char *data, int length);

#endif /* _UHCI_HCD_H */
