/**
 * @file ipc_client.c
 * @brief Implementation of UNIX domain socket JSON-RPC 2.0 client.
 */

#include "ipc_client.h"
#include "ipc_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

bool ipc_client_connect(const char *socket_path, ipc_client_t *out_client)
{
    if (!out_client) return false;
    memset(out_client, 0, sizeof(*out_client));
    out_client->fd = -1;
    out_client->next_id = 1;

    if (socket_path && socket_path[0] != '\0') {
        strncpy(out_client->socket_path, socket_path, sizeof(out_client->socket_path) - 1);
    } else {
        ipc_get_default_socket_path(out_client->socket_path, sizeof(out_client->socket_path));
    }

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return false;
    int flags = fcntl(sock, F_GETFD);
    if (flags < 0 || fcntl(sock, F_SETFD, flags | FD_CLOEXEC) != 0) {
        close(sock);
        return false;
    }
#ifdef SO_NOSIGPIPE
    /* Permission approval can restart the daemon between connect and write. */
    int no_sigpipe = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) != 0) {
        close(sock);
        return false;
    }
#endif

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, out_client->socket_path, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sock);
        return false;
    }

    out_client->fd = sock;
    return true;
}

void ipc_client_close(ipc_client_t *client)
{
    if (client && client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }
}

static char *read_line_from_fd(int fd, int timeout_ms)
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ret = poll(&pfd, 1, timeout_ms);
    if (ret <= 0 || !(pfd.revents & POLLIN)) {
        return NULL;
    }

    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    while (1) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) {
            if (len == 0) {
                free(buf);
                return NULL;
            }
            break;
        }

        if (c == '\n') break;
        if (c == '\r') continue;

        if (len + 2 >= cap) {
            cap *= 2;
            char *new_buf = realloc(buf, cap);
            if (!new_buf) {
                free(buf);
                return NULL;
            }
            buf = new_buf;
        }

        buf[len++] = c;
    }

    buf[len] = '\0';
    return buf;
}

char *ipc_client_call_raw(ipc_client_t *client, const char *method, const char *params_json)
{
    if (!client || client->fd < 0 || !method) return NULL;

    const char *params = (params_json && params_json[0] != '\0') ? params_json : "{}";
    uint64_t id = client->next_id++;

    char req_hdr[256];
    char *buf = req_hdr;
    char *heap_buf = NULL;

    int req_len = snprintf(req_hdr, sizeof(req_hdr),
        "{\"jsonrpc\":\"2.0\",\"id\":%llu,\"method\":\"%s\",\"params\":%s}\n",
        (unsigned long long)id, method, params);

    if (req_len < 0) return NULL;

    if ((size_t)req_len >= sizeof(req_hdr)) {
        heap_buf = malloc((size_t)req_len + 1);
        if (!heap_buf) return NULL;
        snprintf(heap_buf, (size_t)req_len + 1,
            "{\"jsonrpc\":\"2.0\",\"id\":%llu,\"method\":\"%s\",\"params\":%s}\n",
            (unsigned long long)id, method, params);
        buf = heap_buf;
    }

#ifdef MSG_NOSIGNAL
    ssize_t written = send(client->fd, buf, (size_t)req_len, MSG_NOSIGNAL);
#else
    ssize_t written = send(client->fd, buf, (size_t)req_len, 0);
#endif
    if (heap_buf) free(heap_buf);
    if (written != req_len) return NULL;

    /* Await response (up to 3 seconds) */
    return read_line_from_fd(client->fd, 3000);
}

yyjson_doc *ipc_client_call(ipc_client_t *client, const char *method, const char *params_json)
{
    char *raw = ipc_client_call_raw(client, method, params_json);
    if (!raw) return NULL;

    yyjson_doc *doc = yyjson_read(raw, strlen(raw), 0);
    free(raw);
    return doc;
}

yyjson_doc *ipc_client_read_event(ipc_client_t *client, int timeout_ms)
{
    if (!client || client->fd < 0) return NULL;

    char *raw = read_line_from_fd(client->fd, timeout_ms);
    if (!raw) return NULL;

    yyjson_doc *doc = yyjson_read(raw, strlen(raw), 0);
    free(raw);
    return doc;
}
