/**
 * @file armed_session.h
 * @brief Armed session persistence ported from openlogi-agent.
 */

#ifndef OPENLOGI_ARMED_SESSION_H
#define OPENLOGI_ARMED_SESSION_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char boot_uuid[64];
    uint32_t audit_session_id;
} armed_session_t;

/**
 * @brief Safely encode session struct to string ("<boot_uuid> <audit_session_id>\n").
 */
bool armed_session_encode(const armed_session_t *session, char *out_buf, size_t max_len);

/**
 * @brief Safely decode session string into struct.
 * Returns false on malformed, incomplete, or corrupted records.
 */
bool armed_session_decode(const char *text, armed_session_t *out_session);

/**
 * @brief Write session record to file.
 */
bool armed_session_write(const char *filepath, const armed_session_t *session);

/**
 * @brief Check if file holds the exact session record.
 */
bool armed_session_holds(const char *filepath, const armed_session_t *session);

/**
 * @brief Clear / unlink session record file.
 */
void armed_session_clear(const char *filepath);

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_ARMED_SESSION_H */
