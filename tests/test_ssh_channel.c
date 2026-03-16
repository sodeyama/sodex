#include "test_framework.h"

#include "../src/include/ssh_channel_core.h"
#include "../src/include/ssh_packet_core.h"

#include <string.h>

#define SSH_MSG_CHANNEL_OPEN 90
#define SSH_MSG_CHANNEL_DATA 94
#define SSH_MSG_CHANNEL_REQUEST 98

static int build_channel_open(u_int8_t *buf, int cap)
{
    struct ssh_writer writer;

    ssh_writer_init(&writer, buf, cap);
    ssh_writer_put_byte(&writer, SSH_MSG_CHANNEL_OPEN);
    ssh_writer_put_cstring(&writer, "session");
    ssh_writer_put_u32(&writer, 7);
    ssh_writer_put_u32(&writer, 65535);
    ssh_writer_put_u32(&writer, 1024);
    return writer.error ? -1 : writer.len;
}

static int build_channel_request(u_int8_t *buf, int cap,
                                 const char *request_name,
                                 int want_reply,
                                 u_int16_t cols, u_int16_t rows)
{
    struct ssh_writer writer;

    ssh_writer_init(&writer, buf, cap);
    ssh_writer_put_byte(&writer, SSH_MSG_CHANNEL_REQUEST);
    ssh_writer_put_u32(&writer, 0);
    ssh_writer_put_cstring(&writer, request_name);
    ssh_writer_put_bool(&writer, want_reply);
    if (strcmp(request_name, "pty-req") == 0) {
        ssh_writer_put_cstring(&writer, "xterm");
        ssh_writer_put_u32(&writer, cols);
        ssh_writer_put_u32(&writer, rows);
        ssh_writer_put_u32(&writer, 0);
        ssh_writer_put_u32(&writer, 0);
        ssh_writer_put_cstring(&writer, "");
    } else if (strcmp(request_name, "window-change") == 0) {
        ssh_writer_put_u32(&writer, cols);
        ssh_writer_put_u32(&writer, rows);
        ssh_writer_put_u32(&writer, 0);
        ssh_writer_put_u32(&writer, 0);
    }
    return writer.error ? -1 : writer.len;
}

static int build_channel_data(u_int8_t *buf, int cap, const char *text)
{
    struct ssh_writer writer;

    ssh_writer_init(&writer, buf, cap);
    ssh_writer_put_byte(&writer, SSH_MSG_CHANNEL_DATA);
    ssh_writer_put_u32(&writer, 0);
    ssh_writer_put_cstring(&writer, text);
    return writer.error ? -1 : writer.len;
}

TEST(parse_session_open_request) {
    u_int8_t payload[64];
    struct ssh_channel_open_request request;
    int payload_len = build_channel_open(payload, sizeof(payload));

    ASSERT(payload_len > 0);
    ASSERT_EQ(ssh_channel_parse_open(payload, payload_len, &request), 0);
    ASSERT_STR_EQ(request.type, "session");
    ASSERT_EQ(request.peer_id, 7U);
    ASSERT_EQ(request.peer_window, 65535U);
    ASSERT_EQ(request.peer_max_packet, 1024U);
}

TEST(parse_pty_request_and_data) {
    u_int8_t payload[128];
    struct ssh_channel_request request;
    struct ssh_channel_data_request data_request;
    int payload_len = build_channel_request(payload, sizeof(payload),
                                            "pty-req", 1, 120, 40);

    ASSERT(payload_len > 0);
    ASSERT_EQ(ssh_channel_parse_request(payload, payload_len, &request), 0);
    ASSERT_EQ(request.kind, SSH_CHANNEL_REQUEST_PTY);
    ASSERT_EQ(request.want_reply, 1);
    ASSERT_EQ(request.cols, 120);
    ASSERT_EQ(request.rows, 40);

    payload_len = build_channel_data(payload, sizeof(payload), "ls\n");
    ASSERT(payload_len > 0);
    ASSERT_EQ(ssh_channel_parse_data(payload, payload_len, &data_request), 0);
    ASSERT_EQ(data_request.recipient, 0U);
    ASSERT_EQ(data_request.data_len, 3);
    ASSERT_EQ(data_request.data[0], 'l');
    ASSERT_EQ(data_request.data[1], 's');
    ASSERT_EQ(data_request.data[2], '\n');
}

TEST(close_plan_sends_remaining_messages_in_order) {
    struct ssh_channel_close_plan plan;

    ssh_channel_plan_shutdown(0, 0, 0, &plan);
    ASSERT_EQ(plan.send_exit_status, 1);
    ASSERT_EQ(plan.send_eof, 1);
    ASSERT_EQ(plan.send_close, 1);

    ssh_channel_plan_shutdown(1, 0, 0, &plan);
    ASSERT_EQ(plan.send_exit_status, 0);
    ASSERT_EQ(plan.send_eof, 1);
    ASSERT_EQ(plan.send_close, 1);
}

int main(void) {
    RUN_TEST(parse_session_open_request);
    RUN_TEST(parse_pty_request_and_data);
    RUN_TEST(close_plan_sends_remaining_messages_in_order);
    TEST_REPORT();
}
