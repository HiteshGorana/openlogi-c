/**
 * @file test_armed_session.c
 * @brief Regression tests for armed session persistence ported from openlogi-agent.
 */

#include "armed_session.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define TEST_BOOT "C3E655FA-3500-45F4-BAFE-01DA6C16EF21"
#define TEST_PATH "/tmp/test_openlogi_armed_session_unit.txt"

static armed_session_t make_session(const char *boot, uint32_t audit_id)
{
    armed_session_t s;
    memset(&s, 0, sizeof(s));
    strncpy(s.boot_uuid, boot, sizeof(s.boot_uuid) - 1);
    s.audit_session_id = audit_id;
    return s;
}

static void test_only_the_recorded_session_is_rearmed(void)
{
    armed_session_clear(TEST_PATH);

    armed_session_t here = make_session(TEST_BOOT, 100023);

    /* 1. Nothing recorded yet */
    assert(!armed_session_holds(TEST_PATH, &here));

    /* 2. Write session */
    assert(armed_session_write(TEST_PATH, &here));

    /* 3. Same session re-arms */
    assert(armed_session_holds(TEST_PATH, &here));

    /* 4. Different audit ID in same boot stays dormant (clean login) */
    armed_session_t new_login = make_session(TEST_BOOT, 100024);
    assert(!armed_session_holds(TEST_PATH, &new_login));

    /* 5. Same audit ID but after reboot stays dormant */
    armed_session_t after_reboot = make_session("0D8F2B6A-1111-4C0E-9A7E-3B5C0A1D2E3F", 100023);
    assert(!armed_session_holds(TEST_PATH, &after_reboot));

    /* 6. Clear forgets the session */
    armed_session_clear(TEST_PATH);
    assert(!armed_session_holds(TEST_PATH, &here));
}

static void test_a_malformed_record_is_never_armed(void)
{
    const char *garbage_samples[] = {
        "",
        "\n",
        "no-space",
        "C3E655FA not-a-number\n",
        "   ",
        "only-one-word",
        NULL
    };

    armed_session_t here = make_session(TEST_BOOT, 100023);

    for (int i = 0; garbage_samples[i] != NULL; i++) {
        FILE *f = fopen(TEST_PATH, "w");
        assert(f != NULL);
        fputs(garbage_samples[i], f);
        fclose(f);

        /* Malformed record must safely return false and NEVER crash or assert */
        assert(!armed_session_holds(TEST_PATH, &here));
    }

    armed_session_clear(TEST_PATH);
}

static void test_encode_decode_roundtrip(void)
{
    armed_session_t orig = make_session(TEST_BOOT, 54321);
    char buf[128];

    assert(armed_session_encode(&orig, buf, sizeof(buf)));

    armed_session_t decoded;
    assert(armed_session_decode(buf, &decoded));

    assert(strcmp(decoded.boot_uuid, orig.boot_uuid) == 0);
    assert(decoded.audit_session_id == orig.audit_session_id);
}

TEST_SUITE_DECLARE(g_suite);

int main(void)
{
    test_suite_begin(&g_suite, "test_armed_session", "Armed Session & Crash-Respawn State");

    TEST_CASE_RUN(g_suite, test_only_the_recorded_session_is_rearmed);
    TEST_CASE_RUN(g_suite, test_a_malformed_record_is_never_armed);
    TEST_CASE_RUN(g_suite, test_encode_decode_roundtrip);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
