#include "test_framework.h"

#include "../src/include/ssh_packet_core.h"

#define SSH_MSG_USERAUTH_SUCCESS 52

TEST(writer_reader_roundtrip) {
    u_int8_t buf[128];
    const u_int8_t *text = 0;
    int text_len = 0;
    struct ssh_writer writer;
    struct ssh_reader reader;

    ssh_writer_init(&writer, buf, sizeof(buf));
    ssh_writer_put_byte(&writer, SSH_MSG_USERAUTH_SUCCESS);
    ssh_writer_put_u32(&writer, 0x11223344U);
    ssh_writer_put_cstring(&writer, "root");
    ssh_writer_put_mpint(&writer, (const u_int8_t *)"\x01\x02", 2);
    ASSERT_EQ(writer.error, 0);

    ssh_reader_init(&reader, buf, writer.len);
    ASSERT_EQ(ssh_reader_get_byte(&reader), SSH_MSG_USERAUTH_SUCCESS);
    ASSERT_EQ(ssh_reader_get_u32(&reader), 0x11223344U);
    ssh_reader_get_string(&reader, &text, &text_len);
    ASSERT_EQ(text_len, 4);
    ASSERT_EQ(text[0], 'r');
    ASSERT_EQ(text[1], 'o');
    ASSERT_EQ(text[2], 'o');
    ASSERT_EQ(text[3], 't');
    ssh_reader_get_string(&reader, &text, &text_len);
    ASSERT_EQ(text_len, 2);
    ASSERT_EQ(text[0], 0x01);
    ASSERT_EQ(text[1], 0x02);
    ASSERT_EQ(reader.error, 0);
}

TEST(namelist_matches_middle_entry) {
    static const u_int8_t list[] = "curve25519-sha256,aes128-ctr,hmac-sha2-256";

    ASSERT_EQ(ssh_namelist_has(list, (int)sizeof(list) - 1, "aes128-ctr"), 1);
    ASSERT_EQ(ssh_namelist_has(list, (int)sizeof(list) - 1, "ssh-ed25519"), 0);
}

TEST(plain_packet_decode_consumes_buffer) {
    u_int8_t rx_buf[] = {
        0x00, 0x00, 0x00, 0x08,
        0x04,
        0x34, 0x12, 0xab,
        0x00, 0x00, 0x00, 0x00
    };
    u_int8_t payload[16];
    int rx_len = (int)sizeof(rx_buf);
    int payload_len = 0;
    u_int32_t rx_seq = 0;

    ASSERT_EQ(ssh_try_decode_plain_packet_buffer(rx_buf, &rx_len, &rx_seq,
                                                 64, payload, &payload_len), 1);
    ASSERT_EQ(payload_len, 3);
    ASSERT_EQ(payload[0], 0x34);
    ASSERT_EQ(payload[1], 0x12);
    ASSERT_EQ(payload[2], 0xab);
    ASSERT_EQ(rx_len, 0);
    ASSERT_EQ(rx_seq, 1U);
}

TEST(plain_packet_decode_rejects_invalid_padding) {
    u_int8_t rx_buf[] = {
        0x00, 0x00, 0x00, 0x06,
        0x03,
        0x34, 0x12,
        0x00, 0x00, 0x00
    };
    u_int8_t payload[16];
    int rx_len = (int)sizeof(rx_buf);
    int payload_len = 0;
    u_int32_t rx_seq = 0;

    ASSERT_EQ(ssh_try_decode_plain_packet_buffer(rx_buf, &rx_len, &rx_seq,
                                                 64, payload, &payload_len), -1);
}

int main(void) {
    RUN_TEST(writer_reader_roundtrip);
    RUN_TEST(namelist_matches_middle_entry);
    RUN_TEST(plain_packet_decode_consumes_buffer);
    RUN_TEST(plain_packet_decode_rejects_invalid_padding);
    TEST_REPORT();
}
