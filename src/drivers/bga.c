#include <bga.h>
#include <io.h>
#include <page.h>
#include <pci.h>
#include <string.h>

#define BGA_VENDOR_ID     0x1234
#define BGA_DEVICE_ID     0x1111
#define BGA_PORT_INDEX    0x01ce
#define BGA_PORT_DATA     0x01cf

#define BGA_INDEX_ID          0x0
#define BGA_INDEX_XRES        0x1
#define BGA_INDEX_YRES        0x2
#define BGA_INDEX_BPP         0x3
#define BGA_INDEX_ENABLE      0x4
#define BGA_INDEX_VIRT_WIDTH  0x6

#define BGA_ENABLE_DISABLED   0x00
#define BGA_ENABLE_ENABLED    0x01
#define BGA_ENABLE_LFB        0x40

struct bga_pci_addr {
  u_int8_t bus;
  u_int8_t device;
  u_int8_t function;
};

struct bga_mode {
  u_int16_t width;
  u_int16_t height;
  u_int16_t bpp;
};

PRIVATE u_int16_t bga_read_reg(u_int16_t index);
PRIVATE void bga_write_reg(u_int16_t index, u_int16_t value);
PRIVATE int bga_find_pci_device(struct bga_pci_addr *addr, u_int32_t *bar0);
PRIVATE int bga_detect(void);
PRIVATE int bga_try_mode(const struct bga_mode *mode);

PRIVATE u_int16_t bga_read_reg(u_int16_t index)
{
  out16(BGA_PORT_INDEX, index);
  return in16(BGA_PORT_DATA);
}

PRIVATE void bga_write_reg(u_int16_t index, u_int16_t value)
{
  out16(BGA_PORT_INDEX, index);
  out16(BGA_PORT_DATA, value);
}

PRIVATE int bga_find_pci_device(struct bga_pci_addr *addr, u_int32_t *bar0)
{
  u_int8_t bus;
  u_int8_t device;
  u_int8_t function;

  for (bus = 0; bus < BUS_NUM_MAX; bus++) {
    for (device = 0; device < DEVICE_NUM_MAX; device++) {
      for (function = 0; function < FUNC_NUM_MAX; function++) {
        u_int32_t status;

        status = pci_read_config(bus, device, function, PCI_VENDOR_ID, 4);
        if (status == 0xffffffff) {
          if (function == 0)
            break;
          continue;
        }
        if ((status & 0xffff) == BGA_VENDOR_ID &&
            (status >> 16) == BGA_DEVICE_ID) {
          *bar0 = pci_read_config(bus, device, function, PCI_BASE_ADDRESS0, 4);
          addr->bus = bus;
          addr->device = device;
          addr->function = function;
          return 0;
        }
      }
    }
  }

  return -1;
}

PRIVATE int bga_detect(void)
{
  u_int16_t id;

  id = bga_read_reg(BGA_INDEX_ID);
  if (id < 0xb0c0 || id > 0xb0c5)
    return -1;
  return 0;
}

PRIVATE int bga_try_mode(const struct bga_mode *mode)
{
  bga_write_reg(BGA_INDEX_ENABLE, BGA_ENABLE_DISABLED);
  bga_write_reg(BGA_INDEX_XRES, mode->width);
  bga_write_reg(BGA_INDEX_YRES, mode->height);
  bga_write_reg(BGA_INDEX_BPP, mode->bpp);
  bga_write_reg(BGA_INDEX_ENABLE, BGA_ENABLE_ENABLED | BGA_ENABLE_LFB);

  if (bga_read_reg(BGA_INDEX_XRES) != mode->width)
    return -1;
  if (bga_read_reg(BGA_INDEX_YRES) != mode->height)
    return -1;
  if (bga_read_reg(BGA_INDEX_BPP) != mode->bpp)
    return -1;
  return 0;
}

PUBLIC int bga_init(struct fb_info *info)
{
  static const struct bga_mode modes[] = {
    {1280, 800, 32},
    {1024, 768, 32},
    {800, 600, 32},
    {640, 480, 32}
  };
  struct bga_pci_addr addr;
  u_int32_t bar0;
  u_int16_t command;
  int i;

  if (info == NULL)
    return -1;
  memset(info, 0, sizeof(*info));

  if (bga_find_pci_device(&addr, &bar0) < 0)
    return -1;
  if (bga_detect() < 0)
    return -1;

  command = pci_read_config(addr.bus, addr.device, addr.function,
                            PCI_COMMAND, 2);
  if ((command & 0x0003) != 0x0003) {
    pci_write_config(addr.bus, addr.device, addr.function,
                     PCI_COMMAND, 2, command | 0x0003);
  }

  for (i = 0; i < (int)(sizeof(modes) / sizeof(modes[0])); i++) {
    u_int32_t phys_base;
    u_int32_t phys_page;
    u_int32_t page_offset;
    u_int16_t pitch_pixels;
    u_int32_t fb_size;
    u_int32_t map_size;

    if (bga_try_mode(&modes[i]) < 0)
      continue;

    phys_base = bar0 & 0xfffffff0;
    phys_page = phys_base & ~(PSE_PAGE_SIZE - 1);
    page_offset = phys_base & (PSE_PAGE_SIZE - 1);
    pitch_pixels = bga_read_reg(BGA_INDEX_VIRT_WIDTH);
    if (pitch_pixels == 0)
      pitch_pixels = modes[i].width;
    fb_size = (u_int32_t)pitch_pixels * modes[i].height *
              (modes[i].bpp / 8);
    map_size = page_offset + fb_size;
    if (map_size > PSE_PAGE_SIZE * 2)
      continue;

    pg_set_kernel_4m_page(BGA_FB_VADDR, phys_page,
                          PAGE_PRESENT | PAGE_RW | PAGE_US | PAGE_GLOBAL);
    if (map_size > PSE_PAGE_SIZE) {
      /* 高解像度時は LFB が 4MB を跨ぐので次の 4MB も続けて張る。 */
      pg_set_kernel_4m_page(BGA_FB_VADDR + PSE_PAGE_SIZE,
                            phys_page + PSE_PAGE_SIZE,
                            PAGE_PRESENT | PAGE_RW | PAGE_US | PAGE_GLOBAL);
    }
    info->available = TRUE;
    info->width = modes[i].width;
    info->height = modes[i].height;
    info->pitch = pitch_pixels * (modes[i].bpp / 8);
    info->bpp = modes[i].bpp;
    info->size = fb_size;
    info->base = (void *)(BGA_FB_VADDR + page_offset);
    return 0;
  }

  bga_write_reg(BGA_INDEX_ENABLE, BGA_ENABLE_DISABLED);
  return -1;
}
