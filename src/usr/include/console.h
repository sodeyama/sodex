#ifndef _USR_CONSOLE_H
#define _USR_CONSOLE_H

int console_cols(void);
int console_rows(void);
void console_putc_at(int x, int y, char color, char c);
void console_set_cursor(int x, int y);
void console_clear(void);

#endif /* _USR_CONSOLE_H */
