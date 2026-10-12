/**
 * @file test_framework.h
 * @brief Unified ANSI test framework for OpenLogi Pure C unit and regression tests.
 */

#ifndef OPENLOGI_TEST_FRAMEWORK_H
#define OPENLOGI_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Check whether colors should be used */
static inline bool test_has_color(void)
{
    if (getenv("NO_COLOR") != NULL) return false;
    if (getenv("FORCE_COLOR") != NULL) return true;
    return isatty(fileno(stdout));
}

#define T_RESET     (test_has_color() ? "\033[0m" : "")
#define T_BOLD      (test_has_color() ? "\033[1m" : "")
#define T_DIM       (test_has_color() ? "\033[2m" : "")
#define T_GREEN     (test_has_color() ? "\033[32m" : "")
#define T_B_GREEN   (test_has_color() ? "\033[1;32m" : "")
#define T_RED       (test_has_color() ? "\033[31m" : "")
#define T_B_RED     (test_has_color() ? "\033[1;31m" : "")
#define T_YELLOW    (test_has_color() ? "\033[33m" : "")
#define T_BLUE      (test_has_color() ? "\033[34m" : "")
#define T_MAGENTA   (test_has_color() ? "\033[35m" : "")
#define T_CYAN      (test_has_color() ? "\033[36m" : "")
#define T_GRAY      (test_has_color() ? "\033[90m" : "")

/* High-resolution timestamp in nanoseconds */
static inline uint64_t test_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

typedef struct {
    const char *suite_name;
    const char *suite_desc;
    int total;
    int passed;
    int failed;
    uint64_t start_ns;
} test_suite_state_t;

static inline void test_suite_begin(test_suite_state_t *s, const char *name, const char *desc)
{
    s->suite_name = name;
    s->suite_desc = desc;
    s->total = 0;
    s->passed = 0;
    s->failed = 0;
    s->start_ns = test_now_ns();

    printf("\n%s%s PASS %s %s%s%s %s(%s)%s\n",
           T_BOLD, T_B_GREEN, T_RESET,
           T_BOLD, name, T_RESET,
           T_GRAY, desc ? desc : "", T_RESET);
}

static inline void test_suite_end(const test_suite_state_t *s)
{
    uint64_t total_ns = test_now_ns() - s->start_ns;
    double ms = (double)total_ns / 1000000.0;

    if (s->failed == 0) {
        printf("  %sSummary:%s %s%d passed%s, 0 failed %s(%.2f ms)%s\n",
               T_DIM, T_RESET,
               T_GREEN, s->passed, T_RESET,
               T_GRAY, ms, T_RESET);
    } else {
        printf("  %sSummary:%s %s%d passed%s, %s%d failed%s %s(%.2f ms)%s\n",
               T_DIM, T_RESET,
               T_GREEN, s->passed, T_RESET,
               T_RED, s->failed, T_RESET,
               T_GRAY, ms, T_RESET);
    }
}

#define TEST_SUITE_DECLARE(suite_var) \
    static test_suite_state_t suite_var

#define TEST_CASE_RUN(suite_var, fn) do { \
    suite_var.total++; \
    uint64_t _t0 = test_now_ns(); \
    fn(); \
    uint64_t _t1 = test_now_ns(); \
    double _ms = (double)(_t1 - _t0) / 1000000.0; \
    suite_var.passed++; \
    printf("    %s✔%s %-50s %s%6.2f ms%s\n", \
           T_B_GREEN, T_RESET, #fn, T_GRAY, _ms, T_RESET); \
} while (0)

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "    %s✖ FAILED:%s %s (%s:%d)\n", \
                T_B_RED, T_RESET, msg, __FILE__, __LINE__); \
        assert(cond); \
    } \
} while (0)

#ifdef __cplusplus
}
#endif

#endif /* OPENLOGI_TEST_FRAMEWORK_H */
