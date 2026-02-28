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

static int test_pass = 0;
static int test_fail = 0;
static int test_current_failed = 0;
static const char *test_current_name = "";

#define TEST(name) static void test_##name(void)

#define RUN(name) do { \
    test_current_name = #name; \
    test_current_failed = 0; \
    test_##name(); \
    if (test_current_failed) { \
        printf("FAIL  %s\n", #name); \
        test_fail++; \
    } else { \
        printf("  ok  %s\n", #name); \
        test_pass++; \
    } \
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

/* Common init for all tests — call at start of main() */
static void test_init(void)
{
    /* Suppress unused warning when test.h is included */
    (void)test_pass;
    (void)test_fail;
    (void)test_current_failed;
    (void)test_current_name;
}

#endif /* TEST_H */
