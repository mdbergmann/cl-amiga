/*
 * test_amiga_ffi.c — host-side specification of the AMIGA package surface
 * that the OS-binding modules (lib/amiga/ffi.lisp, lib/amiga/raw/) rely on.
 *
 * What is pinned here:
 *   - cl_amiga_box_result: the d0 -> Lisp boxing per result kind
 *     (unsigned / void / pointer / signed) shared by OP_AMIGA_CALL, the JIT
 *     and CALL-LIBRARY*.  The encoding is platform-neutral, so it is tested
 *     on the host even though no library call can be made here.
 *   - The AMIGA package exists on every platform with the same builtin
 *     names; on the host they signal a clear "only available on
 *     AmigaOS/MorphOS" error instead of being undefined.
 *   - AMIGA.FFI:DEFCFUN's regspec encoding: register nibbles, result kind
 *     in bits 28-29, the Lisp-side post-processors for BOOL/WORD/BYTE
 *     results, the >7-register plist fallback, the :A5 rejection.
 *   - FFI:DEFCSTRUCT's :fptr / (:struct N) / (:array T N) / :size.
 *
 * The Amiga-side half (calls that actually reach the OS) lives in
 * tests/amiga/test-raw-bindings.lisp.
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
#include "core/thread.h"
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

/* Eval STR; return the printed result, or "ERROR:<msg>" on a signalled
 * error (the message text is what the tests below match against). */
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
        snprintf(buf, sizeof(buf), "ERROR:%s", cl_error_msg);
        return buf;
    }
}

static int contains(const char *haystack, const char *needle)
{
    return strstr(haystack, needle) != NULL;
}

/* ================================================================
 * cl_amiga_box_result
 * ================================================================ */

TEST(box_unsigned_small_is_fixnum)
{
    CL_Obj r = cl_amiga_box_result(42, CL_AMIGA_RES_UNSIGNED);
    ASSERT(CL_FIXNUM_P(r));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r), 42);
}

TEST(box_unsigned_max_fixnum_boundary)
{
    CL_Obj r = cl_amiga_box_result((uint32_t)CL_FIXNUM_MAX, CL_AMIGA_RES_UNSIGNED);
    ASSERT(CL_FIXNUM_P(r));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r), CL_FIXNUM_MAX);
}

TEST(box_unsigned_large_is_positive_bignum)
{
    /* 0xFFFFFFFF as ULONG must stay 4294967295, never -1 */
    CL_Obj r = cl_amiga_box_result(0xFFFFFFFFu, CL_AMIGA_RES_UNSIGNED);
    CL_Bignum *b;
    ASSERT(CL_BIGNUM_P(r));
    b = (CL_Bignum *)CL_OBJ_TO_PTR(r);
    ASSERT_EQ_INT(b->sign, 0);
    ASSERT_EQ_INT(b->length, 2);
    ASSERT_EQ_INT(b->limbs[0], 0xFFFF);
    ASSERT_EQ_INT(b->limbs[1], 0xFFFF);
}

TEST(box_unsigned_just_above_fixnum)
{
    CL_Obj r = cl_amiga_box_result((uint32_t)CL_FIXNUM_MAX + 1u, CL_AMIGA_RES_UNSIGNED);
    CL_Bignum *b;
    ASSERT(CL_BIGNUM_P(r));
    b = (CL_Bignum *)CL_OBJ_TO_PTR(r);
    ASSERT_EQ_INT(b->sign, 0);
    ASSERT_EQ_INT(b->limbs[0], 0x0000);
    ASSERT_EQ_INT(b->limbs[1], 0x4000);
}

TEST(box_void_is_nil)
{
    ASSERT(CL_NULL_P(cl_amiga_box_result(12345, CL_AMIGA_RES_VOID)));
    ASSERT(CL_NULL_P(cl_amiga_box_result(0, CL_AMIGA_RES_VOID)));
}

TEST(box_pointer_null_is_nil)
{
    ASSERT(CL_NULL_P(cl_amiga_box_result(0, CL_AMIGA_RES_POINTER)));
}

TEST(box_pointer_nonnull_is_foreign_pointer)
{
    CL_Obj r = cl_amiga_box_result(0x07F00010u, CL_AMIGA_RES_POINTER);
    CL_ForeignPtr *p;
    ASSERT(CL_FOREIGN_POINTER_P(r));
    p = (CL_ForeignPtr *)CL_OBJ_TO_PTR(r);
    ASSERT_EQ_INT(p->address, (uint32_t)0x07F00010u);
    ASSERT_EQ_INT(p->size, 0);
    ASSERT_EQ_INT(p->flags, 0);       /* unowned: the OS owns the object */
}

TEST(box_pointer_high_address)
{
    /* Zorro-III / MorphOS addresses above 2^30 must not lose bits */
    CL_Obj r = cl_amiga_box_result(0xC0001234u, CL_AMIGA_RES_POINTER);
    ASSERT(CL_FOREIGN_POINTER_P(r));
    ASSERT_EQ_INT(((CL_ForeignPtr *)CL_OBJ_TO_PTR(r))->address, (uint32_t)0xC0001234u);
}

TEST(box_signed_minus_one)
{
    /* the LONG -1 that used to come back as 4294967295 */
    CL_Obj r = cl_amiga_box_result(0xFFFFFFFFu, CL_AMIGA_RES_SIGNED);
    ASSERT(CL_FIXNUM_P(r));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r), -1);
}

TEST(box_signed_positive_small)
{
    CL_Obj r = cl_amiga_box_result(1000, CL_AMIGA_RES_SIGNED);
    ASSERT(CL_FIXNUM_P(r));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r), 1000);
}

TEST(box_signed_fixnum_min_boundary)
{
    CL_Obj r = cl_amiga_box_result((uint32_t)CL_FIXNUM_MIN, CL_AMIGA_RES_SIGNED);
    ASSERT(CL_FIXNUM_P(r));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r), CL_FIXNUM_MIN);
}

TEST(box_signed_large_negative_is_negative_bignum)
{
    /* -2^30 - 1 = 0xBFFFFFFF */
    CL_Obj r = cl_amiga_box_result(0xBFFFFFFFu, CL_AMIGA_RES_SIGNED);
    CL_Bignum *b;
    ASSERT(CL_BIGNUM_P(r));
    b = (CL_Bignum *)CL_OBJ_TO_PTR(r);
    ASSERT_EQ_INT(b->sign, 1);
    ASSERT_EQ_INT(b->limbs[0], 0x0001);
    ASSERT_EQ_INT(b->limbs[1], 0x4000);   /* magnitude 0x40000001 */
}

TEST(box_signed_int32_min)
{
    CL_Obj r = cl_amiga_box_result(0x80000000u, CL_AMIGA_RES_SIGNED);
    CL_Bignum *b;
    ASSERT(CL_BIGNUM_P(r));
    b = (CL_Bignum *)CL_OBJ_TO_PTR(r);
    ASSERT_EQ_INT(b->sign, 1);
    ASSERT_EQ_INT(b->limbs[0], 0x0000);
    ASSERT_EQ_INT(b->limbs[1], 0x8000);
}

TEST(box_signed_large_positive_is_positive_bignum)
{
    CL_Obj r = cl_amiga_box_result(0x7FFFFFFFu, CL_AMIGA_RES_SIGNED);
    CL_Bignum *b;
    ASSERT(CL_BIGNUM_P(r));
    b = (CL_Bignum *)CL_OBJ_TO_PTR(r);
    ASSERT_EQ_INT(b->sign, 0);
    ASSERT_EQ_INT(b->limbs[0], 0xFFFF);
    ASSERT_EQ_INT(b->limbs[1], 0x7FFF);
}

TEST(res_kind_macro_decodes_bits_28_29)
{
    ASSERT_EQ_INT(CL_AMIGA_RES_KIND(0x00000008u), CL_AMIGA_RES_UNSIGNED);
    ASSERT_EQ_INT(CL_AMIGA_RES_KIND(0x10000008u), CL_AMIGA_RES_VOID);
    ASSERT_EQ_INT(CL_AMIGA_RES_KIND(0x20000008u), CL_AMIGA_RES_POINTER);
    ASSERT_EQ_INT(CL_AMIGA_RES_KIND(0x30000008u), CL_AMIGA_RES_SIGNED);
    /* a regspec with every kind bit set still fits a positive fixnum */
    ASSERT(0x3FFFFFFFu <= (uint32_t)CL_FIXNUM_MAX);
}

/* ================================================================
 * AMIGA package surface on the host
 * ================================================================ */

TEST(amiga_package_exists_on_host)
{
    ASSERT_STR_EQ(eval_print("(package-name (find-package \"AMIGA\"))"), "\"AMIGA\"");
    ASSERT_STR_EQ(eval_print("(and (fboundp 'amiga:open-library) "
                             "(fboundp 'amiga:close-library) "
                             "(fboundp 'amiga:call-library) "
                             "(fboundp 'amiga:call-library-fast) "
                             "(fboundp 'amiga:alloc-chip) "
                             "(fboundp 'amiga:free-chip) "
                             "(fboundp 'amiga:arexx-open) t)"), "T");
    ASSERT_STR_EQ(eval_print("(symbol-package 'amiga:%ffi-call)"), "#<PACKAGE AMIGA>");
}

TEST(amiga_stub_signals_platform_error)
{
    const char *r = eval_print("(amiga:open-library \"intuition.library\" 0)");
    ASSERT(contains(r, "ERROR:"));
    ASSERT(contains(r, "OPEN-LIBRARY"));
    ASSERT(contains(r, "only available on AmigaOS/MorphOS"));
    r = eval_print("(amiga:call-library (ffi:make-foreign-pointer 4) -216 (list :d1 0))");
    ASSERT(contains(r, "CALL-LIBRARY"));
    ASSERT(contains(r, "only available on AmigaOS/MorphOS"));
}

TEST(amiga_call_library_arity_accepts_result_kind)
{
    /* 3..4 args: the 4th is the result kind (the stub still checks arity
     * before erroring, so a 5th argument is an arity error, not the
     * platform error) */
    const char *r = eval_print("(amiga:call-library 1 2 3 4 5)");
    ASSERT(contains(r, "ERROR:"));
    ASSERT(!contains(r, "only available"));
}

/* ================================================================
 * AMIGA.FFI:DEFCFUN encoding
 * ================================================================ */

TEST(defcfun_regspec_nibbles_and_kinds)
{
    ASSERT_STR_EQ(eval_print("(require \"amiga/ffi\")"), "T");
    ASSERT_STR_EQ(eval_print("(defvar cl-user::*tb* nil)"), "*TB*");
    /* a0 -> nibble 8, d0 -> nibble 0 in the next position; void = kind 1 */
    ASSERT_STR_EQ(eval_print(
        "(fourth (second (macroexpand-1 "
        "'(amiga.ffi:defcfun cl-user::f1 cl-user::*tb* -30 (:a0 x :d0 y) :void t))))"),
        "(%FFI-CALL *TB* -30 268435464 X Y)");
    /* :result :pointer = kind 2 */
    ASSERT(contains(eval_print(
        "(macroexpand-1 '(amiga.ffi:defcfun cl-user::f2 cl-user::*tb* -36 (:a0 x) :result :pointer))"),
        "-36 536870920 X"));
    /* :result :signed = kind 3, d1 -> nibble 1 */
    ASSERT(contains(eval_print(
        "(macroexpand-1 '(amiga.ffi:defcfun cl-user::f3 cl-user::*tb* -42 (:d1 x) :result :signed))"),
        "-42 805306369 X"));
    /* :result :unsigned (default) = kind 0 */
    ASSERT(contains(eval_print(
        "(macroexpand-1 '(amiga.ffi:defcfun cl-user::f4 cl-user::*tb* -48 (:d0 a :d1 b :a0 c)))"),
        "-48 2064 A B C"));
}

TEST(defcfun_lisp_side_post_processors)
{
    /* :bool wraps an unsigned result in %RESULT-BOOL, in both the wrapper
     * and the compiler-macro expansion */
    const char *r = eval_print(
        "(macroexpand-1 '(amiga.ffi:defcfun cl-user::f5 cl-user::*tb* -54 (:d0 x) :result :bool))");
    ASSERT(contains(r, "(AMIGA.FFI::%RESULT-BOOL (%FFI-CALL *TB* -54 0 X))"));
    ASSERT(contains(r, "(LIST (QUOTE AMIGA.FFI::%RESULT-BOOL)"));
    /* the helpers themselves: only d0.w / d0.b are defined for BOOL/WORD/BYTE */
    ASSERT_STR_EQ(eval_print("(amiga.ffi::%result-bool #x10000)"), "NIL");
    ASSERT_STR_EQ(eval_print("(amiga.ffi::%result-bool #x1)"), "T");
    ASSERT_STR_EQ(eval_print("(amiga.ffi::%result-bool #xFFFFFFFF)"), "T");
    ASSERT_STR_EQ(eval_print("(amiga.ffi::%result-i16 #xFFFF)"), "-1");
    ASSERT_STR_EQ(eval_print("(amiga.ffi::%result-i16 #x12347FFF)"), "32767");
    ASSERT_STR_EQ(eval_print("(amiga.ffi::%result-u16 #x1234FFFF)"), "65535");
    ASSERT_STR_EQ(eval_print("(amiga.ffi::%result-i8 200)"), "-56");
    ASSERT_STR_EQ(eval_print("(amiga.ffi::%result-u8 #x1FF)"), "255");
}

TEST(defcfun_more_than_seven_registers_uses_call_library)
{
    const char *r = eval_print(
        "(macroexpand-1 '(amiga.ffi:defcfun cl-user::f6 cl-user::*tb* -60 "
        "(:a0 a :d0 b :d1 c :a1 d :d2 e :d3 f :d4 g :d5 h) :result :pointer))");
    ASSERT(contains(r, "(CALL-LIBRARY *TB* -60 (LIST :A0 A :D0 B :D1 C :A1 D :D2 E :D3 F :D4 G :D5 H) 2)"));
    /* no compiler macro on the plist path */
    ASSERT(!contains(r, "DEFINE-COMPILER-MACRO"));
}

TEST(defcfun_rejects_a5)
{
    const char *r = eval_print(
        "(macroexpand-1 '(amiga.ffi:defcfun cl-user::f7 cl-user::*tb* -66 (:a5 x)))");
    ASSERT(contains(r, "ERROR:"));
    ASSERT(contains(r, ":A5"));
}

TEST(defcfun_compiles_and_reports_nil_base_on_host)
{
    ASSERT_STR_EQ(eval_print("(amiga.ffi:defcfun cl-user::f8 cl-user::*tb* -30 (:a0 x) :result :pointer)"), "F8");
    {
        const char *r = eval_print("(cl-user::f8 1)");
        ASSERT(contains(r, "ERROR:"));
        ASSERT(contains(r, "*TB*"));
        ASSERT(contains(r, "not open"));
    }
}

TEST(library_version_reads_lib_version_field)
{
    /* struct Library: lib_Version is the UWORD at offset 20 */
    ASSERT_STR_EQ(eval_print(
        "(let ((m (ffi:alloc-foreign 34))) "
        "  (ffi:poke-u16 m 47 20) "
        "  (prog1 (amiga.ffi:library-version m) (ffi:free-foreign m)))"), "47");
}

/* ================================================================
 * FFI:DEFCSTRUCT extensions
 * ================================================================ */

TEST(defcstruct_fptr_struct_array_size)
{
    eval_print("(require \"ffi\")");   /* already provided via amiga/ffi */
    ASSERT_STR_EQ(eval_print(
        "(ffi:defcstruct (cl-user::tst :size 32) "
        "  (cl-user::x :i16 0) (cl-user::y :u16 2) (cl-user::p :fptr 4) "
        "  (cl-user::sub (:struct 8) 8) (cl-user::arr (:array :u8 4) 16) (cl-user::f :single 20))"),
        "TST");
    ASSERT_STR_EQ(eval_print("cl-user::*tst-size*"), "32");   /* explicit, not 24 */
    ASSERT_STR_EQ(eval_print(
        "(let ((m (ffi:alloc-foreign 32))) "
        "  (setf (cl-user::tst-x m) -5 (cl-user::tst-y m) 65535 (cl-user::tst-p m) nil) "
        "  (setf (cl-user::tst-arr m 3) 77) "
        "  (prog1 (list (cl-user::tst-x m) (cl-user::tst-y m) (cl-user::tst-p m) "
        "               (cl-user::tst-arr m 3) (ffi:peek-u8 m 19) "
        "               (ffi:foreign-pointer-p (cl-user::tst-sub m)) "
        "               (- (ffi:foreign-pointer-address (cl-user::tst-sub m)) "
        "                  (ffi:foreign-pointer-address m))) "
        "    (ffi:free-foreign m)))"),
        "(-5 65535 NIL 77 77 T 8)");
    /* :fptr setter takes a foreign pointer, an integer or NIL; getter gives NIL for NULL */
    ASSERT_STR_EQ(eval_print(
        "(let ((m (ffi:alloc-foreign 32))) "
        "  (setf (cl-user::tst-p m) 1234) "
        "  (prog1 (list (ffi:foreign-pointer-address (cl-user::tst-p m)) "
        "               (progn (setf (cl-user::tst-p m) (ffi:make-foreign-pointer 99)) "
        "                      (ffi:foreign-pointer-address (cl-user::tst-p m))) "
        "               (progn (setf (cl-user::tst-p m) nil) (cl-user::tst-p m))) "
        "    (ffi:free-foreign m)))"),
        "(1234 99 NIL)");
}

TEST(defcstruct_derived_size_and_legacy_pointer)
{
    ASSERT_STR_EQ(eval_print(
        "(ffi:defcstruct cl-user::leg (cl-user::a :u8 0) (cl-user::ptr :pointer 4))"), "LEG");
    ASSERT_STR_EQ(eval_print("cl-user::*leg-size*"), "8");
    /* :pointer keeps returning the raw integer address */
    ASSERT_STR_EQ(eval_print(
        "(let ((m (ffi:alloc-foreign 8))) "
        "  (setf (cl-user::leg-ptr m) 4660) "
        "  (prog1 (cl-user::leg-ptr m) (ffi:free-foreign m)))"), "4660");
}

TEST(defcstruct_rejects_unknown_type)
{
    ASSERT(contains(eval_print("(ffi:defcstruct cl-user::bad (cl-user::a :u128 0))"), "ERROR:"));
    ASSERT(contains(eval_print("(ffi:defcstruct cl-user::bad2 (cl-user::a (:array :u8 0) 0))"), "ERROR:"));
}

int main(void)
{
    setup();

    printf("--- test_amiga_ffi ---\n");

    RUN(box_unsigned_small_is_fixnum);
    RUN(box_unsigned_max_fixnum_boundary);
    RUN(box_unsigned_large_is_positive_bignum);
    RUN(box_unsigned_just_above_fixnum);
    RUN(box_void_is_nil);
    RUN(box_pointer_null_is_nil);
    RUN(box_pointer_nonnull_is_foreign_pointer);
    RUN(box_pointer_high_address);
    RUN(box_signed_minus_one);
    RUN(box_signed_positive_small);
    RUN(box_signed_fixnum_min_boundary);
    RUN(box_signed_large_negative_is_negative_bignum);
    RUN(box_signed_int32_min);
    RUN(box_signed_large_positive_is_positive_bignum);
    RUN(res_kind_macro_decodes_bits_28_29);

    RUN(amiga_package_exists_on_host);
    RUN(amiga_stub_signals_platform_error);
    RUN(amiga_call_library_arity_accepts_result_kind);

    RUN(defcfun_regspec_nibbles_and_kinds);
    RUN(defcfun_lisp_side_post_processors);
    RUN(defcfun_more_than_seven_registers_uses_call_library);
    RUN(defcfun_rejects_a5);
    RUN(defcfun_compiles_and_reports_nil_base_on_host);
    RUN(library_version_reads_lib_version_field);

    RUN(defcstruct_fptr_struct_array_size);
    RUN(defcstruct_derived_size_and_legacy_pointer);
    RUN(defcstruct_rejects_unknown_type);

    REPORT();
    teardown();
    return test_fail > 0 ? 1 : 0;
}
