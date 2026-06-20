/* Regression tests for FORMAT directives and string printing.
 *
 * Covers two bugs surfaced by the cl-spark test suite:
 *
 *  1. ~mincol,colinc,minpad,padchar<...~>  justification (CLHS 22.3.6.2)
 *     was a no-op stub that emitted the segments with the literal ~;
 *     separators and no padding.  cl-spark's vspark scale line
 *     (~V<~A~;~A~;~A~>) came out unpadded.
 *
 *  2. PRINC of a wide string (TYPE_WIDE_STRING) substituted '?' for any
 *     character >= 128 (a "TODO Phase 2" stub in the printer).  This hit
 *     WITH-OUTPUT-TO-STRING / FORMAT ~A of any string holding non-Latin-1
 *     glyphs — e.g. the vspark scale glyphs ˫ (U+02EB) and ˧ (U+02E7).
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

/* Eval a string and return the prin1 representation of the result. */
static const char *eval_print(const char *str)
{
    static char buf[1024];
    int err;

    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        CL_Obj result = cl_eval_string(str);
        cl_prin1_to_string(result, buf, sizeof(buf));
        CL_UNCATCH();
        return buf;
    } else {
        CL_UNCATCH();
        cl_vm.sp = 0;
        cl_vm.fp = 0;
        snprintf(buf, sizeof(buf), "ERROR:%d", err);
        return buf;
    }
}

/* ================================================================
 * ~<...~> justification
 * ================================================================ */

TEST(justify_three_segments)
{
    /* width 30, text "0"+"75"+"150" = 6, padding 24 over 2 gaps = 12 each. */
    ASSERT_STR_EQ(eval_print("(format nil \"~30<~A~;~A~;~A~>\" \"0\" \"75\" \"150\")"),
                  "\"0            75            150\"");
}

TEST(justify_padchar)
{
    /* Same but with '-' as the pad character (the scale's second line). */
    ASSERT_STR_EQ(eval_print("(format nil \"~30,,,'-<~A~;~A~;~A~>\" \"0\" \"75\" \"150\")"),
                  "\"0------------75------------150\"");
}

TEST(justify_uneven_padding)
{
    /* SBCL reference from cl-spark/spark-test.lisp: the later gap gets the
     * extra column.  text 2+3+2=7, padding 23 over 2 gaps -> 11 then 12. */
    ASSERT_STR_EQ(eval_print("(format nil \"~30,,,'-<~A~;~A~;~A~>\" 11 222 33)"),
                  "\"11-----------222------------33\"");
}

TEST(justify_single_segment_right)
{
    /* A single segment with no modifiers is right-justified. */
    ASSERT_STR_EQ(eval_print("(format nil \"~10<~A~>\" \"hi\")"),
                  "\"        hi\"");
}

TEST(justify_atsign_left)
{
    /* @ pads after the last segment (left-justify a single segment). */
    ASSERT_STR_EQ(eval_print("(format nil \"~10@<~A~>\" \"hi\")"),
                  "\"hi        \"");
}

TEST(justify_colon_before)
{
    /* : pads before the first segment as well. text 4, pad 6 over 2 gaps. */
    ASSERT_STR_EQ(eval_print("(format nil \"~10:<~A~;~A~>\" \"lo\" \"hi\")"),
                  "\"   lo   hi\"");
}

TEST(justify_v_param)
{
    /* mincol supplied via ~V (consumes an argument). */
    ASSERT_STR_EQ(eval_print("(format nil \"~V<~A~;~A~;~A~>\" 30 \"0\" \"75\" \"150\")"),
                  "\"0            75            150\"");
}

TEST(justify_no_mincol_concatenates)
{
    /* No mincol: padding is zero, segments are emitted in order (this is the
     * ~<...~> logical-block degenerate case used by some pretty-printers). */
    ASSERT_STR_EQ(eval_print("(format nil \"[~<~A~;~A~>]\" \"a\" \"b\")"),
                  "\"[ab]\"");
}

/* ================================================================
 * Wide-string output through PRINC / FORMAT ~A
 * ================================================================ */

TEST(princ_wide_char)
{
    /* A lone wide char prints as its codepoint, not '?'. */
    ASSERT_STR_EQ(eval_print("(char-code (char (princ-to-string (code-char 747)) 0))"),
                  "747");
}

TEST(princ_wide_string)
{
    /* PRINC of a STRING containing a wide char must not substitute '?'. */
    ASSERT_STR_EQ(eval_print(
        "(char-code (char (princ-to-string (string (code-char 747))) 0))"),
                  "747");
}

TEST(format_a_wide_string)
{
    /* FORMAT ~A of a wide string preserves the codepoint. */
    ASSERT_STR_EQ(eval_print(
        "(char-code (char (format nil \"~A\" (string (code-char 747))) 0))"),
                  "747");
}

TEST(with_output_to_string_wide)
{
    /* WITH-OUTPUT-TO-STRING + PRINC of a wide-char string (the cl-spark
     * string-concat path). */
    ASSERT_STR_EQ(eval_print(
        "(char-code (char (with-output-to-string (s)"
        " (princ (string (code-char 743)) s)) 0))"),
                  "743");
}

TEST(justify_wide_glyph_segments)
{
    /* The exact vspark scale second line: wide glyphs ˫ + ˧ justified with
     * '-' padding.  The first/last chars must be the wide codepoints. */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (format nil \"~10,,,'-<~A~;~A~;~A~>\""
        " (code-char 747) #\\+ (code-char 743))))"
        " (list (char-code (char s 0)) (char-code (char s (1- (length s))))))"),
                  "(747 743)");
}

int main(void)
{
    setup();

    RUN(justify_three_segments);
    RUN(justify_padchar);
    RUN(justify_uneven_padding);
    RUN(justify_single_segment_right);
    RUN(justify_atsign_left);
    RUN(justify_colon_before);
    RUN(justify_v_param);
    RUN(justify_no_mincol_concatenates);

    RUN(princ_wide_char);
    RUN(princ_wide_string);
    RUN(format_a_wide_string);
    RUN(with_output_to_string_wide);
    RUN(justify_wide_glyph_segments);

    teardown();
    REPORT();
}
