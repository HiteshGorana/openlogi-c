/**
 * @file test_ipc_server.c
 * @brief Unit tests for JSON-RPC 2.0 dispatch and UNIX socket server.
 */

#include "ipc_server.h"
#include "ipc_client.h"
#include "yyjson.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>

#define TEST_PASS(name) (void)(name)

TEST_SUITE_DECLARE(g_suite);

static bool mock_set_dpi(openlogi_daemon_ctx_t *ctx, const char *dev_key, uint16_t dpi)
{
    for (size_t i = 0; i < ctx->device_count; i++) {
        if (!dev_key || strcmp(ctx->devices[i].device_key, dev_key) == 0) {
            ctx->devices[i].dpi = dpi;
            return true;
        }
    }
    return false;
}

static bool mock_set_smartshift(openlogi_daemon_ctx_t *ctx, const char *dev_key, uint8_t mode, uint8_t thresh)
{
    for (size_t i = 0; i < ctx->device_count; i++) {
        if (!dev_key || strcmp(ctx->devices[i].device_key, dev_key) == 0) {
            ctx->devices[i].smartshift_mode = mode;
            ctx->devices[i].smartshift_threshold = thresh;
            return true;
        }
    }
    return false;
}

static bool mock_reload_config(openlogi_daemon_ctx_t *ctx)
{
    (void)ctx;
    return true;
}

static void setup_mock_ctx(openlogi_daemon_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->start_time = time(NULL) - 100; /* 100 seconds uptime */
    ctx->config.schema_version = 7;

    ctx->device_count = 1;
    openlogi_device_status_t *d = &ctx->devices[0];
    strncpy(d->device_key, "unit:6be9d300", sizeof(d->device_key) - 1);
    strncpy(d->product_name, "MX Master 3S", sizeof(d->product_name) - 1);
    d->vendor_id = 0x046D;
    d->product_id = 0xB023;
    strncpy(d->serial_number, "1234ABCDEF", sizeof(d->serial_number) - 1);
    d->battery_percent = 85;
    d->is_charging = false;
    d->dpi = 1600;
    d->smartshift_mode = 1; /* ratchet */
    d->smartshift_threshold = 14;
    d->online = true;

    ctx->set_dpi = mock_set_dpi;
    ctx->set_smartshift = mock_set_smartshift;
    ctx->reload_config = mock_reload_config;
}

static void test_jsonrpc_dispatch_get_status(void)
{
    openlogi_daemon_ctx_t ctx;
    setup_mock_ctx(&ctx);

    const char *req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"openlogi.get_status\",\"params\":{}}";
    char *resp = ipc_dispatch_jsonrpc(&ctx, req);
    assert(resp != NULL);

    yyjson_doc *doc = yyjson_read(resp, strlen(resp), 0);
    assert(doc != NULL);
    yyjson_val *root = yyjson_doc_get_root(doc);

    assert(strcmp(yyjson_get_str(yyjson_obj_get(root, "jsonrpc")), "2.0") == 0);
    assert(yyjson_get_int(yyjson_obj_get(root, "id")) == 1);

    yyjson_val *res = yyjson_obj_get(root, "result");
    assert(res != NULL);
    assert(strcmp(yyjson_get_str(yyjson_obj_get(res, "version")), OPENLOGI_AGENT_VERSION) == 0);
    assert(strcmp(yyjson_get_str(yyjson_obj_get(res, "protocol")), OPENLOGI_PROTOCOL_VERSION) == 0);
    assert(yyjson_get_uint(yyjson_obj_get(res, "pid")) > 0);
    assert(yyjson_get_uint(yyjson_obj_get(res, "uptime_sec")) >= 100);
    assert(yyjson_get_uint(yyjson_obj_get(res, "devices_online")) == 1);

    yyjson_val *perms = yyjson_obj_get(res, "permissions");
    assert(perms != NULL);
    assert(yyjson_is_bool(yyjson_obj_get(perms, "accessibility")));
    assert(yyjson_is_bool(yyjson_obj_get(perms, "input_monitoring")));

    yyjson_doc_free(doc);
    free(resp);
    TEST_PASS("test_jsonrpc_dispatch_get_status");
}

static void test_jsonrpc_dispatch_get_inventory(void)
{
    openlogi_daemon_ctx_t ctx;
    setup_mock_ctx(&ctx);

    const char *req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"openlogi.get_inventory\",\"params\":{}}";
    char *resp = ipc_dispatch_jsonrpc(&ctx, req);
    assert(resp != NULL);

    yyjson_doc *doc = yyjson_read(resp, strlen(resp), 0);
    assert(doc != NULL);
    yyjson_val *root = yyjson_doc_get_root(doc);

    yyjson_val *res = yyjson_obj_get(root, "result");
    assert(res != NULL);
    yyjson_val *devs = yyjson_obj_get(res, "devices");
    assert(devs != NULL && yyjson_is_arr(devs));
    assert(yyjson_arr_size(devs) == 1);

    yyjson_val *d0 = yyjson_arr_get(devs, 0);
    assert(strcmp(yyjson_get_str(yyjson_obj_get(d0, "device_key")), "unit:6be9d300") == 0);
    assert(strcmp(yyjson_get_str(yyjson_obj_get(d0, "product_name")), "MX Master 3S") == 0);
    assert(strcmp(yyjson_get_str(yyjson_obj_get(d0, "vendor_id")), "0x046D") == 0);
    assert(strcmp(yyjson_get_str(yyjson_obj_get(d0, "product_id")), "0xB023") == 0);
    assert(yyjson_get_uint(yyjson_obj_get(d0, "battery_percent")) == 85);
    assert(yyjson_get_uint(yyjson_obj_get(d0, "dpi")) == 1600);
    assert(yyjson_get_uint(yyjson_obj_get(d0, "smartshift_mode")) == 1);
    assert(yyjson_get_uint(yyjson_obj_get(d0, "smartshift_threshold")) == 14);

    yyjson_doc_free(doc);
    free(resp);
    TEST_PASS("test_jsonrpc_dispatch_get_inventory");
}

static void test_jsonrpc_dispatch_set_dpi_and_smartshift(void)
{
    openlogi_daemon_ctx_t ctx;
    setup_mock_ctx(&ctx);

    /* 1. Set DPI */
    const char *req_dpi = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"openlogi.set_dpi\",\"params\":{\"dpi\":2400}}";
    char *resp_dpi = ipc_dispatch_jsonrpc(&ctx, req_dpi);
    assert(resp_dpi != NULL);

    yyjson_doc *doc_dpi = yyjson_read(resp_dpi, strlen(resp_dpi), 0);
    assert(doc_dpi != NULL);
    yyjson_val *res_dpi = yyjson_obj_get(yyjson_doc_get_root(doc_dpi), "result");
    assert(res_dpi != NULL);
    assert(yyjson_get_bool(yyjson_obj_get(res_dpi, "success")) == true);
    assert(yyjson_get_uint(yyjson_obj_get(res_dpi, "dpi")) == 2400);
    assert(ctx.devices[0].dpi == 2400);
    yyjson_doc_free(doc_dpi);
    free(resp_dpi);

    /* 2. Set SmartShift to freespin */
    const char *req_ss = "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"openlogi.set_smartshift\",\"params\":{\"mode\":\"freespin\",\"threshold\":25}}";
    char *resp_ss = ipc_dispatch_jsonrpc(&ctx, req_ss);
    assert(resp_ss != NULL);

    yyjson_doc *doc_ss = yyjson_read(resp_ss, strlen(resp_ss), 0);
    assert(doc_ss != NULL);
    yyjson_val *res_ss = yyjson_obj_get(yyjson_doc_get_root(doc_ss), "result");
    assert(res_ss != NULL);
    assert(yyjson_get_bool(yyjson_obj_get(res_ss, "success")) == true);
    assert(yyjson_get_uint(yyjson_obj_get(res_ss, "mode")) == 2);
    assert(yyjson_get_uint(yyjson_obj_get(res_ss, "threshold")) == 25);
    assert(ctx.devices[0].smartshift_mode == 2);
    assert(ctx.devices[0].smartshift_threshold == 25);
    yyjson_doc_free(doc_ss);
    free(resp_ss);

    /* 3. Reload Config */
    const char *req_rc = "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"openlogi.reload_config\",\"params\":{}}";
    char *resp_rc = ipc_dispatch_jsonrpc(&ctx, req_rc);
    assert(resp_rc != NULL);

    yyjson_doc *doc_rc = yyjson_read(resp_rc, strlen(resp_rc), 0);
    assert(doc_rc != NULL);
    yyjson_val *res_rc = yyjson_obj_get(yyjson_doc_get_root(doc_rc), "result");
    assert(res_rc != NULL);
    assert(yyjson_get_bool(yyjson_obj_get(res_rc, "success")) == true);
    assert(yyjson_get_uint(yyjson_obj_get(res_rc, "schema_version")) == 7);
    yyjson_doc_free(doc_rc);
    free(resp_rc);

    TEST_PASS("test_jsonrpc_dispatch_set_dpi_and_smartshift");
}

static void test_jsonrpc_dispatch_errors(void)
{
    openlogi_daemon_ctx_t ctx;
    setup_mock_ctx(&ctx);

    /* Case A: Malformed JSON */
    char *r1 = ipc_dispatch_jsonrpc(&ctx, "invalid json");
    assert(r1 != NULL);
    yyjson_doc *d1 = yyjson_read(r1, strlen(r1), 0);
    assert(d1 != NULL);
    yyjson_val *err1 = yyjson_obj_get(yyjson_doc_get_root(d1), "error");
    assert(err1 != NULL);
    assert(yyjson_get_int(yyjson_obj_get(err1, "code")) == -32700); /* Parse error */
    yyjson_doc_free(d1);
    free(r1);

    /* Case B: Method not found */
    char *r2 = ipc_dispatch_jsonrpc(&ctx, "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"unknown.method\"}");
    assert(r2 != NULL);
    yyjson_doc *d2 = yyjson_read(r2, strlen(r2), 0);
    assert(d2 != NULL);
    yyjson_val *err2 = yyjson_obj_get(yyjson_doc_get_root(d2), "error");
    assert(err2 != NULL);
    assert(yyjson_get_int(yyjson_obj_get(err2, "code")) == -32601); /* Method not found */
    yyjson_doc_free(d2);
    free(r2);

    /* Case C: Invalid params for set_dpi */
    char *r3 = ipc_dispatch_jsonrpc(&ctx, "{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"openlogi.set_dpi\",\"params\":{}}");
    assert(r3 != NULL);
    yyjson_doc *d3 = yyjson_read(r3, strlen(r3), 0);
    assert(d3 != NULL);
    yyjson_val *err3 = yyjson_obj_get(yyjson_doc_get_root(d3), "error");
    assert(err3 != NULL);
    assert(yyjson_get_int(yyjson_obj_get(err3, "code")) == -32602); /* Invalid params */
    yyjson_doc_free(d3);
    free(r3);

    TEST_PASS("test_jsonrpc_dispatch_errors");
}

static void test_socket_server_end_to_end(void)
{
    openlogi_daemon_ctx_t ctx;
    setup_mock_ctx(&ctx);

    const char *test_sock = "/tmp/openlogi_test_m6.sock";

    /* Start server */
    bool started = ipc_server_start(test_sock, &ctx);
    assert(started && "ipc_server_start should succeed");
    assert(ipc_server_is_running());

    /* Wait a moment for server thread */
    usleep(20000);

    /* Connect test client */
    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(client_fd >= 0);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, test_sock, sizeof(addr.sun_path) - 1);

    int conn_res = connect(client_fd, (struct sockaddr *)&addr, sizeof(addr));
    assert(conn_res == 0 && "connect to IPC socket should succeed");

    /* Send request with trailing newline */
    const char *client_req = "{\"jsonrpc\":\"2.0\",\"id\":999,\"method\":\"openlogi.get_status\",\"params\":{}}\n";
    ssize_t written = write(client_fd, client_req, strlen(client_req));
    assert(written == (ssize_t)strlen(client_req));

    /* Read response */
    char resp_buf[4096] = {0};
    ssize_t bytes_read = 0;
    for (int retry = 0; retry < 20; retry++) {
        ssize_t n = read(client_fd, resp_buf + bytes_read, sizeof(resp_buf) - bytes_read - 1);
        if (n > 0) {
            bytes_read += n;
            if (strchr(resp_buf, '\n')) break;
        }
        usleep(10000);
    }
    assert(bytes_read > 0 && "Should receive JSON-RPC response from socket");

    /* Parse response */
    yyjson_doc *doc = yyjson_read(resp_buf, strlen(resp_buf), 0);
    assert(doc != NULL);
    yyjson_val *root = yyjson_doc_get_root(doc);
    assert(yyjson_get_int(yyjson_obj_get(root, "id")) == 999);
    yyjson_val *res = yyjson_obj_get(root, "result");
    assert(res != NULL);
    assert(strcmp(yyjson_get_str(yyjson_obj_get(res, "version")), OPENLOGI_AGENT_VERSION) == 0);
    yyjson_doc_free(doc);

    /* Test event broadcasting to subscribed client */
    const char *sub_req = "{\"jsonrpc\":\"2.0\",\"id\":1000,\"method\":\"openlogi.subscribe\",\"params\":{}}\n";
    (void)write(client_fd, sub_req, strlen(sub_req));
    usleep(20000); /* Wait for server to process */

    /* Consume subscribe response */
    char sub_resp[512] = {0};
    (void)read(client_fd, sub_resp, sizeof(sub_resp));

    /* Broadcast event */
    ipc_server_broadcast_event("dpi_change", "{\"device_key\":\"unit:6be9d300\",\"dpi\":3200}");
    usleep(30000);

    /* Read broadcasted event */
    char event_buf[512] = {0};
    ssize_t ev_bytes = read(client_fd, event_buf, sizeof(event_buf) - 1);
    assert(ev_bytes > 0 && "Subscribed client should receive broadcast event");

    yyjson_doc *ev_doc = yyjson_read(event_buf, strlen(event_buf), 0);
    assert(ev_doc != NULL);
    yyjson_val *ev_root = yyjson_doc_get_root(ev_doc);
    assert(strcmp(yyjson_get_str(yyjson_obj_get(ev_root, "method")), "openlogi.event") == 0);
    yyjson_val *ev_params = yyjson_obj_get(ev_root, "params");
    assert(strcmp(yyjson_get_str(yyjson_obj_get(ev_params, "event")), "dpi_change") == 0);
    yyjson_doc_free(ev_doc);

    close(client_fd);

    /* Stop server */
    ipc_server_stop();
    assert(!ipc_server_is_running());

    /* Verify socket unlinked */
    assert(access(test_sock, F_OK) != 0 && "Socket file must be unlinked on shutdown");

    TEST_PASS("test_socket_server_end_to_end");
}

static void test_ipc_client_large_payload(void)
{
    openlogi_daemon_ctx_t ctx;
    setup_mock_ctx(&ctx);

    const char *test_sock = "/tmp/test_openlogi_ipc_client.sock";
    unlink(test_sock);

    bool started = ipc_server_start(test_sock, &ctx);
    assert(started);
    usleep(20000);

    ipc_client_t client;
    bool connected = ipc_client_connect(test_sock, &client);
    assert(connected);

    /* Construct a large parameters payload > 256 bytes (e.g. 512 bytes padding) */
    char large_params[1024];
    char padding[512];
    memset(padding, 'a', sizeof(padding) - 1);
    padding[sizeof(padding) - 1] = '\0';
    snprintf(large_params, sizeof(large_params), "{\"padding\":\"%s\"}", padding);
    assert(strlen(large_params) > 500);

    /* Call openlogi.get_status with large payload */
    yyjson_doc *doc = ipc_client_call(&client, "openlogi.get_status", large_params);
    assert(doc != NULL && "Large payload RPC call should succeed without crash or truncation");

    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *res = yyjson_obj_get(root, "result");
    assert(res != NULL);
    assert(strcmp(yyjson_get_str(yyjson_obj_get(res, "version")), OPENLOGI_AGENT_VERSION) == 0);
    yyjson_doc_free(doc);

    ipc_client_close(&client);
    ipc_server_stop();
    unlink(test_sock);

    TEST_PASS("test_ipc_client_large_payload");
}

int main(void)
{
    test_suite_begin(&g_suite, "test_ipc_server", "JSON-RPC 2.0 UNIX Domain Socket Server");

    TEST_CASE_RUN(g_suite, test_jsonrpc_dispatch_get_status);
    TEST_CASE_RUN(g_suite, test_jsonrpc_dispatch_get_inventory);
    TEST_CASE_RUN(g_suite, test_jsonrpc_dispatch_set_dpi_and_smartshift);
    TEST_CASE_RUN(g_suite, test_jsonrpc_dispatch_errors);
    TEST_CASE_RUN(g_suite, test_socket_server_end_to_end);
    TEST_CASE_RUN(g_suite, test_ipc_client_large_payload);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
