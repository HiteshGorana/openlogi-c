/**
 * @file ipc_server.h
 * @brief High-performance UNIX domain socket JSON-RPC 2.0 server for OpenLogi daemon.
 */

#ifndef OPENLOGI_IPC_SERVER_H
#define OPENLOGI_IPC_SERVER_H

#include "config.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENLOGI_PROTOCOL_VERSION "2.0"
#ifndef OPENLOGI_AGENT_VERSION
#define OPENLOGI_AGENT_VERSION    "0.1.3-c"
#endif
#define OPENLOGI_MAX_CLIENTS      16

typedef struct {
    char device_key[128];
    char product_name[128];
    uint16_t vendor_id;
    uint16_t product_id;
    char serial_number[64];
    uint8_t battery_percent;
    bool is_charging;
    uint16_t dpi;
    uint8_t smartshift_mode;
    uint8_t smartshift_threshold;
    bool online;
} openlogi_device_status_t;

typedef struct openlogi_daemon_ctx_s {
    time_t start_time;
    openlogi_config_t config;

    /* Hardware state */
    openlogi_device_status_t devices[OPENLOGI_MAX_DEVICES];
    size_t device_count;

    /* Hardware & config control callbacks */
    bool (*set_dpi)(struct openlogi_daemon_ctx_s *ctx, const char *device_key, uint16_t dpi);
    bool (*set_smartshift)(struct openlogi_daemon_ctx_s *ctx, const char *device_key, uint8_t mode, uint8_t threshold);
    bool (*reload_config)(struct openlogi_daemon_ctx_s *ctx);
    void *user_data;
} openlogi_daemon_ctx_t;

/**
 * @brief Get the default OpenLogi IPC socket path (~/.local/state/openlogi/openlogi.sock).
 */
bool ipc_get_default_socket_path(char *out_path, size_t max_len);

/**
 * @brief Start the UNIX domain socket JSON-RPC server on a background thread.
 * @param socket_path Path to socket file.
 * @param ctx Daemon context for querying state and executing RPC actions.
 * @return true on success, false on error.
 */
bool ipc_server_start(const char *socket_path, openlogi_daemon_ctx_t *ctx);

/**
 * @brief Stop the IPC server and remove socket file.
 */
void ipc_server_stop(void);

/**
 * @brief Check if the IPC server is currently running.
 */
bool ipc_server_is_running(void);

/**
 * @brief Broadcast a push event notification to all subscribed clients.
 * @param event_name e.g. "device_change", "battery_update", "dpi_change".
 * @param json_payload JSON string containing event fields, or NULL.
 */
void ipc_server_broadcast_event(const char *event_name, const char *json_payload);

/**
 * @brief In-memory dispatch of a JSON-RPC 2.0 request string.
 * @param ctx Daemon context.
 * @param request_json Input JSON-RPC request string.
 * @return Allocated response JSON string (must be freed by caller), or NULL on memory error.
 */
char *ipc_dispatch_jsonrpc(openlogi_daemon_ctx_t *ctx, const char *request_json);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_IPC_SERVER_H */
