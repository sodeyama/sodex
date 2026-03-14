/*
 * Unit tests for key event queue and stdin compatibility adapter.
 */
#include "test_framework.h"

typedef unsigned char u_int8_t;

struct key_event {
    u_int8_t scancode;
    u_int8_t ascii;
    u_int8_t modifiers;
    u_int8_t flags;
};

extern void init_key(void);
extern char key_handle_scancode(u_int8_t c);
extern int key_pop_event(struct key_event *event);
extern char *get_stdin(char *tobuf);

#define KEY_BACK 0x08
#define KEY_ENTER 0x0D
#define KEY_SCANCODE_EXTENDED 0xE0
#define KEY_SCANCODE_LEFT_SHIFT 0x2A
#define KEY_EVENT_RELEASE 0x01
#define KEY_EVENT_EXTENDED 0x02
#define KEY_MOD_SHIFT 0x01
#define KEY_SCANCODE_A 0x1E
#define KEY_SCANCODE_LEFT 0x4B

TEST(key_press_enqueues_event_and_stdin) {
    struct key_event event;
    char stdin_buf[65];

    init_key();
    ASSERT_EQ(key_handle_scancode(KEY_SCANCODE_A), 'a');
    ASSERT(key_pop_event(&event));
    ASSERT_EQ(event.scancode, KEY_SCANCODE_A);
    ASSERT_EQ(event.ascii, 'a');
    ASSERT_EQ(event.flags, 0);
    ASSERT_STR_EQ(get_stdin(stdin_buf), "a");
}

TEST(shift_changes_ascii_and_modifier) {
    struct key_event event;
    char stdin_buf[65];

    init_key();
    ASSERT_EQ(key_handle_scancode(KEY_SCANCODE_LEFT_SHIFT), 0);
    ASSERT(key_pop_event(&event));
    ASSERT_EQ(event.modifiers, KEY_MOD_SHIFT);
    ASSERT_EQ(event.ascii, 0);

    ASSERT_EQ(key_handle_scancode(KEY_SCANCODE_A), 'A');
    ASSERT(key_pop_event(&event));
    ASSERT_EQ(event.ascii, 'A');
    ASSERT_EQ(event.modifiers, KEY_MOD_SHIFT);
    ASSERT_STR_EQ(get_stdin(stdin_buf), "A");
}

TEST(release_event_does_not_enqueue_stdin) {
    struct key_event event;
    char stdin_buf[65];

    init_key();
    ASSERT_EQ(key_handle_scancode(KEY_SCANCODE_A | 0x80), 0);
    ASSERT(key_pop_event(&event));
    ASSERT_EQ(event.flags, KEY_EVENT_RELEASE);
    ASSERT_EQ(event.ascii, 0);
    ASSERT_NULL(get_stdin(stdin_buf));
}

TEST(enter_and_backspace_are_preserved_for_compat) {
    char stdin_buf[65];

    init_key();
    ASSERT_EQ(key_handle_scancode(0x0E), KEY_BACK);
    ASSERT_EQ(key_handle_scancode(0x1C), KEY_ENTER);
    ASSERT_NOT_NULL(get_stdin(stdin_buf));
    ASSERT_EQ(stdin_buf[0], KEY_BACK);
    ASSERT_EQ(stdin_buf[1], KEY_ENTER);
    ASSERT_EQ(stdin_buf[2], '\0');
}

TEST(extended_key_generates_raw_event_only) {
    struct key_event event;
    char stdin_buf[65];

    init_key();
    ASSERT_EQ(key_handle_scancode(KEY_SCANCODE_EXTENDED), 0);
    ASSERT_EQ(key_handle_scancode(KEY_SCANCODE_LEFT), 0);
    ASSERT(key_pop_event(&event));
    ASSERT_EQ(event.scancode, KEY_SCANCODE_LEFT);
    ASSERT_EQ(event.flags, KEY_EVENT_EXTENDED);
    ASSERT_EQ(event.ascii, 0);
    ASSERT_NULL(get_stdin(stdin_buf));
}

int main(void)
{
    printf("=== key event tests ===\n");

    RUN_TEST(key_press_enqueues_event_and_stdin);
    RUN_TEST(shift_changes_ascii_and_modifier);
    RUN_TEST(release_event_does_not_enqueue_stdin);
    RUN_TEST(enter_and_backspace_are_preserved_for_compat);
    RUN_TEST(extended_key_generates_raw_event_only);

    TEST_REPORT();
}
