
#include <uip.h>
#include <ne2000.h>
#include <ether.h>
#include <vga.h>

// test
PUBLIC void ether_send()
{
  uip_init();
  uip_ipaddr_t addr;
  uip_ipaddr(&addr, 192,168,81,40);
  uip_sethostaddr(&addr);
  uip_ipaddr(&addr, 255,255,255,0);
  uip_setnetmask(&addr);
  uip_ipaddr(&addr, 192,168,81,1);
  uip_setdraddr(&addr);
  
  u_int32_t* p = (u_int32_t*)uip_buf;
  int count;
  for (count = 0; count < 16; count++) {
    _kprintf("%x", p[count++]);
  } 
  _kprintf("\nlen:%x\n", uip_len);

  ne2000_send(uip_buf, uip_len);
}
