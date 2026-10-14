/**
 * @file ipc_server.c
 * @brief High-performance UNIX domain socket JSON-RPC 2.0 server.
 */

#include "ipc_server.h"
#include "app_watch.h"
#include "hook.h"
#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
extern Boolean AXIsProcessTrusted(void);
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <sys/event.h>
#define OPENLOGI_USE_KQUEUE 1
#endif

#define TIMER_CONFIG_SETTLE 9999

typedef struct {
    int fd;
    bool subscribed;
    char rx_buf[8192];
    size_t rx_len;
} ipc_client_slot_t;

static pthread_t g_server_thread;
static volatile bool g_is_running = false;
static int g_listen_fd = -1;
static int g_stop_pipe[2] = { -1, -1 };
static char g_socket_path[512] = { 0 };
static openlogi_daemon_ctx_t *g_ctx = NULL;

#ifdef OPENLOGI_USE_KQUEUE
static int g_kq = -1;
static int g_config_fd = -1;
static char g_watch_path[512] = { 0 };
static ipc_file_watch_cb_t g_config_cb = NULL;
static void *g_config_ud = NULL;
static pthread_mutex_t g_watch_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static ipc_client_slot_t g_clients[OPENLOGI_MAX_CLIENTS];
static pthread_mutex_t g_clients_lock = PTHREAD_MUTEX_INITIALIZER;

/* Helper to recursively create directory */
static bool mkdir_p(const char *dir_path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", dir_path);
    size_t len = strlen(tmp);
    if (len == 0) return false;

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0755);
            tmp[i] = '/';
        }
    }
    mkdir(tmp, 0755);
    return true;
}

bool ipc_get_default_socket_path(char *out_path, size_t max_len)
{
    if (!out_path || max_len == 0) return false;

    const char *xdg_state = getenv("XDG_STATE_HOME");
    if (xdg_state && xdg_state[0] != '\0') {
        snprintf(out_path, max_len, "%s/openlogi/openlogi.sock", xdg_state);
        return true;
    }

    const char *home = getenv("HOME");
    if (home && home[0] != '\0') {
        snprintf(out_path, max_len, "%s/.local/state/openlogi/openlogi.sock", home);
        return true;
    }

    snprintf(out_path, max_len, "/tmp/openlogi.sock");
    return true;
}

/* Format standard JSON-RPC 2.0 error response */
static char *format_jsonrpc_error(yyjson_val *id_val, int code, const char *msg)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_str(doc, root, "jsonrpc", "2.0");

    if (id_val) {
        yyjson_mut_val *mid = yyjson_val_mut_copy(doc, id_val);
        yyjson_mut_obj_add_val(doc, root, "id", mid);
    } else {
        yyjson_mut_obj_add_null(doc, root, "id");
    }

    yyjson_mut_val *err_obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, root, "error", err_obj);
    yyjson_mut_obj_add_int(doc, err_obj, "code", code);
    yyjson_mut_obj_add_str(doc, err_obj, "message", msg ? msg : "Unknown error");

    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

/* RPC: openlogi.get_status */
static yyjson_mut_val *handle_get_status(openlogi_daemon_ctx_t *ctx, yyjson_mut_doc *doc)
{
    yyjson_mut_val *res = yyjson_mut_obj(doc);

    yyjson_mut_obj_add_str(doc, res, "version", OPENLOGI_AGENT_VERSION);
    yyjson_mut_obj_add_str(doc, res, "protocol", OPENLOGI_PROTOCOL_VERSION);
    yyjson_mut_obj_add_uint(doc, res, "pid", (uint64_t)getpid());

    time_t uptime = (ctx && ctx->start_time > 0) ? (time(NULL) - ctx->start_time) : 0;
    yyjson_mut_obj_add_uint(doc, res, "uptime_sec", (uint64_t)uptime);

    /* Permissions */
    bool accessibility = false;
    bool input_mon = false;
#ifdef __APPLE__
    accessibility = AXIsProcessTrusted();
    extern bool CGPreflightListenEventAccess(void);
    input_mon = CGPreflightListenEventAccess();
#else
    accessibility = true;
    input_mon = true;
#endif

    yyjson_mut_val *perms = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, res, "permissions", perms);
    yyjson_mut_obj_add_bool(doc, perms, "accessibility", accessibility);
    yyjson_mut_obj_add_bool(doc, perms, "input_monitoring", input_mon);

    /* Hook and App state */
    yyjson_mut_obj_add_bool(doc, res, "hook_installed", hook_is_running());

    char front_app[256] = { 0 };
    app_watch_get_frontmost(front_app, sizeof(front_app));
    yyjson_mut_obj_add_strcpy(doc, res, "frontmost_app", front_app);

    size_t online_devs = 0;
    if (ctx) {
        for (size_t i = 0; i < ctx->device_count; i++) {
            if (ctx->devices[i].online) online_devs++;
        }
    }
    yyjson_mut_obj_add_uint(doc, res, "devices_online", online_devs);

    return res;
}

/* RPC: openlogi.get_inventory */
static yyjson_mut_val *handle_get_inventory(openlogi_daemon_ctx_t *ctx, yyjson_mut_doc *doc)
{
    yyjson_mut_val *res = yyjson_mut_obj(doc);
    yyjson_mut_val *dev_arr = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, res, "devices", dev_arr);

    if (ctx) {
        for (size_t i = 0; i < ctx->device_count; i++) {
            const openlogi_device_status_t *d = &ctx->devices[i];
            yyjson_mut_val *d_obj = yyjson_mut_obj(doc);

            yyjson_mut_obj_add_strcpy(doc, d_obj, "device_key", d->device_key);
            yyjson_mut_obj_add_strcpy(doc, d_obj, "product_name", d->product_name);

            char vid_hex[16], pid_hex[16];
            snprintf(vid_hex, sizeof(vid_hex), "0x%04X", d->vendor_id);
            snprintf(pid_hex, sizeof(pid_hex), "0x%04X", d->product_id);
            yyjson_mut_obj_add_strcpy(doc, d_obj, "vendor_id", vid_hex);
            yyjson_mut_obj_add_strcpy(doc, d_obj, "product_id", pid_hex);

            yyjson_mut_obj_add_strcpy(doc, d_obj, "serial_number", d->serial_number);
            yyjson_mut_obj_add_uint(doc, d_obj, "battery_percent", d->battery_percent);
            yyjson_mut_obj_add_bool(doc, d_obj, "is_charging", d->is_charging);
            yyjson_mut_obj_add_uint(doc, d_obj, "dpi", d->dpi);
            yyjson_mut_obj_add_uint(doc, d_obj, "smartshift_mode", d->smartshift_mode);
            yyjson_mut_obj_add_uint(doc, d_obj, "smartshift_threshold", d->smartshift_threshold);
            yyjson_mut_obj_add_bool(doc, d_obj, "online", d->online);

            yyjson_mut_arr_add_val(dev_arr, d_obj);
        }
    }

    return res;
}

/* RPC: openlogi.set_dpi */
static yyjson_mut_val *handle_set_dpi(openlogi_daemon_ctx_t *ctx, yyjson_val *params, yyjson_mut_doc *doc, int *err_code, const char **err_msg)
{
    if (!params || !yyjson_is_obj(params)) {
        *err_code = -32602;
        *err_msg = "Invalid params: object required";
        return NULL;
    }

    yyjson_val *dpi_val = yyjson_obj_get(params, "dpi");
    if (!dpi_val || !yyjson_is_int(dpi_val)) {
        *err_code = -32602;
        *err_msg = "Param 'dpi' is required and must be an integer";
        return NULL;
    }
    uint16_t dpi = (uint16_t)yyjson_get_uint(dpi_val);

    const char *dev_key = yyjson_get_str(yyjson_obj_get(params, "device_key"));
    if (!dev_key && ctx && ctx->device_count > 0) {
        dev_key = ctx->devices[0].device_key;
    }

    bool success = true;
    if (ctx && ctx->set_dpi) {
        success = ctx->set_dpi(ctx, dev_key, dpi);
    } else if (ctx && ctx->device_count > 0) {
        /* Update local state if no callback set */
        for (size_t i = 0; i < ctx->device_count; i++) {
            if (!dev_key || strcmp(ctx->devices[i].device_key, dev_key) == 0) {
                ctx->devices[i].dpi = dpi;
                break;
            }
        }
    }

    if (!success) {
        *err_code = -32000;
        *err_msg = "Failed to set DPI on hardware device";
        return NULL;
    }

    yyjson_mut_val *res = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, res, "success", true);
    yyjson_mut_obj_add_uint(doc, res, "dpi", dpi);
    if (dev_key) yyjson_mut_obj_add_str(doc, res, "device_key", dev_key);
    return res;
}

/* RPC: openlogi.set_smartshift */
static yyjson_mut_val *handle_set_smartshift(openlogi_daemon_ctx_t *ctx, yyjson_val *params, yyjson_mut_doc *doc, int *err_code, const char **err_msg)
{
    if (!params || !yyjson_is_obj(params)) {
        *err_code = -32602;
        *err_msg = "Invalid params: object required";
        return NULL;
    }

    uint8_t mode = 1;
    yyjson_val *m_val = yyjson_obj_get(params, "mode");
    if (m_val) {
        if (yyjson_is_str(m_val)) {
            const char *m_str = yyjson_get_str(m_val);
            if (strcasecmp(m_str, "ratchet") == 0) mode = 1;
            else if (strcasecmp(m_str, "freespin") == 0) mode = 2;
            else if (strcasecmp(m_str, "auto") == 0) mode = 3;
            else mode = 0;
        } else if (yyjson_is_int(m_val)) {
            mode = (uint8_t)yyjson_get_uint(m_val);
        }
    }

    uint8_t threshold = 16;
    yyjson_val *t_val = yyjson_obj_get(params, "threshold");
    if (t_val && yyjson_is_int(t_val)) {
        threshold = (uint8_t)yyjson_get_uint(t_val);
    }

    const char *dev_key = yyjson_get_str(yyjson_obj_get(params, "device_key"));
    if (!dev_key && ctx && ctx->device_count > 0) {
        dev_key = ctx->devices[0].device_key;
    }

    bool success = true;
    if (ctx && ctx->set_smartshift) {
        success = ctx->set_smartshift(ctx, dev_key, mode, threshold);
    } else if (ctx && ctx->device_count > 0) {
        for (size_t i = 0; i < ctx->device_count; i++) {
            if (!dev_key || strcmp(ctx->devices[i].device_key, dev_key) == 0) {
                ctx->devices[i].smartshift_mode = mode;
                ctx->devices[i].smartshift_threshold = threshold;
                break;
            }
        }
    }

    if (!success) {
        *err_code = -32000;
        *err_msg = "Failed to set SmartShift on hardware device";
        return NULL;
    }

    yyjson_mut_val *res = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, res, "success", true);
    yyjson_mut_obj_add_uint(doc, res, "mode", mode);
    yyjson_mut_obj_add_uint(doc, res, "threshold", threshold);
    if (dev_key) yyjson_mut_obj_add_str(doc, res, "device_key", dev_key);
    return res;
}

/* RPC: openlogi.reload_config */
static yyjson_mut_val *handle_reload_config(openlogi_daemon_ctx_t *ctx, yyjson_mut_doc *doc)
{
    bool ok = true;
    if (ctx && ctx->reload_config) {
        ok = ctx->reload_config(ctx);
    }

    yyjson_mut_val *res = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, res, "success", ok);
    if (ctx) {
        yyjson_mut_obj_add_uint(doc, res, "schema_version", ctx->config.schema_version);
    }
    return res;
}

char *ipc_dispatch_jsonrpc(openlogi_daemon_ctx_t *ctx, const char *request_json)
{
    if (!request_json || request_json[0] == '\0') {
        return format_jsonrpc_error(NULL, -32700, "Empty request");
    }

    yyjson_doc *req_doc = yyjson_read(request_json, strlen(request_json), 0);
    if (!req_doc) {
        return format_jsonrpc_error(NULL, -32700, "Parse error: Invalid JSON");
    }

    yyjson_val *root = yyjson_doc_get_root(req_doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(req_doc);
        return format_jsonrpc_error(NULL, -32600, "Invalid Request: JSON object expected");
    }

    yyjson_val *id_val = yyjson_obj_get(root, "id");
    yyjson_val *method_val = yyjson_obj_get(root, "method");
    if (!method_val || !yyjson_is_str(method_val)) {
        char *err = format_jsonrpc_error(id_val, -32600, "Invalid Request: 'method' string required");
        yyjson_doc_free(req_doc);
        return err;
    }

    const char *method = yyjson_get_str(method_val);
    yyjson_val *params = yyjson_obj_get(root, "params");

    yyjson_mut_doc *resp_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *resp_root = yyjson_mut_obj(resp_doc);
    yyjson_mut_doc_set_root(resp_doc, resp_root);

    yyjson_mut_obj_add_str(resp_doc, resp_root, "jsonrpc", "2.0");
    if (id_val) {
        yyjson_mut_val *id_copy = yyjson_val_mut_copy(resp_doc, id_val);
        yyjson_mut_obj_add_val(resp_doc, resp_root, "id", id_copy);
    } else {
        yyjson_mut_obj_add_null(resp_doc, resp_root, "id");
    }

    yyjson_mut_val *result_val = NULL;
    int err_code = 0;
    const char *err_msg = NULL;

    if (strcmp(method, "openlogi.get_status") == 0) {
        result_val = handle_get_status(ctx, resp_doc);
    } else if (strcmp(method, "openlogi.get_inventory") == 0) {
        result_val = handle_get_inventory(ctx, resp_doc);
    } else if (strcmp(method, "openlogi.set_dpi") == 0) {
        result_val = handle_set_dpi(ctx, params, resp_doc, &err_code, &err_msg);
    } else if (strcmp(method, "openlogi.set_smartshift") == 0) {
        result_val = handle_set_smartshift(ctx, params, resp_doc, &err_code, &err_msg);
    } else if (strcmp(method, "openlogi.reload_config") == 0) {
        result_val = handle_reload_config(ctx, resp_doc);
    } else if (strcmp(method, "openlogi.subscribe") == 0) {
        result_val = yyjson_mut_obj(resp_doc);
        yyjson_mut_obj_add_bool(resp_doc, result_val, "subscribed", true);
    } else {
        err_code = -32601;
        err_msg = "Method not found";
    }

    char *out_json = NULL;
    if (err_code != 0) {
        yyjson_mut_doc_free(resp_doc);
        out_json = format_jsonrpc_error(id_val, err_code, err_msg);
    } else {
        yyjson_mut_obj_add_val(resp_doc, resp_root, "result", result_val);
        out_json = yyjson_mut_write(resp_doc, 0, NULL);
        yyjson_mut_doc_free(resp_doc);
    }

    yyjson_doc_free(req_doc);
    return out_json;
}

static void *ipc_server_thread_main(void *arg)
{
    (void)arg;

#ifdef OPENLOGI_USE_KQUEUE
    struct kevent init_evs[2];
    EV_SET(&init_evs[0], g_stop_pipe[0], EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
    EV_SET(&init_evs[1], g_listen_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
    kevent(g_kq, init_evs, 2, NULL, 0, NULL);

    while (g_is_running) {
        struct kevent events[16];
        int nev = kevent(g_kq, NULL, 0, events, 16, NULL);
        if (!g_is_running) break;
        if (nev <= 0) continue;

        for (int i = 0; i < nev; i++) {
            uintptr_t ident = events[i].ident;
            int16_t filter  = events[i].filter;
            uint32_t fflags = events[i].fflags;

            /* 1. Stop signal */
            if (ident == (uintptr_t)g_stop_pipe[0]) {
                return NULL;
            }

            /* 2. New incoming connection */
            if (ident == (uintptr_t)g_listen_fd) {
                int client_fd = accept(g_listen_fd, NULL, NULL);
                if (client_fd >= 0) {
                    fcntl(client_fd, F_SETFL, O_NONBLOCK);
#ifdef SO_NOSIGPIPE
                    int nosigpipe = 1;
                    setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe));
#endif
                    pthread_mutex_lock(&g_clients_lock);
                    bool added = false;
                    for (int c = 0; c < OPENLOGI_MAX_CLIENTS; c++) {
                        if (g_clients[c].fd < 0) {
                            g_clients[c].fd = client_fd;
                            g_clients[c].subscribed = false;
                            g_clients[c].rx_len = 0;
                            g_clients[c].rx_buf[0] = '\0';
                            added = true;

                            struct kevent cev;
                            EV_SET(&cev, client_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
                            kevent(g_kq, &cev, 1, NULL, 0, NULL);
                            break;
                        }
                    }
                    if (!added) {
                        close(client_fd);
                    }
                    pthread_mutex_unlock(&g_clients_lock);
                }
                continue;
            }

            /* 3. Config file change */
            pthread_mutex_lock(&g_watch_lock);
            bool is_cfg_event = (filter == EVFILT_VNODE && ident == (uintptr_t)g_config_fd && g_config_fd >= 0);
            ipc_file_watch_cb_t cfg_cb = g_config_cb;
            void *cfg_ud = g_config_ud;
            pthread_mutex_unlock(&g_watch_lock);

            if (is_cfg_event) {
                if (cfg_cb) {
                    cfg_cb(g_watch_path, cfg_ud);
                }

                if (fflags & (NOTE_DELETE | NOTE_RENAME)) {
                    /* Atomic file replacement by text editors: arm 50ms settling timer without sleeping! */
                    pthread_mutex_lock(&g_watch_lock);
                    if (g_config_fd >= 0) {
                        close(g_config_fd);
                        g_config_fd = -1;
                    }
                    struct kevent tev;
                    EV_SET(&tev, TIMER_CONFIG_SETTLE, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0, 50, NULL);
                    kevent(g_kq, &tev, 1, NULL, 0, NULL);
                    pthread_mutex_unlock(&g_watch_lock);
                }
                continue;
            }

            /* 4. Config settling timer expired */
            if (filter == EVFILT_TIMER && ident == TIMER_CONFIG_SETTLE) {
                pthread_mutex_lock(&g_watch_lock);
                if (g_watch_path[0] != '\0') {
                    int new_fd = open(g_watch_path, O_RDONLY);
                    if (new_fd >= 0) {
                        g_config_fd = new_fd;
                        struct kevent vev;
                        EV_SET(&vev, g_config_fd, EVFILT_VNODE,
                               EV_ADD | EV_ENABLE | EV_CLEAR,
                               NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB, 0, NULL);
                        kevent(g_kq, &vev, 1, NULL, 0, NULL);
                    }
                }
                cfg_cb = g_config_cb;
                cfg_ud = g_config_ud;
                pthread_mutex_unlock(&g_watch_lock);

                if (cfg_cb) {
                    cfg_cb(g_watch_path, cfg_ud);
                }
                continue;
            }

            /* 5. Client socket I/O */
            if (filter == EVFILT_READ) {
                int client_fd = (int)ident;
                bool client_closed = false;

                pthread_mutex_lock(&g_clients_lock);
                int slot = -1;
                for (int c = 0; c < OPENLOGI_MAX_CLIENTS; c++) {
                    if (g_clients[c].fd == client_fd) {
                        slot = c;
                        break;
                    }
                }

                if (slot >= 0) {
                    ipc_client_slot_t *cl = &g_clients[slot];
                    ssize_t n = read(cl->fd, cl->rx_buf + cl->rx_len, sizeof(cl->rx_buf) - cl->rx_len - 1);
                    if (n <= 0) {
                        close(cl->fd);
                        cl->fd = -1;
                        cl->rx_len = 0;
                        client_closed = true;
                    } else {
                        cl->rx_len += (size_t)n;
                        cl->rx_buf[cl->rx_len] = '\0';
                    }
                }
                pthread_mutex_unlock(&g_clients_lock);

                if (slot >= 0 && !client_closed) {
                    while (true) {
                        char line_to_process[8192];
                        line_to_process[0] = '\0';

                        pthread_mutex_lock(&g_clients_lock);
                        if (g_clients[slot].fd == client_fd) {
                            ipc_client_slot_t *cl = &g_clients[slot];
                            char *nl = strchr(cl->rx_buf, '\n');
                            if (nl) {
                                size_t line_bytes = (size_t)(nl - cl->rx_buf);
                                if (line_bytes < sizeof(line_to_process)) {
                                    memcpy(line_to_process, cl->rx_buf, line_bytes);
                                    line_to_process[line_bytes] = '\0';
                                }

                                size_t remaining = cl->rx_len - (line_bytes + 1);
                                if (remaining > 0) {
                                    memmove(cl->rx_buf, nl + 1, remaining);
                                }
                                cl->rx_len = remaining;
                                cl->rx_buf[cl->rx_len] = '\0';
                            }
                        }
                        pthread_mutex_unlock(&g_clients_lock);

                        if (line_to_process[0] == '\0') {
                            break; /* No more complete lines */
                        }

                        size_t llen = strlen(line_to_process);
                        if (llen > 0 && line_to_process[llen - 1] == '\r') {
                            line_to_process[llen - 1] = '\0';
                        }

                        if (strlen(line_to_process) > 0) {
                            if (strstr(line_to_process, "openlogi.subscribe")) {
                                pthread_mutex_lock(&g_clients_lock);
                                if (g_clients[slot].fd == client_fd) {
                                    g_clients[slot].subscribed = true;
                                }
                                pthread_mutex_unlock(&g_clients_lock);
                            }

                            char *resp = ipc_dispatch_jsonrpc(g_ctx, line_to_process);
                            if (resp) {
                                size_t rlen = strlen(resp);
                                pthread_mutex_lock(&g_clients_lock);
                                if (g_clients[slot].fd == client_fd) {
                                    (void)write(client_fd, resp, rlen);
                                    (void)write(client_fd, "\n", 1);
                                }
                                pthread_mutex_unlock(&g_clients_lock);
                                free(resp);
                            }
                        }
                    }
                }
            }
        }
    }
    return NULL;
#else
    while (g_is_running) {
        struct pollfd pfds[OPENLOGI_MAX_CLIENTS + 2];
        int pfd_count = 0;

        /* Index 0: Stop pipe */
        pfds[0].fd = g_stop_pipe[0];
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        pfd_count++;

        /* Index 1: Listen socket */
        pfds[1].fd = g_listen_fd;
        pfds[1].events = POLLIN;
        pfds[1].revents = 0;
        pfd_count++;

        /* Client sockets */
        pthread_mutex_lock(&g_clients_lock);
        int client_indices[OPENLOGI_MAX_CLIENTS];
        for (int i = 0; i < OPENLOGI_MAX_CLIENTS; i++) {
            if (g_clients[i].fd >= 0) {
                pfds[pfd_count].fd = g_clients[i].fd;
                pfds[pfd_count].events = POLLIN;
                pfds[pfd_count].revents = 0;
                client_indices[pfd_count] = i;
                pfd_count++;
            }
        }
        pthread_mutex_unlock(&g_clients_lock);

        int ret = poll(pfds, pfd_count, 100); /* 100ms timeout */
        if (!g_is_running) break;
        if (ret <= 0) continue;

        /* Stop signal */
        if (pfds[0].revents & POLLIN) {
            break;
        }

        /* New incoming connection */
        if (pfds[1].revents & POLLIN) {
            int client_fd = accept(g_listen_fd, NULL, NULL);
            if (client_fd >= 0) {
                fcntl(client_fd, F_SETFL, O_NONBLOCK);
#ifdef SO_NOSIGPIPE
                int nosigpipe = 1;
                setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe));
#endif
                pthread_mutex_lock(&g_clients_lock);
                bool added = false;
                for (int i = 0; i < OPENLOGI_MAX_CLIENTS; i++) {
                    if (g_clients[i].fd < 0) {
                        g_clients[i].fd = client_fd;
                        g_clients[i].subscribed = false;
                        g_clients[i].rx_len = 0;
                        g_clients[i].rx_buf[0] = '\0';
                        added = true;
                        break;
                    }
                }
                if (!added) {
                    close(client_fd);
                }
                pthread_mutex_unlock(&g_clients_lock);
            }
        }

        /* Check client sockets */
        for (int p = 2; p < pfd_count; p++) {
            if (pfds[p].revents & (POLLIN | POLLHUP | POLLERR)) {
                int c_idx = client_indices[p];
                pthread_mutex_lock(&g_clients_lock);
                ipc_client_slot_t *cl = &g_clients[c_idx];

                if (cl->fd >= 0) {
                    ssize_t n = read(cl->fd, cl->rx_buf + cl->rx_len, sizeof(cl->rx_buf) - cl->rx_len - 1);
                    if (n <= 0) {
                        /* Disconnected */
                        close(cl->fd);
                        cl->fd = -1;
                        cl->rx_len = 0;
                    } else {
                        cl->rx_len += (size_t)n;
                        cl->rx_buf[cl->rx_len] = '\0';

                        /* Process newline-delimited requests */
                        char *line_start = cl->rx_buf;
                        char *nl = NULL;
                        while ((nl = strchr(line_start, '\n')) != NULL) {
                            *nl = '\0';
                            if (nl > line_start && *(nl - 1) == '\r') {
                                *(nl - 1) = '\0';
                            }

                            if (strlen(line_start) > 0) {
                                if (strstr(line_start, "openlogi.subscribe")) {
                                    cl->subscribed = true;
                                }

                                char *resp = ipc_dispatch_jsonrpc(g_ctx, line_start);
                                if (resp) {
                                    size_t rlen = strlen(resp);
                                    (void)write(cl->fd, resp, rlen);
                                    (void)write(cl->fd, "\n", 1);
                                    free(resp);
                                }
                            }
                            line_start = nl + 1;
                        }

                        /* Shift unparsed buffer */
                        size_t remaining = (size_t)((cl->rx_buf + cl->rx_len) - line_start);
                        if (remaining > 0 && line_start != cl->rx_buf) {
                            memmove(cl->rx_buf, line_start, remaining);
                        }
                        cl->rx_len = remaining;
                        cl->rx_buf[cl->rx_len] = '\0';
                    }
                }
                pthread_mutex_unlock(&g_clients_lock);
            }
        }
    }

    return NULL;
#endif
}

bool ipc_server_start(const char *socket_path, openlogi_daemon_ctx_t *ctx)
{
    if (!socket_path || socket_path[0] == '\0') return false;

    if (g_is_running) return true;

    g_ctx = ctx;
    strncpy(g_socket_path, socket_path, sizeof(g_socket_path) - 1);

    /* Ensure parent directory exists */
    char dir_copy[512];
    strncpy(dir_copy, socket_path, sizeof(dir_copy) - 1);
    char *last_slash = strrchr(dir_copy, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir_p(dir_copy);
    }

    /* Unlink existing socket file */
    unlink(g_socket_path);

    /* Open self-pipe for stop signaling */
    if (pipe(g_stop_pipe) != 0) {
        return false;
    }

    /* Create UNIX socket */
    g_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        close(g_stop_pipe[0]);
        close(g_stop_pipe[1]);
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_socket_path, sizeof(addr.sun_path) - 1);

    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
        close(g_stop_pipe[0]);
        close(g_stop_pipe[1]);
        return false;
    }

    /* Set socket permissions to user only */
    chmod(g_socket_path, 0700);

    if (listen(g_listen_fd, 8) != 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
        unlink(g_socket_path);
        close(g_stop_pipe[0]);
        close(g_stop_pipe[1]);
        return false;
    }

#ifdef OPENLOGI_USE_KQUEUE
    g_kq = kqueue();
    if (g_kq < 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
        unlink(g_socket_path);
        close(g_stop_pipe[0]);
        close(g_stop_pipe[1]);
        return false;
    }
#endif

    /* Initialize client slots */
    pthread_mutex_lock(&g_clients_lock);
    for (int i = 0; i < OPENLOGI_MAX_CLIENTS; i++) {
        g_clients[i].fd = -1;
        g_clients[i].subscribed = false;
        g_clients[i].rx_len = 0;
    }
    pthread_mutex_unlock(&g_clients_lock);

    g_is_running = true;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
    int thread_rc = pthread_create(&g_server_thread, &attr, ipc_server_thread_main, NULL);
    pthread_attr_destroy(&attr);

    if (thread_rc != 0) {
        g_is_running = false;
#ifdef OPENLOGI_USE_KQUEUE
        close(g_kq);
        g_kq = -1;
#endif
        close(g_listen_fd);
        g_listen_fd = -1;
        unlink(g_socket_path);
        close(g_stop_pipe[0]);
        close(g_stop_pipe[1]);
        return false;
    }

    return true;
}

void ipc_server_stop(void)
{
    if (!g_is_running) return;

    g_is_running = false;
    if (g_stop_pipe[1] >= 0) {
        char stop_byte = 1;
        (void)write(g_stop_pipe[1], &stop_byte, 1);
    }

    pthread_join(g_server_thread, NULL);

#ifdef OPENLOGI_USE_KQUEUE
    pthread_mutex_lock(&g_watch_lock);
    if (g_config_fd >= 0) {
        close(g_config_fd);
        g_config_fd = -1;
    }
    g_watch_path[0] = '\0';
    g_config_cb = NULL;
    g_config_ud = NULL;
    pthread_mutex_unlock(&g_watch_lock);

    if (g_kq >= 0) {
        close(g_kq);
        g_kq = -1;
    }
#endif

    pthread_mutex_lock(&g_clients_lock);
    for (int i = 0; i < OPENLOGI_MAX_CLIENTS; i++) {
        if (g_clients[i].fd >= 0) {
            close(g_clients[i].fd);
            g_clients[i].fd = -1;
        }
    }
    pthread_mutex_unlock(&g_clients_lock);

    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }

    if (g_stop_pipe[0] >= 0) {
        close(g_stop_pipe[0]);
        close(g_stop_pipe[1]);
        g_stop_pipe[0] = -1;
        g_stop_pipe[1] = -1;
    }

    if (g_socket_path[0] != '\0') {
        unlink(g_socket_path);
        g_socket_path[0] = '\0';
    }
}

#ifdef OPENLOGI_USE_KQUEUE
bool ipc_server_watch_file(const char *filepath, ipc_file_watch_cb_t callback, void *user_data)
{
    if (!filepath || !callback || !g_is_running || g_kq < 0) return false;

    pthread_mutex_lock(&g_watch_lock);
    strncpy(g_watch_path, filepath, sizeof(g_watch_path) - 1);
    g_config_cb = callback;
    g_config_ud = user_data;

    if (g_config_fd >= 0) {
        close(g_config_fd);
        g_config_fd = -1;
    }

    g_config_fd = open(filepath, O_RDONLY);
    if (g_config_fd >= 0) {
        struct kevent ev;
        EV_SET(&ev, g_config_fd, EVFILT_VNODE,
               EV_ADD | EV_ENABLE | EV_CLEAR,
               NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB, 0, NULL);
        kevent(g_kq, &ev, 1, NULL, 0, NULL);
    }
    pthread_mutex_unlock(&g_watch_lock);
    return true;
}

void ipc_server_unwatch_file(void)
{
    pthread_mutex_lock(&g_watch_lock);
    if (g_config_fd >= 0) {
        close(g_config_fd);
        g_config_fd = -1;
    }
    g_watch_path[0] = '\0';
    g_config_cb = NULL;
    g_config_ud = NULL;
    pthread_mutex_unlock(&g_watch_lock);
}
#else
bool ipc_server_watch_file(const char *filepath, ipc_file_watch_cb_t callback, void *user_data)
{
    (void)filepath; (void)callback; (void)user_data;
    return false;
}
void ipc_server_unwatch_file(void) {}
#endif

bool ipc_server_is_running(void)
{
    return g_is_running;
}

void ipc_server_broadcast_event(const char *event_name, const char *json_payload)
{
    if (!g_is_running || !event_name) return;

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_str(doc, root, "jsonrpc", "2.0");
    yyjson_mut_obj_add_str(doc, root, "method", "openlogi.event");

    yyjson_mut_val *params = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, root, "params", params);
    yyjson_mut_obj_add_str(doc, params, "event", event_name);

    if (json_payload && json_payload[0] != '\0') {
        yyjson_doc *p_doc = yyjson_read(json_payload, strlen(json_payload), 0);
        if (p_doc) {
            yyjson_mut_val *p_val = yyjson_val_mut_copy(doc, yyjson_doc_get_root(p_doc));
            yyjson_mut_obj_add_val(doc, params, "data", p_val);
            yyjson_doc_free(p_doc);
        } else {
            yyjson_mut_obj_add_str(doc, params, "data", json_payload);
        }
    }

    char *json_str = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);

    if (!json_str) return;

    size_t len = strlen(json_str);

    pthread_mutex_lock(&g_clients_lock);
    for (int i = 0; i < OPENLOGI_MAX_CLIENTS; i++) {
        if (g_clients[i].fd >= 0 && g_clients[i].subscribed) {
            (void)write(g_clients[i].fd, json_str, len);
            (void)write(g_clients[i].fd, "\n", 1);
        }
    }
    pthread_mutex_unlock(&g_clients_lock);

    free(json_str);
}
