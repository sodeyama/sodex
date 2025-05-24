/*
 *  @File        pci.c
 *  @Brief       pci driver
 *  
 *  @Author      Sodex
 *  @License     GPL
 *  @Date        creae: 2009/06/26  update: 2009/06/26
 *      
 *  Copyright (C) 2007 Sodex
 */

#include <vga.h>
#include <io.h>
#include <memory.h>
#include <pci.h>
#include <nic.h>
#include <uhci-hcd.h>

PRIVATE void regist_pci_devices();
PRIVATE int check_vendor(u_int16_t id);
PRIVATE int check_nic_device(u_int32_t status);
PRIVATE int check_usb_device(u_int32_t status);
PRIVATE void regist_nic_device(struct pci_info* pci_info);
PRIVATE void regist_usb_device(struct pci_info* pci_info);
PRIVATE void set_pci_info(u_int32_t status, u_int8_t bus_num,
                          u_int8_t device_num, u_int8_t func_num,
                          int count);
PRIVATE int is_multi_function(u_int8_t bus_name, u_int8_t device_num);

PRIVATE int nic_host_count = 0;
PRIVATE int usb_host_count = 0;

PRIVATE u_int16_t vendor_list[] = {
  PCI_VENDOR_INTEL,
  PCI_VENDOR_REALTEK,
  PCI_VENDOR_CIRRUS
};

PRIVATE u_int32_t nic_dev_list[] = {
  NIC_REALTEK
};

PRIVATE u_int32_t usb_dev_list[] = {
  UHC_QEMU,
  UHC_IBM_X60S_1,
  UHC_IBM_X60S_2,
  UHC_IBM_X60S_3,
  UHC_IBM_X60S_4
};

PUBLIC void init_pci()
{
  pci_info = kalloc(sizeof(struct pci_info) * MAX_PCI_DEVICE);
  nic_info = NULL;
  cur_nic_info = NULL;
  regist_pci_devices();
}

PUBLIC u_int32_t pci_read_config(u_int8_t bus, u_int8_t device,
                                 u_int8_t function, u_int8_t reg, u_int8_t size)
{
  u_int32_t result;
  pci_param param;
  param.p.enabled = 1;
  param.p.none = 0;
  param.p.reserve = 0;
  param.p.bus = bus;
  param.p.device = device;
  param.p.function = function;
  param.p.reg = ((reg & ~3) >> 2);
  out32(PCI_CONF_ADDR_PORT, param.command);
  switch (size) {
  case 1:
    result = in8(PCI_CONF_DATA_PORT + (reg & 3));
    break;
  case 2:
    result = in16(PCI_CONF_DATA_PORT + (reg & 3));
    break;
  case 4:   
    result = in32(PCI_CONF_DATA_PORT);
    break;
  default:
    result = 0xffffffff;
    break;
  }
  param.p.enabled = 0;
  out32(PCI_CONF_ADDR_PORT, param.command);
  return result;
}

PRIVATE void regist_pci_devices()
{
  u_int8_t func_num = 0;
  u_int8_t device_num = 0;
  u_int8_t bus_num = 0;
  u_int32_t status;
  int multi_func_checked;
  int count = 0;
  for (;bus_num < BUS_NUM_MAX; bus_num++) {
    for (;device_num < DEVICE_NUM_MAX; device_num++) {
      for (func_num = 0, multi_func_checked = FALSE; func_num < FUNC_NUM_MAX; func_num++) {
        status = pci_read_config(bus_num, device_num, func_num, PCI_VENDOR_ID, 4);
        //if (check_vendor(vendor)) {
        if (status != 0xffffffff) {
          set_pci_info(status, bus_num, device_num, func_num, count++);
          if (!multi_func_checked && !is_multi_function(bus_num, device_num)) {
            break;
          }
          multi_func_checked = TRUE;
        }
      }
    }
  }
}

PRIVATE int check_nic_device(u_int32_t status)
{
  int nic_devlist_size = sizeof(nic_dev_list)/sizeof(nic_dev_list[0]);
  int i;
  for (i = 0; i < nic_devlist_size; i++) {
    if (status == nic_dev_list[i]) {
      return TRUE;
    }
  }
  return FALSE;
}

PRIVATE int check_usb_device(u_int32_t status)
{
  int usb_devlist_size = sizeof(usb_dev_list)/sizeof(usb_dev_list[0]);
  int i;
  for (i = 0; i < usb_devlist_size; i++) {
    if (status == usb_dev_list[i]) {
      return TRUE;
    }
  }
  return FALSE;
}

PRIVATE void regist_nic_device(struct pci_info* pci_info)
{
  cur_nic_info = kalloc(sizeof(struct nic_info));
  if (nic_info == NULL) {
    nic_info = cur_nic_info;
  }
  cur_nic_info->pci = pci_info;
  cur_nic_info = cur_nic_info->next_nic;
}

PRIVATE void regist_usb_device(struct pci_info* pci_info)
{
  usb_info[usb_host_count] = kalloc(sizeof(USB_HC_INFO));
  memset(usb_info[usb_host_count], 0, sizeof(USB_HC_INFO));
  usb_info[usb_host_count]->pci_info = pci_info;
  usb_host_count++;
}

PRIVATE int check_vendor(u_int16_t id)
{
  int vendor_list_size = sizeof(vendor_list)/sizeof(vendor_list[0]);
  int i;
  for (i = 0; i < vendor_list_size; i++) {
    if (id == vendor_list[i]) {
      return TRUE;
    }
  }
  return FALSE;
}

PRIVATE void set_pci_info(u_int32_t status, u_int8_t bus_num,
                          u_int8_t device_num, u_int8_t func_num,
                          int count)
{
  u_int16_t vendor = status & 0xffff;
  u_int16_t device = status >> 16;
  u_int32_t base_addr = 0;
  u_int8_t irq = pci_read_config(bus_num, device_num, func_num, PCI_IRQ_LINE, 1);
  pci_info[count].vendor_id = vendor;
  pci_info[count].device_id = device;
  pci_info[count].irq = irq;
  if (check_nic_device(status)) { // nic device
    u_int32_t config = pci_read_config(bus_num, device_num, func_num, PCI_BASE_ADDRESS0, 4);
    pci_info[count].base_addr = (config & 0xfffffffc);
    pci_info[count].base_addr_kind = (config & 0x00000001);
    regist_nic_device(&pci_info[count]);
    _kprintf("   [PCI][NIC] regist vendor:%x, device:%x, func_num:%x\n",
             vendor, device, func_num);
  } else if (check_usb_device(status)) { // usb device
    u_int32_t config = pci_read_config(bus_num, device_num, func_num, PCI_BASE_ADDRESS4, 4);
    pci_info[count].base_addr = (config & 0xfffffffc);
    pci_info[count].base_addr_kind = (config & 0x00000001);
    regist_usb_device(&pci_info[count]);
    _kprintf("   [PCI][USB] regist vendor:%x, device:%x, func_num:%x\n",
             vendor, device, func_num);
  } else { // other device
  }
}

PRIVATE int is_multi_function(u_int8_t bus_num, u_int8_t device_num)
{
  u_int8_t header_type = pci_read_config(bus_num, device_num, 0, PCI_HEADER, 1);
  if (header_type && (1<<7)) {
    return TRUE;
  } else {
    return FALSE;
  }
}
