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

int files_alloc_fd(struct files_struct *files, struct file *file) {
    static int next_fd = 0;
    (void)files;
    (void)file;
    return next_fd++;
}

extern void init_tty(void);
extern struct tty *tty_get_console(void);
extern struct tty *tty_alloc_pty(void);
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
    tty = tty_alloc_pty();
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
    ASSERT_EQ(termios.c_lflag, ICANON | ECHO);

    termios.c_lflag = 0;
    ASSERT_EQ(tty_set_termios(tty, &termios), 0);
    ASSERT_EQ(tty_set_input_mode(0), 0);
    tty_feed_console_char('a');
    tty_feed_console_char('b');
    ASSERT_EQ(tty_slave_read(tty, slave_buf, sizeof(slave_buf), 0), 2);
    ASSERT_EQ(slave_buf[0], 'a');
    ASSERT_EQ(slave_buf[1], 'b');
}

int main(void)
{
    printf("=== tty / pty tests ===\n");

    RUN_TEST(canonical_input_waits_for_enter);
    RUN_TEST(canonical_backspace_echo_erases_on_master);
    RUN_TEST(slave_output_is_visible_from_master);
    RUN_TEST(input_mode_switches_between_console_and_raw);
    RUN_TEST(pty_winsize_can_be_updated);
    RUN_TEST(termios_can_switch_console_tty_to_raw_noecho);

    TEST_REPORT();
}
