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

/* Helper: eval a string, return printed result */
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

/* ================================================================
 * C-level tests: TYPE_FOREIGN_POINTER
 * ================================================================ */

TEST(c_foreign_pointer_create)
{
    CL_Obj fp = cl_make_foreign_pointer(0xDEADBEEF, 16, 0);
    ASSERT(CL_FOREIGN_POINTER_P(fp));
    CL_ForeignPtr *p = (CL_ForeignPtr *)CL_OBJ_TO_PTR(fp);
    ASSERT_EQ_INT(p->address, (uint32_t)0xDEADBEEF);
    ASSERT_EQ_INT(p->size, 16);
    ASSERT_EQ_INT(p->flags, 0);
}

TEST(c_foreign_pointer_type_name)
{
    CL_Obj fp = cl_make_foreign_pointer(42, 0, 0);
    ASSERT_STR_EQ(cl_type_name(fp), "FOREIGN-POINTER");
}

TEST(c_foreign_pointer_not_fixnum)
{
    CL_Obj fp = cl_make_foreign_pointer(1, 0, 0);
    ASSERT(!CL_FIXNUM_P(fp));
    ASSERT(!CL_NULL_P(fp));
    ASSERT(!CL_CHAR_P(fp));
    ASSERT(CL_HEAP_P(fp));
}

TEST(c_foreign_pointer_zero_addr)
{
    CL_Obj fp = cl_make_foreign_pointer(0, 0, 0);
    CL_ForeignPtr *p = (CL_ForeignPtr *)CL_OBJ_TO_PTR(fp);
    ASSERT_EQ_INT(p->address, 0);
}

TEST(c_foreign_pointer_gc_survives)
{
    CL_Obj fp = cl_make_foreign_pointer(0x12345678, 32, CL_FPTR_FLAG_OWNED);
    CL_GC_PROTECT(fp);
    cl_gc();
    CL_ForeignPtr *p = (CL_ForeignPtr *)CL_OBJ_TO_PTR(fp);
    ASSERT_EQ_INT(p->address, (uint32_t)0x12345678);
    ASSERT_EQ_INT(p->size, 32);
    ASSERT_EQ_INT(p->flags, CL_FPTR_FLAG_OWNED);
    CL_GC_UNPROTECT(1);
}

/* ================================================================
 * Lisp-level tests: FFI package
 * ================================================================ */

TEST(lisp_ffi_package_exists)
{
    ASSERT_STR_EQ(eval_print("(package-name (find-package \"FFI\"))"), "\"FFI\"");
}

TEST(lisp_ffi_make_foreign_pointer)
{
    /* type-of should return FOREIGN-POINTER */
    ASSERT_STR_EQ(eval_print("(type-of (ffi:make-foreign-pointer 42))"),
                  "FOREIGN-POINTER");
}

TEST(lisp_ffi_foreign_pointer_p)
{
    ASSERT_STR_EQ(eval_print("(ffi:foreign-pointer-p (ffi:make-foreign-pointer 0))"), "T");
    ASSERT_STR_EQ(eval_print("(ffi:foreign-pointer-p 42)"), "NIL");
    ASSERT_STR_EQ(eval_print("(ffi:foreign-pointer-p nil)"), "NIL");
    ASSERT_STR_EQ(eval_print("(ffi:foreign-pointer-p \"hello\")"), "NIL");
}

TEST(lisp_ffi_null_pointer_p)
{
    ASSERT_STR_EQ(eval_print("(ffi:null-pointer-p (ffi:make-foreign-pointer 0))"), "T");
    ASSERT_STR_EQ(eval_print("(ffi:null-pointer-p (ffi:make-foreign-pointer 42))"), "NIL");
}

TEST(lisp_ffi_foreign_pointer_address)
{
    ASSERT_STR_EQ(eval_print("(ffi:foreign-pointer-address (ffi:make-foreign-pointer 42))"), "42");
    ASSERT_STR_EQ(eval_print("(ffi:foreign-pointer-address (ffi:make-foreign-pointer 0))"), "0");
}

TEST(lisp_ffi_alloc_free)
{
    /* Alloc should return a non-null foreign pointer */
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 64)))"
        "  (prog1 (ffi:null-pointer-p p)"
        "    (ffi:free-foreign p)))"),
        "NIL");
}

TEST(lisp_ffi_peek_poke_u32)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 16)))"
        "  (ffi:poke-u32 p 42)"
        "  (prog1 (ffi:peek-u32 p)"
        "    (ffi:free-foreign p)))"),
        "42");
}

TEST(lisp_ffi_peek_poke_u32_offset)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 16)))"
        "  (ffi:poke-u32 p 100 0)"
        "  (ffi:poke-u32 p 200 4)"
        "  (prog1 (+ (ffi:peek-u32 p 0) (ffi:peek-u32 p 4))"
        "    (ffi:free-foreign p)))"),
        "300");
}

TEST(lisp_ffi_peek_poke_u16)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 16)))"
        "  (ffi:poke-u16 p 1234)"
        "  (prog1 (ffi:peek-u16 p)"
        "    (ffi:free-foreign p)))"),
        "1234");
}

TEST(lisp_ffi_peek_poke_u8)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 16)))"
        "  (ffi:poke-u8 p 255)"
        "  (prog1 (ffi:peek-u8 p)"
        "    (ffi:free-foreign p)))"),
        "255");
}

/* --- Bulk byte transfer: POKE-BYTES / PEEK-BYTES --- */

/* A vector source unpacks tagged fixnums; note clamiga upgrades
 * (UNSIGNED-BYTE 8) to T, so these really are boxed elements. */
TEST(lisp_ffi_poke_bytes_vector)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 16))"
        "      (v (make-array 4 :element-type '(unsigned-byte 8)"
        "                       :initial-contents '(1 2 254 255))))"
        "  (let ((n (ffi:poke-bytes p v)))"
        "    (prog1 (list n (ffi:peek-u8 p 0) (ffi:peek-u8 p 1)"
        "                 (ffi:peek-u8 p 2) (ffi:peek-u8 p 3))"
        "      (ffi:free-foreign p))))"),
        "(4 1 2 254 255)");
}

/* A string source takes the memcpy path. */
TEST(lisp_ffi_poke_bytes_string)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 16)))"
        "  (let ((n (ffi:poke-bytes p \"AB\" 8)))"
        "    (prog1 (list n (ffi:peek-u8 p 8) (ffi:peek-u8 p 9))"
        "      (ffi:free-foreign p))))"),
        "(2 65 66)");
}

/* OFFSET applies to the destination, START/END bound the source. */
TEST(lisp_ffi_poke_bytes_offset_start_end)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 16))"
        "      (v (make-array 4 :element-type '(unsigned-byte 8)"
        "                       :initial-contents '(1 2 254 255))))"
        "  (let ((n (ffi:poke-bytes p v 12 1 3)))"
        "    (prog1 (list n (ffi:peek-u8 p 12) (ffi:peek-u8 p 13))"
        "      (ffi:free-foreign p))))"),
        "(2 2 254)");
}

/* An empty span is legal and writes nothing. */
TEST(lisp_ffi_poke_bytes_empty)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 16))"
        "      (v (make-array 4 :element-type '(unsigned-byte 8)"
        "                       :initial-element 7)))"
        "  (ffi:poke-u8 p 99)"
        "  (let ((n (ffi:poke-bytes p v 0 2 2)))"
        "    (prog1 (list n (ffi:peek-u8 p 0))"
        "      (ffi:free-foreign p))))"),
        "(0 99)");
}

TEST(lisp_ffi_peek_bytes_roundtrip)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 16))"
        "      (v (make-array 3 :element-type '(unsigned-byte 8)"
        "                       :initial-contents '(9 0 200)))"
        "      (out (make-array 3 :element-type '(unsigned-byte 8)"
        "                         :initial-element 0)))"
        "  (ffi:poke-bytes p v)"
        "  (let ((n (ffi:peek-bytes p out)))"
        "    (prog1 (list n (coerce out 'list))"
        "      (ffi:free-foreign p))))"),
        "(3 (9 0 200))");
}

/* A known allocation size must reject an overrun rather than corrupt
 * memory past the buffer. */
TEST(lisp_ffi_poke_bytes_overrun_rejected)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 4))"
        "      (v (make-array 4 :element-type '(unsigned-byte 8)"
        "                       :initial-element 1)))"
        "  (prog1 (handler-case (progn (ffi:poke-bytes p v 2) :no-error)"
        "           (error () :rejected))"
        "    (ffi:free-foreign p)))"),
        ":REJECTED");
}

/* The read-side mirror of the overrun check above: a known allocation size
 * must reject an overrun rather than read past the buffer. */
TEST(lisp_ffi_peek_bytes_overrun_rejected)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 4))"
        "      (out (make-array 4 :element-type '(unsigned-byte 8)"
        "                         :initial-element 0)))"
        "  (prog1 (handler-case (progn (ffi:peek-bytes p out 2) :no-error)"
        "           (error () :rejected))"
        "    (ffi:free-foreign p)))"),
        ":REJECTED");
}

/* A rank > 1 array is rejected rather than silently misread via
 * CL_VECTOR_ACTIVE_LENGTH/CL_VECTOR_DATA. */
TEST(lisp_ffi_poke_bytes_rank_rejected)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 16))"
        "      (v (make-array '(2 2) :element-type '(unsigned-byte 8)"
        "                       :initial-element 1)))"
        "  (prog1 (handler-case (progn (ffi:poke-bytes p v) :no-error)"
        "           (error () :rejected))"
        "    (ffi:free-foreign p)))"),
        ":REJECTED");
}

TEST(lisp_ffi_peek_bytes_rank_rejected)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 16))"
        "      (out (make-array '(2 2) :element-type '(unsigned-byte 8)"
        "                         :initial-element 0)))"
        "  (prog1 (handler-case (progn (ffi:peek-bytes p out) :no-error)"
        "           (error () :rejected))"
        "    (ffi:free-foreign p)))"),
        ":REJECTED");
}

TEST(lisp_ffi_poke_bytes_element_range_checked)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 16)))"
        "  (prog1 (list (handler-case (progn (ffi:poke-bytes p (vector 1 300)) :no-error)"
        "                 (error () :rejected))"
        "               (handler-case (progn (ffi:poke-bytes p (vector 1 -1)) :no-error)"
        "                 (error () :rejected))"
        "               (handler-case (progn (ffi:poke-bytes p (vector 1 'x)) :no-error)"
        "                 (error () :rejected)))"
        "    (ffi:free-foreign p)))"),
        "(:REJECTED :REJECTED :REJECTED)");
}

TEST(lisp_ffi_poke_bytes_bad_span_and_source)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 16))"
        "      (v (make-array 4 :element-type '(unsigned-byte 8)"
        "                       :initial-element 1)))"
        "  (prog1 (list (handler-case (progn (ffi:poke-bytes p v 0 0 99) :no-error)"
        "                 (error () :rejected))"
        "               (handler-case (progn (ffi:poke-bytes p v 0 3 1) :no-error)"
        "                 (error () :rejected))"
        "               (handler-case (progn (ffi:poke-bytes p (list 1 2)) :no-error)"
        "                 (error () :rejected))"
        "               (handler-case (progn (ffi:poke-bytes v v) :no-error)"
        "                 (error () :rejected)))"
        "    (ffi:free-foreign p)))"),
        "(:REJECTED :REJECTED :REJECTED :REJECTED)");
}

/* A fill pointer bounds the source: only the active elements move. */
TEST(lisp_ffi_poke_bytes_respects_fill_pointer)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 16))"
        "      (v (make-array 4 :element-type '(unsigned-byte 8)"
        "                       :initial-contents '(1 2 3 4)"
        "                       :fill-pointer 2)))"
        "  (prog1 (list (ffi:poke-bytes p v)"
        "               (handler-case (progn (ffi:poke-bytes p v 0 0 4) :no-error)"
        "                 (error () :rejected)))"
        "    (ffi:free-foreign p)))"),
        "(2 :REJECTED)");
}

TEST(lisp_ffi_foreign_string)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:foreign-string \"hello\")))"
        "  (prog1 (ffi:foreign-to-string p)"
        "    (ffi:free-foreign p)))"),
        "\"hello\"");
}

TEST(lisp_ffi_foreign_string_empty)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:foreign-string \"\")))"
        "  (prog1 (ffi:foreign-to-string p)"
        "    (ffi:free-foreign p)))"),
        "\"\"");
}

TEST(lisp_ffi_pointer_plus)
{
    /* pointer+ should create a new pointer with offset address */
    ASSERT_STR_EQ(eval_print(
        "(ffi:foreign-pointer-address (ffi:pointer+ (ffi:make-foreign-pointer 100) 20))"),
        "120");
}

TEST(lisp_ffi_printer)
{
    /* Foreign pointers should print as #<FOREIGN-POINTER ...> */
    const char *result = eval_print("(ffi:make-foreign-pointer 42)");
    ASSERT(strstr(result, "#<FOREIGN-POINTER") != NULL);
}

/* ================================================================
 * Typed peek/poke: signed, 64-bit, float/double, pointer
 * (platform-independent — exercised on host and Amiga)
 * ================================================================ */

TEST(lisp_ffi_peek_poke_signed)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 8))) (ffi:poke-i8 p -5) (prog1 (ffi:peek-i8 p) (ffi:free-foreign p)))"),
        "-5");
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 8))) (ffi:poke-i16 p -1000) (prog1 (ffi:peek-i16 p) (ffi:free-foreign p)))"),
        "-1000");
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 8))) (ffi:poke-i32 p -123456) (prog1 (ffi:peek-i32 p) (ffi:free-foreign p)))"),
        "-123456");
}

TEST(lisp_ffi_peek_poke_64)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 8))) (ffi:poke-u64 p 4294967300) (prog1 (ffi:peek-u64 p) (ffi:free-foreign p)))"),
        "4294967300");
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 8))) (ffi:poke-i64 p -5000000000) (prog1 (ffi:peek-i64 p) (ffi:free-foreign p)))"),
        "-5000000000");
}

TEST(lisp_ffi_peek_poke_float)
{
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 8))) (ffi:poke-single p 2.5) (prog1 (ffi:peek-single p) (ffi:free-foreign p)))"),
        "2.5");
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 8))) (ffi:poke-double p 6.25d0) (prog1 (ffi:peek-double p) (ffi:free-foreign p)))"),
        "6.25d0");
}

TEST(lisp_ffi_peek_poke_pointer)
{
    /* Store a pointer in memory and read it back; the read pointer must be
     * pointer-eq to the original. */
    ASSERT_STR_EQ(eval_print(
        "(let ((p (ffi:alloc-foreign 8))) (ffi:poke-pointer p p) "
        "(prog1 (ffi:pointer-eq p (ffi:peek-pointer p)) (ffi:free-foreign p)))"),
        "T");
}

TEST(lisp_ffi_pointer_eq)
{
    /* Two pointers to the same real address compare equal even though they
     * are distinct foreign-pointer objects (distinct side-table handles). */
    ASSERT_STR_EQ(eval_print(
        "(let* ((p (ffi:alloc-foreign 8)) (a (ffi:foreign-pointer-address p))) "
        "(prog1 (ffi:pointer-eq p (ffi:make-foreign-pointer a)) (ffi:free-foreign p)))"),
        "T");
    ASSERT_STR_EQ(eval_print(
        "(ffi:pointer-eq (ffi:make-foreign-pointer 16) (ffi:make-foreign-pointer 32))"),
        "NIL");
}

#ifndef PLATFORM_AMIGA
/* ================================================================
 * Host-only: dlopen / dlsym / libffi foreign calls + callbacks
 * ================================================================ */

TEST(lisp_ffi_symbol_pointer_found)
{
    /* abs is in the default (libc/libSystem) namespace. */
    ASSERT_STR_EQ(eval_print("(and (ffi:symbol-pointer \"abs\") t)"), "T");
}

TEST(lisp_ffi_symbol_pointer_missing)
{
    ASSERT_STR_EQ(eval_print("(ffi:symbol-pointer \"no_such_symbol_zzz\")"), "NIL");
}

TEST(lisp_ffi_call_int)
{
    ASSERT_STR_EQ(eval_print(
        "(ffi:call-foreign (ffi:symbol-pointer \"abs\") :int32 '(:int32) '(-42))"),
        "42");
}

TEST(lisp_ffi_call_pointer_arg)
{
    /* strlen of a foreign string. */
    ASSERT_STR_EQ(eval_print(
        "(let ((s (ffi:foreign-string \"hello\"))) "
        "(prog1 (ffi:call-foreign (ffi:symbol-pointer \"strlen\") :uint64 '(:pointer) (list s)) "
        "(ffi:free-foreign s)))"),
        "5");
}

TEST(lisp_ffi_call_double)
{
    ASSERT_STR_EQ(eval_print(
        "(ffi:call-foreign (ffi:symbol-pointer \"pow\") :double '(:double :double) '(2.0d0 10.0d0))"),
        "1024.0d0");
}

TEST(lisp_ffi_callback_qsort)
{
    /* qsort an int array through a Lisp comparator callback. */
    ASSERT_STR_EQ(eval_print(
        "(let* ((arr (ffi:alloc-foreign 16)) "
        "       (cmp (ffi:make-callback :int32 '(:pointer :pointer) "
        "              (lambda (a b) (- (ffi:peek-i32 a) (ffi:peek-i32 b)))))) "
        "  (ffi:poke-i32 arr 9 0) (ffi:poke-i32 arr 2 4) "
        "  (ffi:poke-i32 arr 7 8) (ffi:poke-i32 arr 1 12) "
        "  (ffi:call-foreign (ffi:symbol-pointer \"qsort\") :void "
        "    '(:pointer :uint64 :uint64 :pointer) (list arr 4 4 cmp)) "
        "  (prog1 (list (ffi:peek-i32 arr 0) (ffi:peek-i32 arr 4) "
        "               (ffi:peek-i32 arr 8) (ffi:peek-i32 arr 12)) "
        "    (ffi:free-foreign arr)))"),
        "(1 2 7 9)");
}

TEST(lisp_ffi_free_callback)
{
    /* free-callback should reclaim the slot so it can be reused. */
    ASSERT_STR_EQ(eval_print(
        /* Create and immediately free CL_FFI_MAX_CALLBACKS+1 callbacks —
         * without free-callback this would exhaust the 64-slot table. */
        "(let ((n 0)) "
        "  (dotimes (i 65) "
        "    (let ((cb (ffi:make-callback :int32 '() (lambda () 42)))) "
        "      (ffi:free-callback cb) "
        "      (incf n))) "
        "  n)"),
        "65");
}
#endif

/* ================================================================
 * AMIGA package tests (POSIX: package should not exist)
 * ================================================================ */

#ifndef PLATFORM_AMIGA
TEST(lisp_amiga_package_not_on_posix)
{
    ASSERT_STR_EQ(eval_print("(find-package \"AMIGA\")"), "NIL");
}
#endif

int main(void)
{
    setup();

    printf("--- test_ffi ---\n");

    /* C-level tests */
    RUN(c_foreign_pointer_create);
    RUN(c_foreign_pointer_type_name);
    RUN(c_foreign_pointer_not_fixnum);
    RUN(c_foreign_pointer_zero_addr);
    RUN(c_foreign_pointer_gc_survives);

    /* Lisp-level FFI tests */
    RUN(lisp_ffi_package_exists);
    RUN(lisp_ffi_make_foreign_pointer);
    RUN(lisp_ffi_foreign_pointer_p);
    RUN(lisp_ffi_null_pointer_p);
    RUN(lisp_ffi_foreign_pointer_address);
    RUN(lisp_ffi_alloc_free);
    RUN(lisp_ffi_peek_poke_u32);
    RUN(lisp_ffi_peek_poke_u32_offset);
    RUN(lisp_ffi_peek_poke_u16);
    RUN(lisp_ffi_peek_poke_u8);
    RUN(lisp_ffi_poke_bytes_vector);
    RUN(lisp_ffi_poke_bytes_string);
    RUN(lisp_ffi_poke_bytes_offset_start_end);
    RUN(lisp_ffi_poke_bytes_empty);
    RUN(lisp_ffi_peek_bytes_roundtrip);
    RUN(lisp_ffi_poke_bytes_overrun_rejected);
    RUN(lisp_ffi_peek_bytes_overrun_rejected);
    RUN(lisp_ffi_poke_bytes_rank_rejected);
    RUN(lisp_ffi_peek_bytes_rank_rejected);
    RUN(lisp_ffi_poke_bytes_element_range_checked);
    RUN(lisp_ffi_poke_bytes_bad_span_and_source);
    RUN(lisp_ffi_poke_bytes_respects_fill_pointer);
    RUN(lisp_ffi_foreign_string);
    RUN(lisp_ffi_foreign_string_empty);
    RUN(lisp_ffi_pointer_plus);
    RUN(lisp_ffi_printer);

    /* Typed peek/poke (signed, 64-bit, float/double, pointer) */
    RUN(lisp_ffi_peek_poke_signed);
    RUN(lisp_ffi_peek_poke_64);
    RUN(lisp_ffi_peek_poke_float);
    RUN(lisp_ffi_peek_poke_pointer);
    RUN(lisp_ffi_pointer_eq);

    /* AMIGA package tests */
#ifndef PLATFORM_AMIGA
    RUN(lisp_amiga_package_not_on_posix);

    /* Host-only: dlopen / libffi calls + callbacks */
    RUN(lisp_ffi_symbol_pointer_found);
    RUN(lisp_ffi_symbol_pointer_missing);
    RUN(lisp_ffi_call_int);
    RUN(lisp_ffi_call_pointer_arg);
    RUN(lisp_ffi_call_double);
    RUN(lisp_ffi_callback_qsort);
    RUN(lisp_ffi_free_callback);
#endif

    REPORT();
    teardown();
    return test_fail > 0 ? 1 : 0;
}
