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
#include <stdarg.h>

/* VGA text-mode color attributes */
#define VGA_COLOR_GREEN   0x02
#define VGA_COLOR_CYAN    0x30
#define VGA_COLOR_DEFAULT VGA_COLOR_GREEN

PUBLIC void _poscolor_printc(int x, int y, char color, char c);
PUBLIC void _pos_putc(int x, int y, char c);
PUBLIC void _kputc(char c);
PUBLIC void _kputs(char *str);
PUBLIC void _kprintf(char *str, ...);
PUBLIC int _kvsnprintf(char *buf, int size, const char *fmt, va_list ap);
PUBLIC void _kprintb(u_int8_t c);
PUBLIC void _kprintb16(u_int16_t c);
PUBLIC void _kprintb32(u_int32_t c);
PUBLIC void screen_scrollup();
PUBLIC void clr_screen();
PUBLIC void init_screen();
PUBLIC void screen_pointset(int x, int y);
PUBLIC int screen_cols(void);
PUBLIC int screen_rows(void);

PUBLIC void screen_save_prompt();
PUBLIC void print_registers();
PUBLIC void debug_print();

#endif /* vga.h */
