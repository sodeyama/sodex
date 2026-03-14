#include "test_framework.h"

typedef unsigned short u_int16_t;
typedef unsigned char u_int8_t;

struct tty_ring {
    char data[256];
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
    char canon_buf[256];
    int canon_len;
    void *slave_wait;
    void *master_wait;
};

struct winsize {
    u_int16_t cols;
    u_int16_t rows;
};

extern void init_tty(void);
extern struct tty *tty_alloc_pty(void);
extern int tty_set_winsize(struct tty *tty, u_int16_t cols, u_int16_t rows);
extern int tty_get_winsize(struct tty *tty, struct winsize *winsize);
extern int tty_set_input_mode(int mode);
extern int tty_get_input_mode(void);
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

TEST(canonical_echo_flows_to_master) {
    struct tty *tty;
    char buf[8];

    init_tty();
    tty = tty_alloc_pty();
    ASSERT_NOT_NULL(tty);

    ASSERT_EQ(tty_master_write(tty, "a", 1), 1);
    ASSERT_EQ(tty_master_write(tty, "\b", 1), 1);
    ASSERT_EQ(tty_master_write(tty, "\r", 1), 1);
    ASSERT_EQ(tty_master_read(tty, buf, sizeof(buf)), 3);
    ASSERT_EQ(buf[0], 'a');
    ASSERT_EQ(buf[1], '\b');
    ASSERT_EQ(buf[2], '\n');
}

TEST(slave_output_is_visible_from_master) {
    struct tty *tty;
    char buf[8];

    init_tty();
    tty = tty_alloc_pty();
    ASSERT_NOT_NULL(tty);

    ASSERT_EQ(tty_slave_write(tty, "ls\n", 3), 3);
    ASSERT_EQ(tty_master_read(tty, buf, sizeof(buf)), 3);
    ASSERT_EQ(buf[0], 'l');
    ASSERT_EQ(buf[1], 's');
    ASSERT_EQ(buf[2], '\n');
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

int main(void)
{
    printf("=== tty / pty tests ===\n");

    RUN_TEST(canonical_input_waits_for_enter);
    RUN_TEST(canonical_echo_flows_to_master);
    RUN_TEST(slave_output_is_visible_from_master);
    RUN_TEST(input_mode_switches_between_console_and_raw);
    RUN_TEST(pty_winsize_can_be_updated);

    TEST_REPORT();
}
