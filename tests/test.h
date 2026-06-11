#ifndef TEST_H
#define TEST_H

/*
 * Minimal test framework for CL-Amiga.
 * Usage:
 *   TEST(name) { ASSERT(...); ASSERT_EQ(...); }
 *   int main() { RUN(name); REPORT(); }
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static int test_pass = 0;
static int test_fail = 0;
static int test_current_failed = 0;
static volatile const char *test_current_name = "";
static volatile unsigned int test_current_name_len = 0;

/* Per-test watchdog (seconds).  A test that blocks (e.g. a thread/GC
 * deadlock that only manifests under a particular scheduler) would
 * otherwise hang the whole CI job until GitHub's job timeout.  The
 * SIGALRM handler names the offending test on stderr (unbuffered, so it
 * survives a hung process) and aborts so the run fails loudly and fast.
 * Override with TEST_WATCHDOG_SECS=0 to disable, or any value to retune. */
#ifndef TEST_WATCHDOG_DEFAULT_SECS
#define TEST_WATCHDOG_DEFAULT_SECS 120
#endif

static volatile unsigned int test_watchdog_secs = TEST_WATCHDOG_DEFAULT_SECS;

static void test_watchdog_handler(int sig)
{
    /* async-signal-safe: write() + the literal test name only */
    static const char msg[] = "\n*** TEST WATCHDOG: timed out (likely deadlock) in test: ";
    (void)sig;
    write(2, msg, sizeof(msg) - 1);
    if (test_current_name)
        write(2, (const char *)test_current_name, test_current_name_len);
    write(2, "\n", 1);
    /* abort() raises SIGABRT -> core/backtrace; not strictly async-signal
     * -safe but acceptable for a fatal watchdog in a test harness. */
    abort();
}

/* One-time harness setup: unbuffered stdout (so per-test progress survives a
 * hang on CI, where stdout is a pipe) + the SIGALRM watchdog.  Called lazily
 * from RUN so it applies even to test mains that don't call test_init(). */
static void test_setup_once(void)
{
    static int done = 0;
    const char *env;
    if (done) return;
    done = 1;
    setvbuf(stdout, NULL, _IONBF, 0);
    env = getenv("TEST_WATCHDOG_SECS");
    if (env)
        test_watchdog_secs = (unsigned int)strtoul(env, NULL, 10);
    if (test_watchdog_secs)
        signal(SIGALRM, test_watchdog_handler);
}

#define TEST(name) static void test_##name(void)

#define RUN(name) do { \
    test_setup_once(); \
    test_current_name = #name; \
    test_current_name_len = (unsigned int)(sizeof(#name) - 1); \
    test_current_failed = 0; \
    if (test_watchdog_secs) alarm(test_watchdog_secs); \
    test_##name(); \
    if (test_watchdog_secs) alarm(0); \
    if (test_current_failed) { \
        printf("FAIL  %s\n", #name); \
        test_fail++; \
    } else { \
        printf("  ok  %s\n", #name); \
        test_pass++; \
    } \
    fflush(stdout); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        test_current_failed = 1; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  ASSERT_EQ FAILED: %s:%d: %s != %s\n", \
               __FILE__, __LINE__, #a, #b); \
        test_current_failed = 1; \
    } \
} while(0)

#define ASSERT_EQ_INT(a, b) do { \
    int _a = (a), _b = (b); \
    if (_a != _b) { \
        printf("  ASSERT_EQ_INT FAILED: %s:%d: %s = %d, expected %d\n", \
               __FILE__, __LINE__, #a, _a, _b); \
        test_current_failed = 1; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    const char *_a = (a), *_b = (b); \
    if (strcmp(_a, _b) != 0) { \
        printf("  ASSERT_STR_EQ FAILED: %s:%d: \"%s\" != \"%s\"\n", \
               __FILE__, __LINE__, _a, _b); \
        test_current_failed = 1; \
    } \
} while(0)

#define REPORT() do { \
    printf("\n%d passed, %d failed, %d total\n", \
           test_pass, test_fail, test_pass + test_fail); \
    return test_fail > 0 ? 1 : 0; \
} while(0)

/* Common init for all tests — call at start of main() (optional: RUN also
 * lazily performs this setup, so tests that omit it are still covered). */
static void test_init(void)
{
    test_setup_once();

    /* Suppress unused warning when test.h is included */
    (void)test_pass;
    (void)test_fail;
    (void)test_current_failed;
    (void)test_current_name;
    (void)test_current_name_len;
    (void)test_watchdog_handler;
}

#endif /* TEST_H */
