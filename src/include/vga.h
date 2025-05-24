/*
 *  @File        vga_screen.h
 *  @Brief       print char and string on the screen.
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        creae: 2007/04/19  update: 2007/04/19  
 *      
 *  Copyright (C) 2007 Sodex
 */

#ifndef _VGA_H
#define _VGA_H

#include <sodex/const.h>
#include <sys/types.h>

#define SCREEN_WIDTH    80
#define SCREEN_HEIGHT   25
#define VRAMMAX         2080 // SCREEN*(SCREEN_HEIGHT+1)

#define VRAM    ((char *)0xc00b8000)

PUBLIC void _poscolor_printc(int x, int y, char color, char c);
PUBLIC void _pos_putc(int x, int y, char c);
PUBLIC void _kputc(char c);
PUBLIC void _kputs(char *str);
PUBLIC void _kprintf(char *str, ...);
PUBLIC void _kprintb(u_int8_t c);
PUBLIC void _kprintb16(u_int16_t c);
PUBLIC void _kprintb32(u_int32_t c);
PUBLIC void screen_scrollup();
PUBLIC void clr_screen();
PUBLIC void init_screen();
PUBLIC void screen_pointset(int x, int y);

PUBLIC void print_registers();
PUBLIC void debug_print();

#endif /* vga.h */
