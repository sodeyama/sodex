#ifndef _NIC_H_
#define _NIC_H_

#include <pci.h>

struct nic_info {
  struct nic_info* next_nic;
  struct pci_info* pci;
};
struct nic_info* nic_info;
struct nic_info* cur_nic_info;

#endif /* _NIC_H_ */
