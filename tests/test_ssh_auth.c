#include "test_framework.h"

#include "../src/include/ssh_auth_core.h"
#include "../src/include/ssh_packet_core.h"

#define SSH_MSG_SERVICE_REQUEST 5
#define SSH_MSG_USERAUTH_REQUEST 50

static int build_service_request(u_int8_t *buf, int cap, const char *service_name)
{
    struct ssh_writer writer;

    ssh_writer_init(&writer, buf, cap);
    ssh_writer_put_byte(&writer, SSH_MSG_SERVICE_REQUEST);
    ssh_writer_put_cstring(&writer, service_name);
    return writer.error ? -1 : writer.len;
}

static int build_password_request(u_int8_t *buf, int cap,
                                  const char *username,
                                  const char *service,
                                  const char *method,
                                  int change_request,
                                  const char *password)
{
    struct ssh_writer writer;

    ssh_writer_init(&writer, buf, cap);
    ssh_writer_put_byte(&writer, SSH_MSG_USERAUTH_REQUEST);
    ssh_writer_put_cstring(&writer, username);
    ssh_writer_put_cstring(&writer, service);
    ssh_writer_put_cstring(&writer, method);
    ssh_writer_put_bool(&writer, change_request);
    ssh_writer_put_cstring(&writer, password);
    return writer.error ? -1 : writer.len;
}

static int build_auth_request(u_int8_t *buf, int cap,
                              const char *username,
                              const char *service,
                              const char *method)
{
    struct ssh_writer writer;

    ssh_writer_init(&writer, buf, cap);
    ssh_writer_put_byte(&writer, SSH_MSG_USERAUTH_REQUEST);
    ssh_writer_put_cstring(&writer, username);
    ssh_writer_put_cstring(&writer, service);
    ssh_writer_put_cstring(&writer, method);
    return writer.error ? -1 : writer.len;
}

TEST(parse_service_request) {
    u_int8_t payload[64];
    char service_name[SSH_AUTH_TEXT_MAX];
    int payload_len = build_service_request(payload, sizeof(payload), "ssh-userauth");

    ASSERT(payload_len > 0);
    ASSERT_EQ(ssh_auth_parse_service_request(payload, payload_len,
                                             service_name, sizeof(service_name)), 0);
    ASSERT_STR_EQ(service_name, "ssh-userauth");
}

TEST(parse_password_request_and_match_password) {
    u_int8_t payload[128];
    struct ssh_auth_request request;
    int payload_len = build_password_request(payload, sizeof(payload),
                                             "root", "ssh-connection",
                                             "password", 0, "root-secret");

    ASSERT(payload_len > 0);
    ASSERT_EQ(ssh_auth_parse_request(payload, payload_len, &request), 0);
    ASSERT_STR_EQ(request.username, "root");
    ASSERT_STR_EQ(request.service, "ssh-connection");
    ASSERT_STR_EQ(request.method, "password");
    ASSERT_EQ(ssh_auth_password_request_matches(&request,
                                                "root",
                                                "ssh-connection",
                                                "root-secret"), 1);
    ASSERT_EQ(ssh_auth_password_request_matches(&request,
                                                "root",
                                                "ssh-connection",
                                                "wrong"), 0);
}

TEST(parse_none_request_without_protocol_error) {
    u_int8_t payload[128];
    struct ssh_auth_request request;
    int payload_len = build_auth_request(payload, sizeof(payload),
                                         "root", "ssh-connection", "none");

    ASSERT(payload_len > 0);
    ASSERT_EQ(ssh_auth_parse_request(payload, payload_len, &request), 0);
    ASSERT_STR_EQ(request.username, "root");
    ASSERT_STR_EQ(request.service, "ssh-connection");
    ASSERT_STR_EQ(request.method, "none");
    ASSERT_EQ(request.password_len, 0);
    ASSERT_EQ(ssh_auth_password_request_matches(&request,
                                                "root",
                                                "ssh-connection",
                                                "root-secret"), 0);
}

TEST(identity_is_pinned_after_first_request) {
    u_int8_t payload[128];
    struct ssh_auth_request request;
    struct ssh_auth_identity identity;
    int payload_len = build_password_request(payload, sizeof(payload),
                                             "root", "ssh-connection",
                                             "password", 0, "root-secret");

    ssh_auth_identity_reset(&identity);
    ASSERT(payload_len > 0);
    ASSERT_EQ(ssh_auth_parse_request(payload, payload_len, &request), 0);
    ASSERT_EQ(ssh_auth_identity_capture(&identity, &request), 0);
    ASSERT_EQ(identity.set, 1);
    ASSERT_EQ(ssh_auth_identity_matches(&identity, &request), 1);

    payload_len = build_password_request(payload, sizeof(payload),
                                         "root", "other-service",
                                         "password", 0, "root-secret");
    ASSERT(payload_len > 0);
    ASSERT_EQ(ssh_auth_parse_request(payload, payload_len, &request), 0);
    ASSERT_EQ(ssh_auth_identity_matches(&identity, &request), 0);
}

int main(void) {
    RUN_TEST(parse_service_request);
    RUN_TEST(parse_password_request_and_match_password);
    RUN_TEST(parse_none_request_without_protocol_error);
    RUN_TEST(identity_is_pinned_after_first_request);
    TEST_REPORT();
}
