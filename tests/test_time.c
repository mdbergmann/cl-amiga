/*
 * test_time.c — universal-time encoding/decoding.
 *
 * Regression: encode-universal-time over-counted leap days for the 400-year
 * cycle (the "(truncate (+ y 399) 400)" term should be "(+ y 299)"), so every
 * date in 1901-2000 was encoded one day (86400 s) too large while decode was
 * correct.  This silently shifted local-time timestamps a day into the past
 * (the local-time epoch is a read-time #.(encode-universal-time ... 1 3 2000)),
 * which made the chipi InfluxDB persistence write points outside the retention
 * window.  Absolute values below are UTC universal time = unix + 2208988800.
 */
#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/reader.h"
#include "core/printer.h"
#include "core/compiler.h"
#include "core/vm.h"
#include "core/builtins.h"
#include "core/repl.h"
#include "platform/platform.h"

static void setup(void)
{
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
    cl_reader_init();
    cl_printer_init();
    cl_compiler_init();
    cl_vm_init(0, 0);
    cl_builtins_init();
    cl_repl_init();
}

static void teardown(void)
{
    cl_mem_shutdown();
    platform_shutdown();
}

static const char *eval_print(const char *str)
{
    static char buf[256];
    int err;
    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        CL_Obj result = cl_eval_string(str);
        cl_prin1_to_string(result, buf, sizeof(buf));
        CL_UNCATCH();
        return buf;
    } else {
        CL_UNCATCH();
        snprintf(buf, sizeof(buf), "ERROR:%d", err);
        return buf;
    }
}

TEST(encode_universal_time_absolute)
{
    /* These all fell in the formerly-broken 1901-2000 range (and the boundary
       just after, 2001, which was already correct). */
    ASSERT_STR_EQ(eval_print("(encode-universal-time 0 0 0 1 1 1999 0)"), "3124137600");
    ASSERT_STR_EQ(eval_print("(encode-universal-time 0 0 0 1 1 2000 0)"), "3155673600");
    ASSERT_STR_EQ(eval_print("(encode-universal-time 0 0 0 29 2 2000 0)"), "3160771200");
    ASSERT_STR_EQ(eval_print("(encode-universal-time 0 0 0 1 3 2000 0)"), "3160857600");
    ASSERT_STR_EQ(eval_print("(encode-universal-time 0 0 0 1 1 2001 0)"), "3187296000");
    ASSERT_STR_EQ(eval_print("(encode-universal-time 0 0 0 27 6 2026 0)"), "3991507200");
}

TEST(encode_decode_roundtrip_leap_range)
{
    /* encode must be the inverse of (correct) decode across the leap boundary */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s mi h d mo y)"
        "    (decode-universal-time (encode-universal-time 33 22 11 29 2 2000 0) 0)"
        "  (list s mi h d mo y))"),
        "(33 22 11 29 2 2000)");
    /* a date well after the boundary still round-trips */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s mi h d mo y)"
        "    (decode-universal-time (encode-universal-time 30 45 12 15 6 2025 0) 0)"
        "  (list s mi h d mo y))"),
        "(30 45 12 15 6 2025)");
}

int main(void)
{
    setup();
    RUN(encode_universal_time_absolute);
    RUN(encode_decode_roundtrip_leap_range);
    teardown();
    REPORT();
}
