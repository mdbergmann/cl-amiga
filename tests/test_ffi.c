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

    err = CL_CATCH();
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
    RUN(lisp_ffi_foreign_string);
    RUN(lisp_ffi_foreign_string_empty);
    RUN(lisp_ffi_pointer_plus);
    RUN(lisp_ffi_printer);

    /* AMIGA package tests */
#ifndef PLATFORM_AMIGA
    RUN(lisp_amiga_package_not_on_posix);
#endif

    REPORT();
    teardown();
    return test_fail > 0 ? 1 : 0;
}
