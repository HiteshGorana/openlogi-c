/**
 * @file ipc_client.h
 * @brief Lightweight UNIX domain socket JSON-RPC 2.0 client for OpenLogi.
 */

#ifndef OPENLOGI_IPC_CLIENT_H
#define OPENLOGI_IPC_CLIENT_H

#include "yyjson.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int fd;
    char socket_path[512];
    uint64_t next_id;
} ipc_client_t;

/**
 * @brief Connect to OpenLogi daemon.
 * @param socket_path Path to socket or NULL for default path.
 * @param out_client Initialized client struct.
 * @return true on success, false if daemon is not running or unreachable.
 */
bool ipc_client_connect(const char *socket_path, ipc_client_t *out_client);

/**
 * @brief Close connection to OpenLogi daemon.
 */
void ipc_client_close(ipc_client_t *client);

/**
 * @brief Send a JSON-RPC request and await response.
 * @param client Connected client.
 * @param method RPC method name.
 * @param params_json JSON string of parameters (e.g. "{\"dpi\":1600}"), or NULL for "{}".
 * @return Parsed response document (caller must free with yyjson_doc_free), or NULL on communication error.
 */
yyjson_doc *ipc_client_call(ipc_client_t *client, const char *method, const char *params_json);

/**
 * @brief Send a raw JSON-RPC request and return raw response string.
 * @param client Connected client.
 * @param method RPC method name.
 * @param params_json JSON string of parameters.
 * @return Dynamically allocated response string (caller frees with free()), or NULL on error.
 */
char *ipc_client_call_raw(ipc_client_t *client, const char *method, const char *params_json);

/**
 * @brief Read next streaming notification event (for 'monitor' subcommand).
 * Blocks until next event or timeout.
 * @param client Connected client subscribed via openlogi.subscribe.
 * @param timeout_ms Timeout in milliseconds (-1 for infinite).
 * @return Parsed event document, or NULL on timeout/disconnect.
 */
yyjson_doc *ipc_client_read_event(ipc_client_t *client, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_IPC_CLIENT_H */
