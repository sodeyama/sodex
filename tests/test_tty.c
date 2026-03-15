#include "test_framework.h"
#include <string.h>

typedef unsigned short u_int16_t;
typedef unsigned char u_int8_t;

#define TEST_TTY_RING_SIZE 8192
#define TEST_TTY_CANON_SIZE 256
#define TEST_TTY_LONG_OUTPUT 6000

struct tty_ring {
    char data[TEST_TTY_RING_SIZE];
    int head;
    int tail;
};

struct tty {
    int active;
    int has_master;
    u_int8_t flags;
    u_int16_t cols;
    u_int16_t rows;
    struct tty_ring slave_rx;
    struct tty_ring master_rx;
    char canon_buf[TEST_TTY_CANON_SIZE];
    int canon_len;
    void *slave_wait;
    void *master_wait;
};

struct winsize {
    u_int16_t cols;
    u_int16_t rows;
};

struct termios {
    unsigned int c_lflag;
};

struct files_struct;
struct file;

#define ICANON 0x0001
#define ECHO   0x0002
#define ISIG   0x0004

int files_alloc_fd(struct files_struct *files, struct file *file) {
    static int next_fd = 0;
    (void)files;
    (void)file;
    return next_fd++;
}

int file_put(struct file *file) {
    (void)file;
    return 1;
}

extern void init_tty(void);
extern struct tty *tty_get_console(void);
extern struct tty *tty_alloc_pty(void);
extern void tty_release(struct tty *tty);
extern int tty_set_winsize(struct tty *tty, u_int16_t cols, u_int16_t rows);
extern int tty_get_winsize(struct tty *tty, struct winsize *winsize);
extern int tty_set_input_mode(int mode);
extern int tty_get_input_mode(void);
extern int tty_get_termios(struct tty *tty, struct termios *termios);
extern int tty_set_termios(struct tty *tty, const struct termios *termios);
extern void tty_feed_console_char(char c);
extern ssize_t tty_master_write(struct tty *tty, const void *buf, size_t count);
extern ssize_t tty_master_read(struct tty *tty, void *buf, size_t count);
extern ssize_t tty_slave_read(struct tty *tty, void *buf, size_t count, int block);
extern ssize_t tty_slave_write(struct tty *tty, const void *buf, size_t count);

TEST(canonical_input_waits_for_enter) {
    struct tty *tty;
    char buf[8];

    init_tty();
    tty = tty_alloc_pty();
    ASSERT_NOT_NULL(tty);

    ASSERT_EQ(tty_master_write(tty, "abc", 3), 3);
    ASSERT_EQ(tty_slave_read(tty, buf, sizeof(buf), 0), 0);
    ASSERT_EQ(tty_master_write(tty, "\r", 1), 1);
    ASSERT_EQ(tty_slave_read(tty, buf, sizeof(buf), 0), 4);
    ASSERT_EQ(buf[0], 'a');
    ASSERT_EQ(buf[1], 'b');
    ASSERT_EQ(buf[2], 'c');
    ASSERT_EQ(buf[3], '\0');
    tty_release(tty);
}

TEST(canonical_backspace_echo_erases_on_master) {
    struct tty *tty;
    char buf[8];

    init_tty();
    tty = tty_alloc_pty();
    ASSERT_NOT_NULL(tty);

    ASSERT_EQ(tty_master_write(tty, "a", 1), 1);
    ASSERT_EQ(tty_master_write(tty, "\b", 1), 1);
    ASSERT_EQ(tty_master_write(tty, "\r", 1), 1);
    ASSERT_EQ(tty_master_read(tty, buf, sizeof(buf)), 5);
    ASSERT_EQ(buf[0], 'a');
    ASSERT_EQ(buf[1], '\b');
    ASSERT_EQ(buf[2], ' ');
    ASSERT_EQ(buf[3], '\b');
    ASSERT_EQ(buf[4], '\n');
    tty_release(tty);
}

TEST(canonical_delete_key_erases_on_master) {
    struct tty *tty;
    char buf[8];
    char slave_buf[8];
    const char del = 0x7f;

    init_tty();
    tty = tty_alloc_pty();
    ASSERT_NOT_NULL(tty);

    ASSERT_EQ(tty_master_write(tty, "ab", 2), 2);
    ASSERT_EQ(tty_master_write(tty, &del, 1), 1);
    ASSERT_EQ(tty_master_write(tty, "\r", 1), 1);
    ASSERT_EQ(tty_slave_read(tty, slave_buf, sizeof(slave_buf), 0), 2);
    ASSERT_EQ(slave_buf[0], 'a');
    ASSERT_EQ(slave_buf[1], '\0');
    ASSERT_EQ(tty_master_read(tty, buf, sizeof(buf)), 6);
    ASSERT_EQ(buf[0], 'a');
    ASSERT_EQ(buf[1], 'b');
    ASSERT_EQ(buf[2], '\b');
    ASSERT_EQ(buf[3], ' ');
    ASSERT_EQ(buf[4], '\b');
    ASSERT_EQ(buf[5], '\n');
    tty_release(tty);
}

TEST(canonical_utf8_backspace_erases_whole_character) {
    struct tty *tty;
    char buf[32];
    char slave_buf[16];
    const char a_utf8[] = { (char)0xe3, (char)0x81, (char)0x82 };
    const char i_utf8[] = { (char)0xe3, (char)0x81, (char)0x84 };
    const char u_utf8[] = { (char)0xe3, (char)0x81, (char)0x86 };
    const char e_utf8[] = { (char)0xe3, (char)0x81, (char)0x88 };

    init_tty();
    tty = tty_alloc_pty();
    ASSERT_NOT_NULL(tty);

    ASSERT_EQ(tty_master_write(tty, a_utf8, sizeof(a_utf8)), (ssize_t)sizeof(a_utf8));
    ASSERT_EQ(tty_master_write(tty, i_utf8, sizeof(i_utf8)), (ssize_t)sizeof(i_utf8));
    ASSERT_EQ(tty_master_write(tty, u_utf8, sizeof(u_utf8)), (ssize_t)sizeof(u_utf8));
    ASSERT_EQ(tty_master_write(tty, "\b", 1), 1);
    ASSERT_EQ(tty_master_write(tty, e_utf8, sizeof(e_utf8)), (ssize_t)sizeof(e_utf8));
    ASSERT_EQ(tty_master_write(tty, "\r", 1), 1);

    ASSERT_EQ(tty_slave_read(tty, slave_buf, sizeof(slave_buf), 0), 10);
    ASSERT_EQ((unsigned char)slave_buf[0], 0xe3);
    ASSERT_EQ((unsigned char)slave_buf[1], 0x81);
    ASSERT_EQ((unsigned char)slave_buf[2], 0x82);
    ASSERT_EQ((unsigned char)slave_buf[3], 0xe3);
    ASSERT_EQ((unsigned char)slave_buf[4], 0x81);
    ASSERT_EQ((unsigned char)slave_buf[5], 0x84);
    ASSERT_EQ((unsigned char)slave_buf[6], 0xe3);
    ASSERT_EQ((unsigned char)slave_buf[7], 0x81);
    ASSERT_EQ((unsigned char)slave_buf[8], 0x88);
    ASSERT_EQ(slave_buf[9], '\0');

    ASSERT_EQ(tty_master_read(tty, buf, sizeof(buf)), 19);
    ASSERT_EQ((unsigned char)buf[0], 0xe3);
    ASSERT_EQ((unsigned char)buf[1], 0x81);
    ASSERT_EQ((unsigned char)buf[2], 0x82);
    ASSERT_EQ((unsigned char)buf[3], 0xe3);
    ASSERT_EQ((unsigned char)buf[4], 0x81);
    ASSERT_EQ((unsigned char)buf[5], 0x84);
    ASSERT_EQ((unsigned char)buf[6], 0xe3);
    ASSERT_EQ((unsigned char)buf[7], 0x81);
    ASSERT_EQ((unsigned char)buf[8], 0x86);
    ASSERT_EQ(buf[9], '\b');
    ASSERT_EQ(buf[10], ' ');
    ASSERT_EQ(buf[11], '\b');
    ASSERT_EQ(buf[12], '\b');
    ASSERT_EQ(buf[13], ' ');
    ASSERT_EQ(buf[14], '\b');
    ASSERT_EQ((unsigned char)buf[15], 0xe3);
    ASSERT_EQ((unsigned char)buf[16], 0x81);
    ASSERT_EQ((unsigned char)buf[17], 0x88);
    ASSERT_EQ(buf[18], '\n');
    tty_release(tty);
}

TEST(slave_output_is_visible_from_master) {
    struct tty *tty;
    char buf[8];
    char long_out[TEST_TTY_LONG_OUTPUT];
    char long_in[TEST_TTY_LONG_OUTPUT];
    int i;

    init_tty();
    tty = tty_alloc_pty();
    ASSERT_NOT_NULL(tty);

    ASSERT_EQ(tty_slave_write(tty, "ls\n", 3), 3);
    ASSERT_EQ(tty_master_read(tty, buf, sizeof(buf)), 3);
    ASSERT_EQ(buf[0], 'l');
    ASSERT_EQ(buf[1], 's');
    ASSERT_EQ(buf[2], '\n');

    for (i = 0; i < TEST_TTY_LONG_OUTPUT; i++) {
        long_out[i] = (char)('a' + (i % 26));
    }

    ASSERT_EQ(tty_slave_write(tty, long_out, sizeof(long_out)), sizeof(long_out));
    ASSERT_EQ(tty_master_read(tty, long_in, sizeof(long_in)), sizeof(long_in));
    ASSERT_EQ(memcmp(long_in, long_out, sizeof(long_out)), 0);
    tty_release(tty);
}

TEST(input_mode_switches_between_console_and_raw) {
    init_tty();
    ASSERT_EQ(tty_set_input_mode(1), 0);
    ASSERT_EQ(tty_get_input_mode(), 1);
    ASSERT_EQ(tty_set_input_mode(0), 0);
    ASSERT_EQ(tty_get_input_mode(), 0);
}

TEST(pty_winsize_can_be_updated) {
    struct tty *tty;
    struct winsize winsize;

    init_tty();
    tty = tty_get_console();
    ASSERT_NOT_NULL(tty);

    ASSERT_EQ(tty_set_winsize(tty, 132, 43), 0);
    ASSERT_EQ(tty_get_winsize(tty, &winsize), 0);
    ASSERT_EQ(winsize.cols, 132);
    ASSERT_EQ(winsize.rows, 43);
}

TEST(termios_can_switch_console_tty_to_raw_noecho) {
    struct tty *tty;
    struct termios termios;
    char slave_buf[8];

    init_tty();
    tty = tty_get_console();
    ASSERT_NOT_NULL(tty);

    ASSERT_EQ(tty_get_termios(tty, &termios), 0);
    ASSERT_EQ(termios.c_lflag, ICANON | ECHO | ISIG);

    termios.c_lflag = 0;
    ASSERT_EQ(tty_set_termios(tty, &termios), 0);
    ASSERT_EQ(tty_set_input_mode(0), 0);
    tty_feed_console_char('a');
    tty_feed_console_char('b');
    ASSERT_EQ(tty_slave_read(tty, slave_buf, sizeof(slave_buf), 0), 2);
    ASSERT_EQ(slave_buf[0], 'a');
    ASSERT_EQ(slave_buf[1], 'b');
}

TEST(ctrl_c_discards_pending_line) {
    struct tty *tty;
    char master_buf[8];
    char slave_buf[8];
    const char intr = 0x03;

    init_tty();
    tty = tty_alloc_pty();
    ASSERT_NOT_NULL(tty);

    ASSERT_EQ(tty_master_write(tty, "abc", 3), 3);
    ASSERT_EQ(tty_master_write(tty, &intr, 1), 1);
    ASSERT_EQ(tty_slave_read(tty, slave_buf, sizeof(slave_buf), 0), 1);
    ASSERT_EQ(slave_buf[0], '\0');
    ASSERT_EQ(tty_master_read(tty, master_buf, sizeof(master_buf)), 6);
    ASSERT_EQ(master_buf[0], 'a');
    ASSERT_EQ(master_buf[1], 'b');
    ASSERT_EQ(master_buf[2], 'c');
    ASSERT_EQ(master_buf[3], '^');
    ASSERT_EQ(master_buf[4], 'C');
    ASSERT_EQ(master_buf[5], '\n');
    tty_release(tty);
}

int main(void)
{
    printf("=== tty / pty tests ===\n");

    RUN_TEST(canonical_input_waits_for_enter);
    RUN_TEST(canonical_backspace_echo_erases_on_master);
    RUN_TEST(canonical_delete_key_erases_on_master);
    RUN_TEST(canonical_utf8_backspace_erases_whole_character);
    RUN_TEST(slave_output_is_visible_from_master);
    RUN_TEST(input_mode_switches_between_console_and_raw);
    RUN_TEST(pty_winsize_can_be_updated);
    RUN_TEST(termios_can_switch_console_tty_to_raw_noecho);
    RUN_TEST(ctrl_c_discards_pending_line);

    TEST_REPORT();
}
