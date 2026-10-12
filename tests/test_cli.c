/**
 * @file test_cli.c
 * @brief Integration tests for openlogictl CLI and IPC client library.
 */

#include "ipc_client.h"
#include "ipc_server.h"
#include "yyjson.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

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
    ctx->start_time = time(NULL) - 300;
    ctx->config.schema_version = 7;

    ctx->device_count = 1;
    openlogi_device_status_t *d = &ctx->devices[0];
    strncpy(d->device_key, "unit:6be9d300", sizeof(d->device_key) - 1);
    strncpy(d->product_name, "MX Master 3S", sizeof(d->product_name) - 1);
    d->vendor_id = 0x046D;
    d->product_id = 0xB023;
    strncpy(d->serial_number, "KLM8901234", sizeof(d->serial_number) - 1);
    d->battery_percent = 90;
    d->is_charging = true;
    d->dpi = 1600;
    d->smartshift_mode = 1;
    d->smartshift_threshold = 14;
    d->online = true;

    ctx->set_dpi = mock_set_dpi;
    ctx->set_smartshift = mock_set_smartshift;
    ctx->reload_config = mock_reload_config;
}

static void test_ipc_client_unreachable(void)
{
    ipc_client_t client;
    bool connected = ipc_client_connect("/tmp/nonexistent_openlogi_sock_12345.sock", &client);
    assert(!connected && "Connecting to nonexistent socket should fail gracefully");
    assert(client.fd < 0);
    TEST_PASS("test_ipc_client_unreachable");
}

static void test_ipc_client_methods(void)
{
    openlogi_daemon_ctx_t ctx;
    setup_mock_ctx(&ctx);

    const char *sock_path = "/tmp/openlogi_test_client_m7.sock";
    assert(ipc_server_start(sock_path, &ctx));

    ipc_client_t client;
    assert(ipc_client_connect(sock_path, &client));

    /* 1. Status */
    yyjson_doc *st_doc = ipc_client_call(&client, "openlogi.get_status", "{}");
    assert(st_doc != NULL);
    yyjson_val *st_res = yyjson_obj_get(yyjson_doc_get_root(st_doc), "result");
    assert(st_res != NULL);
    assert(strcmp(yyjson_get_str(yyjson_obj_get(st_res, "version")), OPENLOGI_AGENT_VERSION) == 0);
    yyjson_doc_free(st_doc);

    /* 2. Inventory */
    yyjson_doc *inv_doc = ipc_client_call(&client, "openlogi.get_inventory", "{}");
    assert(inv_doc != NULL);
    yyjson_val *inv_res = yyjson_obj_get(yyjson_doc_get_root(inv_doc), "result");
    assert(inv_res != NULL);
    yyjson_val *devs = yyjson_obj_get(inv_res, "devices");
    assert(devs != NULL && yyjson_arr_size(devs) == 1);
    yyjson_doc_free(inv_doc);

    /* 3. Set DPI */
    yyjson_doc *dpi_doc = ipc_client_call(&client, "openlogi.set_dpi", "{\"dpi\":3200}");
    assert(dpi_doc != NULL);
    yyjson_val *dpi_res = yyjson_obj_get(yyjson_doc_get_root(dpi_doc), "result");
    assert(dpi_res != NULL && yyjson_get_bool(yyjson_obj_get(dpi_res, "success")));
    assert(ctx.devices[0].dpi == 3200);
    yyjson_doc_free(dpi_doc);

    /* 4. Set SmartShift */
    yyjson_doc *ss_doc = ipc_client_call(&client, "openlogi.set_smartshift", "{\"mode\":\"freespin\",\"threshold\":22}");
    assert(ss_doc != NULL);
    yyjson_val *ss_res = yyjson_obj_get(yyjson_doc_get_root(ss_doc), "result");
    assert(ss_res != NULL && yyjson_get_bool(yyjson_obj_get(ss_res, "success")));
    assert(ctx.devices[0].smartshift_mode == 2);
    assert(ctx.devices[0].smartshift_threshold == 22);
    yyjson_doc_free(ss_doc);

    /* 5. Reload Config */
    yyjson_doc *rc_doc = ipc_client_call(&client, "openlogi.reload_config", "{}");
    assert(rc_doc != NULL);
    yyjson_val *rc_res = yyjson_obj_get(yyjson_doc_get_root(rc_doc), "result");
    assert(rc_res != NULL && yyjson_get_bool(yyjson_obj_get(rc_res, "success")));
    yyjson_doc_free(rc_doc);

    ipc_client_close(&client);
    ipc_server_stop();

    TEST_PASS("test_ipc_client_methods");
}

static const char *get_openlogictl_path(void)
{
    if (access("bin/openlogictl", X_OK) == 0) return "bin/openlogictl";
    if (access("openlogi-c/bin/openlogictl", X_OK) == 0) return "openlogi-c/bin/openlogictl";
    return "bin/openlogictl";
}

static void test_cli_subcommands(void)
{
    openlogi_daemon_ctx_t ctx;
    setup_mock_ctx(&ctx);

    const char *sock_path = "/tmp/openlogi_test_cli_sub.sock";
    assert(ipc_server_start(sock_path, &ctx));

    const char *cli = get_openlogictl_path();
    char cmd[512];

    /* 1. openlogictl --help */
    snprintf(cmd, sizeof(cmd), "%s --help > /dev/null 2>&1", cli);
    int ret = system(cmd);
    assert(ret == 0);

    /* 2. openlogictl status */
    snprintf(cmd, sizeof(cmd), "%s -s %s status > /dev/null 2>&1", cli, sock_path);
    ret = system(cmd);
    assert(ret == 0);

    /* 3. openlogictl --json status */
    snprintf(cmd, sizeof(cmd), "%s -s %s --json status > /dev/null 2>&1", cli, sock_path);
    ret = system(cmd);
    assert(ret == 0);

    /* 4. openlogictl list */
    snprintf(cmd, sizeof(cmd), "%s -s %s list > /dev/null 2>&1", cli, sock_path);
    ret = system(cmd);
    assert(ret == 0);

    /* 5. openlogictl --json list */
    snprintf(cmd, sizeof(cmd), "%s -s %s --json list > /dev/null 2>&1", cli, sock_path);
    ret = system(cmd);
    assert(ret == 0);

    /* 6. openlogictl dpi set 4000 */
    snprintf(cmd, sizeof(cmd), "%s -s %s dpi set 4000 > /dev/null 2>&1", cli, sock_path);
    ret = system(cmd);
    assert(ret == 0);
    assert(ctx.devices[0].dpi == 4000);

    /* 7. openlogictl smartshift set ratchet 18 */
    snprintf(cmd, sizeof(cmd), "%s -s %s smartshift set ratchet 18 > /dev/null 2>&1", cli, sock_path);
    ret = system(cmd);
    assert(ret == 0);
    assert(ctx.devices[0].smartshift_mode == 1);
    assert(ctx.devices[0].smartshift_threshold == 18);

    /* 8. openlogictl reload */
    snprintf(cmd, sizeof(cmd), "%s -s %s reload > /dev/null 2>&1", cli, sock_path);
    ret = system(cmd);
    assert(ret == 0);

    /* 9. openlogictl autostart status */
    snprintf(cmd, sizeof(cmd), "%s autostart status > /dev/null 2>&1", cli);
    ret = system(cmd);
    assert(ret == 0);

    /* 10. openlogictl --json autostart status */
    snprintf(cmd, sizeof(cmd), "%s --json autostart status > /dev/null 2>&1", cli);
    ret = system(cmd);
    assert(ret == 0);

    /* 11. openlogictl devices (alias for list) */
    snprintf(cmd, sizeof(cmd), "%s -s %s devices > /dev/null 2>&1", cli, sock_path);
    ret = system(cmd);
    assert(ret == 0);

    /* 12. openlogictl actions & filter */
    snprintf(cmd, sizeof(cmd), "%s actions > /dev/null 2>&1", cli);
    ret = system(cmd);
    assert(ret == 0);

    snprintf(cmd, sizeof(cmd), "%s actions scroll > /dev/null 2>&1", cli);
    ret = system(cmd);
    assert(ret == 0);

    snprintf(cmd, sizeof(cmd), "%s --json actions > /dev/null 2>&1", cli);
    ret = system(cmd);
    assert(ret == 0);

    /* 13. openlogictl options & schema */
    snprintf(cmd, sizeof(cmd), "%s options > /dev/null 2>&1", cli);
    ret = system(cmd);
    assert(ret == 0);

    snprintf(cmd, sizeof(cmd), "%s schema > /dev/null 2>&1", cli);
    ret = system(cmd);
    assert(ret == 0);

    snprintf(cmd, sizeof(cmd), "%s --json options > /dev/null 2>&1", cli);
    ret = system(cmd);
    assert(ret == 0);

    /* 14. openlogictl scroll get */
    snprintf(cmd, sizeof(cmd), "%s scroll get > /dev/null 2>&1", cli);
    ret = system(cmd);
    assert(ret == 0);

    snprintf(cmd, sizeof(cmd), "%s --json scroll get > /dev/null 2>&1", cli);
    ret = system(cmd);
    assert(ret == 0);

    /* 15. openlogictl config path */
    snprintf(cmd, sizeof(cmd), "%s config path > /dev/null 2>&1", cli);
    ret = system(cmd);
    assert(ret == 0);

    ipc_server_stop();
    TEST_PASS("test_cli_subcommands");
}

int main(void)
{
    test_suite_begin(&g_suite, "test_cli", "CLI & IPC Client Subcommand Suite");

    TEST_CASE_RUN(g_suite, test_ipc_client_unreachable);
    TEST_CASE_RUN(g_suite, test_ipc_client_methods);
    TEST_CASE_RUN(g_suite, test_cli_subcommands);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
