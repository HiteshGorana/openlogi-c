/**
 * @file test_single_instance.c
 * @brief Unit tests for single_instance lock mechanism.
 */

#include "single_instance.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

static void test_default_path(void)
{
    char path[512] = { 0 };
    bool ok = single_instance_get_default_path(path, sizeof(path));
    assert(ok);
    assert(strlen(path) > 0);
    assert(strstr(path, "agent.lock") != NULL);
}

static void test_lock_acquire_and_contention(void)
{
    const char *test_path = "/tmp/test_openlogi_lock_unit.lock";
    unlink(test_path);

    /* 1. Acquire initial lock */
    pid_t existing_pid = 0;
    int fd1 = single_instance_acquire(test_path, &existing_pid);
    assert(fd1 >= 0);

    /* 2. Check PID in file */
    pid_t read_pid = single_instance_read_pid(test_path);
    assert(read_pid == getpid());

    /* 3. Attempt second lock - should fail with contention */
    pid_t conflict_pid = 0;
    int fd2 = single_instance_acquire(test_path, &conflict_pid);
    assert(fd2 == -1);
    assert(conflict_pid == getpid());

    /* 4. Update PID */
    bool updated = single_instance_update_pid(fd1, 99999);
    assert(updated);
    assert(single_instance_read_pid(test_path) == 99999);

    /* 5. Release lock */
    single_instance_release(fd1, test_path);

    /* 6. Verify lock can now be acquired again */
    int fd3 = single_instance_acquire(test_path, NULL);
    assert(fd3 >= 0);
    single_instance_release(fd3, test_path);
}

TEST_SUITE_DECLARE(g_suite);

int main(void)
{
    test_suite_begin(&g_suite, "test_single_instance", "Single-Instance Process Lock");

    TEST_CASE_RUN(g_suite, test_default_path);
    TEST_CASE_RUN(g_suite, test_lock_acquire_and_contention);

    test_suite_end(&g_suite);
    return (g_suite.failed == 0) ? 0 : 1;
}
