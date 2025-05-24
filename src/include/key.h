#ifndef _KEY_H
#define _KEY_H

#include <sodex/const.h>
#include <sys/types.h>

#define KEY_NULL 0
#define KEY_1   '1'
#define KEY_2   '2'
#define KEY_3   '3'
#define KEY_4   '4'
#define KEY_5   '5'
#define KEY_6   '6'
#define KEY_7   '7'
#define KEY_8   '8'
#define KEY_9   '9'
#define KEY_0   '0'
#define KEY_A   'a'
#define KEY_B   'b'
#define KEY_C   'c'
#define KEY_D   'd'
#define KEY_E   'e'
#define KEY_F   'f'
#define KEY_G   'g'
#define KEY_H   'h'
#define KEY_I   'i'
#define KEY_J   'j'
#define KEY_K   'k'
#define KEY_L   'l'
#define KEY_M   'm'
#define KEY_N   'n'
#define KEY_O   'o'
#define KEY_P   'p'
#define KEY_Q   'q'
#define KEY_R   'r'
#define KEY_S   's'
#define KEY_T   't'
#define KEY_U   'u'
#define KEY_V   'v'
#define KEY_W   'w'
#define KEY_X   'x'
#define KEY_Y   'y'
#define KEY_Z   'z'
#define KEY_ESC             0x1B
#define KEY_MINUS           '-'
#define KEY_POW             '^' 
#define KEY_BACK            0x08
#define KEY_TAB             0x09
#define KEY_ATTO            0x40
#define KEY_LEFTBIGCACCO    0x5B
#define KEY_ENTER           0x0D
#define KEY_BACKSLASH       0x5C
#define KEY_PLUS            0x2B
#define KEY_ASTA            0x2A
#define KEY_LEFTSHIFT       0x0F
#define KEY_RIGHTBIGCACCO   0x5D
#define KEY_SMALL           0x3C
#define KEY_BIG             0x3E
#define KEY_DOT             0x2E
#define KEY_SLASH           0x2F
#define KEY_RIGHTSHIFT      0x0F
#define KEY_LEFTALT         0x00
#define KEY_SPACE           ' '
#define KEY_CAPS            0x3A
#define KEY_F1              0x00
#define KEY_F2              0x00
#define KEY_F3              0x00
#define KEY_F4              0x00
#define KEY_F5              0x00
#define KEY_F6              0x00
#define KEY_F7              0x00
#define KEY_F8              0x00
#define KEY_F9              0x00
#define KEY_F10             0x00
#define KEY_F11             0x00
#define KEY_F12             0x00
#define KEY_SHIFT           0x0F

#define KEY_BUF 64

struct stdin_list {
  struct stdin_list* next;
  char buf[KEY_BUF+1];
  int nums;
};

PUBLIC char get_keymap(u_int8_t c);
PUBLIC char get_shiftkeymap(u_int8_t c);
PUBLIC char set_stdin(u_int8_t c);
PUBLIC char* get_stdin(char* tobuf);

#endif /* _KEY_H */
