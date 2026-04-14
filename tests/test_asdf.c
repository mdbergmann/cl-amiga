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
#include "core/stream.h"
#include "platform/platform.h"

/* 12MB heap — boot.lisp + CLOS + ASDF live data is ~11.1MB */
#define ASDF_HEAP_SIZE (12 * 1024 * 1024)

static void setup(void)
{
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(ASDF_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
    cl_reader_init();
    cl_printer_init();
    cl_compiler_init();
    cl_vm_init(0, 0);
    cl_stream_init();
    cl_builtins_init();
    cl_repl_init();  /* Loads boot.lisp + CLOS via (require "clos") */
}

static void teardown(void)
{
    cl_mem_shutdown();
    platform_shutdown();
}

/* Helper: eval a string, return printed result */
static const char *eval_print(const char *str)
{
    static char buf[512];
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
        cl_vm.sp = 0;
        cl_vm.fp = 0;
        return buf;
    }
}

/* Helper: load a file, return 0 on success, error code on failure */
static int load_file(const char *path)
{
    int err; CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        cl_load_file(path);
        CL_UNCATCH();
        return 0;
    } else {
        CL_UNCATCH();
        cl_vm.sp = 0;
        cl_vm.fp = 0;
        return err;
    }
}

TEST(clos_loaded)
{
    /* CLOS must be available before ASDF can load.
     * defclass/defgeneric/defmethod are compiler special forms (not fboundp).
     * Check for CLOS runtime primitives instead. */
    ASSERT_STR_EQ(eval_print("(fboundp 'make-instance)"), "T");
    ASSERT_STR_EQ(eval_print("(fboundp 'find-class)"), "T");
    ASSERT_STR_EQ(eval_print("(not (null (find-class 'standard-class)))"), "T");
}

TEST(load_asdf)
{
    int err = load_file("lib/asdf.lisp");
    ASSERT_EQ_INT(err, 0);
}

TEST(asdf_package_exists)
{
    ASSERT_STR_EQ(eval_print("(package-name (find-package \"ASDF\"))"),
                  "\"ASDF/INTERFACE\"");
}

TEST(asdf_version)
{
    ASSERT_STR_EQ(eval_print("(symbol-value (find-symbol \"*ASDF-VERSION*\" \"ASDF\"))"),
                  "\"3.3.7\"");
}

int main(void)
{
    test_init();
    setup();

    RUN(clos_loaded);
    RUN(load_asdf);
    RUN(asdf_package_exists);
    RUN(asdf_version);

    teardown();
    REPORT();
}
