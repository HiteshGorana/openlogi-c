/**
 * @file armed_session.c
 * @brief Implementation of armed session persistence ported from openlogi-agent.
 */

#include "armed_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool armed_session_encode(const armed_session_t *session, char *out_buf, size_t max_len)
{
    if (!session || !out_buf || max_len == 0) return false;
    int len = snprintf(out_buf, max_len, "%s %u\n", session->boot_uuid, session->audit_session_id);
    return (len > 0 && (size_t)len < max_len);
}

bool armed_session_decode(const char *text, armed_session_t *out_session)
{
    if (!text || !out_session) return false;

    char copy[128];
    strncpy(copy, text, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    /* Trim trailing newlines and spaces */
    size_t len = strlen(copy);
    while (len > 0 && (copy[len - 1] == '\n' || copy[len - 1] == '\r' || copy[len - 1] == ' ')) {
        copy[--len] = '\0';
    }
    if (len == 0) return false;

    char *space = strchr(copy, ' ');
    if (!space) return false;

    *space = '\0';
    const char *uuid_str = copy;
    const char *id_str = space + 1;

    if (uuid_str[0] == '\0' || id_str[0] == '\0') return false;

    char *endptr = NULL;
    unsigned long val = strtoul(id_str, &endptr, 10);
    if (!endptr || *endptr != '\0') return false;

    strncpy(out_session->boot_uuid, uuid_str, sizeof(out_session->boot_uuid) - 1);
    out_session->boot_uuid[sizeof(out_session->boot_uuid) - 1] = '\0';
    out_session->audit_session_id = (uint32_t)val;
    return true;
}

bool armed_session_write(const char *filepath, const armed_session_t *session)
{
    if (!filepath || !session) return false;

    char buf[128];
    if (!armed_session_encode(session, buf, sizeof(buf))) return false;

    FILE *f = fopen(filepath, "w");
    if (!f) return false;

    size_t written = fwrite(buf, 1, strlen(buf), f);
    fclose(f);
    return (written == strlen(buf));
}

bool armed_session_holds(const char *filepath, const armed_session_t *session)
{
    if (!filepath || !session) return false;

    FILE *f = fopen(filepath, "r");
    if (!f) return false;

    char buf[128];
    size_t read_bytes = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    if (read_bytes == 0) return false;
    buf[read_bytes] = '\0';

    armed_session_t decoded;
    if (!armed_session_decode(buf, &decoded)) return false;

    return (decoded.audit_session_id == session->audit_session_id &&
            strcmp(decoded.boot_uuid, session->boot_uuid) == 0);
}

void armed_session_clear(const char *filepath)
{
    if (!filepath) return;
    unlink(filepath);
}
