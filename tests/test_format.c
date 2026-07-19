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

/* FORMATTER (CLHS 22.3.6.4) returns a function usable as a ~? control. */
TEST(formatter_basic)
{
    /* funcall directly */
    ASSERT_STR_EQ(eval_print(
        "(with-output-to-string (s) (funcall (formatter \"~d-~d\") s 3 4))"),
        "\"3-4\"");
    /* via ~? — args passed as a list */
    ASSERT_STR_EQ(eval_print(
        "(format nil \"~?\" (formatter \"~d/~d\") '(7 8))"),
        "\"7/8\"");
}

/* Regression: a ~? whose control is a function (which re-enters the VM via
 * apply) must NOT clobber the parent format's remaining args — a trailing
 * directive after ~? must still read the correct arg.  Before the
 * stable-arg-copy fix the trailing ~A read into the consumed arg list, so
 * (format nil "~?~a" (formatter "...") '(nil 500 t "k") "") printed "500 kT"
 * instead of "500 k" (serapeum FILE-SIZE-HUMAN-READABLE / FORMAT-HUMAN-SIZE). */
TEST(formatter_recursive_no_arg_clobber)
{
    ASSERT_STR_EQ(eval_print(
        "(format nil \"~?~a\" (formatter \"~a~a~a~a\") '(1 2 3 4) \"X\")"),
        "\"1234X\"");
    ASSERT_STR_EQ(eval_print(
        "(format nil \"~?~a\" (formatter \"~:[~d~;~,1f~]~:[~; ~]~a\")"
        " '(nil 500 t \"k\") \"\")"),
        "\"500 k\"");
    ASSERT_STR_EQ(eval_print(
        "(format nil \"~a ~? ~a\" :start (formatter \"~d/~d\") '(7 8) :end)"),
        "\"START 7/8 END\"");
}

/* ~@? with a function control (FORMATTER): the function must return the
 * unconsumed arg tail so the parent format context can advance correctly.
 * Before the fix, ~@? + function consumed ALL remaining parent args,
 * so a trailing directive after ~@? would see NIL instead of its arg. */
TEST(formatter_atsign_function)
{
    /* Trailing ~A must see the arg that the formatter did not consume. */
    ASSERT_STR_EQ(eval_print(
        "(format nil \"~@? and ~A\" (formatter \"~A ~A\") 1 2 3)"),
        "\"1 2 and 3\"");
    /* Formatter consumes 1 arg; trailing ~A gets the second. */
    ASSERT_STR_EQ(eval_print(
        "(format nil \"~@?~A\" (formatter \"~D\") 42 99)"),
        "\"4299\"");
    /* Formatter consumes all available args; no trailing directive. */
    ASSERT_STR_EQ(eval_print(
        "(format nil \"~@?\" (formatter \"~A ~A\") 1 2)"),
        "\"1 2\"");
    /* String control for ~@? still works (regression). */
    ASSERT_STR_EQ(eval_print(
        "(format nil \"~@? and ~A\" \"~A ~A\" 1 2 3)"),
        "\"1 2 and 3\"");
}

/* WITH-OUTPUT-TO-STRING with a target string (CLHS): output is appended to the
 * supplied fill-pointer string and the form returns the body value, not the
 * string.  The macro previously ignored the target and always made a fresh
 * stream (serapeum WITH-STRING). */
TEST(with_output_to_string_target)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((out (make-array 4 :adjustable t :fill-pointer 4"
        "                       :element-type 'character :initial-contents \"the \")))"
        "  (with-output-to-string (s out) (write-string \"string\" s))"
        "  out)"),
        "\"the string\"");
    /* Returns the body value, not the string, when a target is given. */
    ASSERT_STR_EQ(eval_print(
        "(let ((out (make-array 0 :adjustable t :fill-pointer 0 :element-type 'character)))"
        "  (with-output-to-string (s out) (write-string \"x\" s) :body-value))"),
        ":BODY-VALUE");
    /* No target → returns the accumulated string (regression). */
    ASSERT_STR_EQ(eval_print(
        "(with-output-to-string (s) (write-string \"hi\" s))"),
        "\"hi\"");
    /* Non-trivial string-form must be evaluated exactly once, not per character. */
    ASSERT_STR_EQ(eval_print(
        "(let ((n 0))"
        "  (flet ((get-buf () (incf n)"
        "                    (make-array 0 :adjustable t :fill-pointer 0"
        "                                :element-type 'character)))"
        "    (with-output-to-string (s (get-buf)) (write-string \"abc\" s))"
        "    n))"),
        "1");
}

/* ================================================================
 * Tier-4 GC audit batch 2 — format directive regressions
 * ================================================================ */

TEST(grouped_integer_100_digits_no_smash)
{
    /* FS1: ~,,,1:D groups EVERY digit; (expt 10 100) renders 101 digits +
     * 100 separators = 201 bytes, which overran the old 192-byte
     * with_commas[] on the C stack (stack smash).  Verify the full grouped
     * string: length, leading 1, 100 commas, all other digits 0. */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (format nil \"~,,,1:D\" (expt 10 100))))"
        "  (list (length s) (char s 0) (count #\\, s)"
        "        (every (lambda (c) (char= c #\\0)) (remove #\\, (subseq s 1)))))"),
        "(201 #\\1 100 T)");
}

TEST(grouped_integer_grouping_correct)
{
    /* The rewritten single grouping loop must keep ordinary output intact,
     * including custom comma char and interval. */
    ASSERT_STR_EQ(eval_print("(format nil \"~:D\" 1234567)"), "\"1,234,567\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~:D\" -1234567)"), "\"-1,234,567\"");
    ASSERT_STR_EQ(eval_print("(format nil \"~,,'.,2:D\" 123456)"), "\"12.34.56\"");
}

/* ~A/~S rendered their argument into a fixed 512-byte C buffer, silently
 * truncating anything longer at 511 chars (a 2000-char string printed as
 * 511).  The no-padding path must stream unbounded; the padded path must
 * measure the true printed length for the pad math. */
TEST(padded_obj_long_string_no_truncation)
{
    ASSERT_STR_EQ(eval_print(
        "(length (format nil \"~A\" (make-string 2000 :initial-element #\\x)))"),
        "2000");
    /* ~S adds the two quote chars */
    ASSERT_STR_EQ(eval_print(
        "(length (format nil \"~S\" (make-string 2000 :initial-element #\\x)))"),
        "2002");
    /* padded paths: left- and right-justified beyond the old buffer cap */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (format nil \"~2100A\" (make-string 2000 :initial-element #\\x))))"
        "  (list (length s) (char s 1999) (char s 2000)))"),
        "(2100 #\\x #\\Space)");
    ASSERT_STR_EQ(eval_print(
        "(let ((s (format nil \"~2100@A\" (make-string 2000 :initial-element #\\x))))"
        "  (list (length s) (char s 99) (char s 100)))"),
        "(2100 #\\Space #\\x)");
    /* padding shorter than the text leaves it untouched */
    ASSERT_STR_EQ(eval_print("(format nil \"~10A\" \"abc\")"), "\"abc       \"");
    ASSERT_STR_EQ(eval_print("(format nil \"~10@A\" \"abc\")"), "\"       abc\"");
}

/* ~D/~B/~O/~X rendered integers into a fixed 128-byte buffer — a bignum
 * past ~127 digits truncated.  The bignum path now renders through a
 * string stream sized to fit. */
TEST(padded_integer_big_bignum_no_truncation)
{
    ASSERT_STR_EQ(eval_print(
        "(length (format nil \"~D\" (expt 10 200)))"), "201");
    /* grouped: 201 digits + 66 separators */
    ASSERT_STR_EQ(eval_print(
        "(length (format nil \"~:D\" (expt 10 200)))"), "267");
    /* sign survives the bignum path */
    ASSERT_STR_EQ(eval_print(
        "(subseq (format nil \"~:D\" (- (expt 10 200))) 0 2)"), "\"-1\"");
    /* other radices share the path */
    ASSERT_STR_EQ(eval_print(
        "(length (format nil \"~X\" (expt 16 150)))"), "151");
    /* mincol padding on a bignum */
    ASSERT_STR_EQ(eval_print(
        "(length (format nil \"~210D\" (expt 10 200)))"), "210");
}

TEST(goto_negative_and_overshoot_clamp)
{
    /* FS3: plain ~n* with a negative n indexed before the arg vector
     * (ctx->args[-3] OOB read fed to the printer).  Both directions must
     * clamp instead of crashing/reading garbage. */
    ASSERT_STR_EQ(eval_print("(stringp (format nil \"~-5*~A\" 1))"), "T");
    ASSERT_STR_EQ(eval_print("(format nil \"~A~100*x\" 1)"), "\"1x\"");
}

TEST(recursive_format_string_control)
{
    /* FS2: ~? with a string control snapshots the arg list into a C array;
     * behavior check here, GC-stress coverage in the shell suite. */
    ASSERT_STR_EQ(eval_print("(format nil \"~? ~A\" \"<~A ~A>\" (list 1 2) 3)"),
                  "\"<1 2> 3\"");
}

/* --- tier-4 batch 7a: 64-arg format caps removed (FS5) --- */

TEST(recursive_format_over_64_args)
{
    /* ~? (string control) staged the arg list in a sub_args[64] C array —
     * args past 64 were silently dropped.  100 one-digit args through a
     * 100-directive control must all print. */
    ASSERT_STR_EQ(eval_print(
        "(let ((ctrl (with-output-to-string (s)"
        "              (dotimes (i 100) (write-string \"~A\" s)))))"
        "  (length (format nil \"~?\" ctrl (make-list 100 :initial-element 7))))"),
        "100");
    /* ~@? function control with >64 remaining parent args: the parent-arg
     * snapshot (and restore) was capped at 64. */
    ASSERT_STR_EQ(eval_print(
        "(let* ((ctrl (with-output-to-string (s)"
        "               (dotimes (i 70) (write-string \"~A\" s))))"
        "       (f (eval (list 'formatter ctrl))))"
        "  (length (apply #'format nil \"~@?\" f"
        "                 (make-list 70 :initial-element 5))))"),
        "70");
}

TEST(formatter_inner_over_64_args)
{
    /* %FORMATTER-INNER staged stream+ctrl+args in fmt_buf[66] — a FORMATTER
     * function invoked with >64 args silently dropped the tail. */
    ASSERT_STR_EQ(eval_print(
        "(let* ((ctrl (with-output-to-string (s)"
        "               (dotimes (i 100) (write-string \"~A\" s))))"
        "       (f (eval (list 'formatter ctrl))))"
        "  (length (with-output-to-string (s)"
        "            (apply f s (make-list 100 :initial-element 3)))))"),
        "100");
}

TEST(iteration_sublist_over_64_elements)
{
    /* ~:{ / ~:@{ staged each sublist in a sub_args[64] C array — sublist
     * elements past 64 silently vanished from the output. */
    ASSERT_STR_EQ(eval_print(
        "(length (format nil \"~:{~@{~A~}~}\""
        "                (list (make-list 100 :initial-element 1))))"),
        "100");
    ASSERT_STR_EQ(eval_print(
        "(length (format nil \"~:@{~@{~A~}~}\""
        "                (make-list 100 :initial-element 2)))"),
        "100");
}

#ifdef CL_WIDE_STRINGS
TEST(case_convert_wide_string)
{
    /* FS6: ~( ~) cast a wide (UTF-32) intermediate result to CL_String and
     * ran byte-wise ASCII case ops over the code units, garbling non-ASCII
     * text (e.g. U+4E2D's 0x4E byte reads as 'N' and got +32'd).  ASCII
     * chars must still convert; non-ASCII code points pass through intact. */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (coerce (list #\\A (code-char #x4E2D) #\\B) 'string)))"
        "  (let ((r (format nil \"~(~A~)\" s)))"
        "    (list (length r) (char r 0)"
        "          (char= (char r 1) (code-char #x4E2D)) (char r 2))))"),
        "(3 #\\a T #\\b)");
    ASSERT_STR_EQ(eval_print(
        "(let ((s (coerce (list #\\a (code-char #x4E2D) #\\b) 'string)))"
        "  (let ((r (format nil \"~:@(~A~)\" s)))"
        "    (list (char r 0) (char= (char r 1) (code-char #x4E2D))"
        "          (char r 2))))"),
        "(#\\A T #\\B)");
}

TEST(case_convert_wide_capitalize_each_word)
{
    /* ~:( capitalize-each-word: a non-ASCII code point is not alpha, so it
     * acts as a word separator (matching the narrow path's behavior for
     * non-alpha bytes) and each side gets its own initial capitalized. */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (coerce (list #\\a (code-char #x4E2D) #\\b) 'string)))"
        "  (let ((r (format nil \"~:(~A~)\" s)))"
        "    (list (length r) (char r 0)"
        "          (char= (char r 1) (code-char #x4E2D)) (char r 2))))"),
        "(3 #\\A T #\\B)");
}

TEST(case_convert_wide_capitalize_first_word)
{
    /* ~@( capitalize-first-word: only the very first alpha character is
     * uppercased; a non-ASCII code point does not reset the "first word"
     * tracking (matching the narrow path's whitespace handling). */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (coerce (list #\\A #\\B #\\Space (code-char #x4E2D)"
        "                        #\\C #\\D) 'string)))"
        "  (let ((r (format nil \"~@(~A~)\" s)))"
        "    (list (length r) (char r 0) (char r 1) (char r 2)"
        "          (char= (char r 3) (code-char #x4E2D)) (char r 4)"
        "          (char r 5))))"),
        "(6 #\\A #\\b #\\Space T #\\c #\\d)");
}
#endif

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

    RUN(formatter_basic);
    RUN(formatter_recursive_no_arg_clobber);
    RUN(formatter_atsign_function);
    RUN(with_output_to_string_target);

    RUN(princ_wide_char);
    RUN(princ_wide_string);
    RUN(format_a_wide_string);
    RUN(with_output_to_string_wide);
    RUN(justify_wide_glyph_segments);

    RUN(grouped_integer_100_digits_no_smash);
    RUN(grouped_integer_grouping_correct);
    RUN(padded_obj_long_string_no_truncation);
    RUN(padded_integer_big_bignum_no_truncation);
    RUN(goto_negative_and_overshoot_clamp);
    RUN(recursive_format_string_control);

    RUN(recursive_format_over_64_args);
    RUN(formatter_inner_over_64_args);
    RUN(iteration_sublist_over_64_elements);
#ifdef CL_WIDE_STRINGS
    RUN(case_convert_wide_string);
    RUN(case_convert_wide_capitalize_each_word);
    RUN(case_convert_wide_capitalize_first_word);
#endif

    teardown();
    REPORT();
}
