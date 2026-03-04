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
    static char buf[512];
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
        cl_vm.sp = 0;
        cl_vm.fp = 0;
        return buf;
    }
}

/* --- Basic #P reader --- */

TEST(pathname_read_simple)
{
    ASSERT_STR_EQ(eval_print("#P\"foo.lisp\""), "#P\"foo.lisp\"");
}

TEST(pathname_read_with_dir)
{
    ASSERT_STR_EQ(eval_print("#P\"/foo/bar.lisp\""), "#P\"/foo/bar.lisp\"");
}

TEST(pathname_read_relative)
{
    ASSERT_STR_EQ(eval_print("#P\"dir/file.txt\""), "#P\"dir/file.txt\"");
}

TEST(pathname_read_empty)
{
    ASSERT_STR_EQ(eval_print("#P\"\""), "#P\"\"");
}

TEST(pathname_read_case_insensitive)
{
    ASSERT_STR_EQ(eval_print("#p\"test.lisp\""), "#P\"test.lisp\"");
}

/* --- pathnamep predicate --- */

TEST(pathnamep_true)
{
    ASSERT_STR_EQ(eval_print("(pathnamep #P\"test\")"), "T");
}

TEST(pathnamep_false_string)
{
    ASSERT_STR_EQ(eval_print("(pathnamep \"test\")"), "NIL");
}

TEST(pathnamep_false_nil)
{
    ASSERT_STR_EQ(eval_print("(pathnamep nil)"), "NIL");
}

TEST(pathnamep_false_number)
{
    ASSERT_STR_EQ(eval_print("(pathnamep 42)"), "NIL");
}

/* --- pathname coercion --- */

TEST(pathname_from_string)
{
    ASSERT_STR_EQ(eval_print("(pathnamep (pathname \"foo.lisp\"))"), "T");
}

TEST(pathname_from_pathname)
{
    ASSERT_STR_EQ(eval_print("(pathnamep (pathname #P\"foo.lisp\"))"), "T");
}

/* --- pathname-name --- */

TEST(pathname_name_simple)
{
    ASSERT_STR_EQ(eval_print("(pathname-name #P\"foo.lisp\")"), "\"foo\"");
}

TEST(pathname_name_with_dir)
{
    ASSERT_STR_EQ(eval_print("(pathname-name #P\"/foo/bar.lisp\")"), "\"bar\"");
}

TEST(pathname_name_no_ext)
{
    ASSERT_STR_EQ(eval_print("(pathname-name #P\"foo\")"), "\"foo\"");
}

TEST(pathname_name_from_string)
{
    ASSERT_STR_EQ(eval_print("(pathname-name \"bar.txt\")"), "\"bar\"");
}

/* --- pathname-type --- */

TEST(pathname_type_lisp)
{
    ASSERT_STR_EQ(eval_print("(pathname-type #P\"foo.lisp\")"), "\"lisp\"");
}

TEST(pathname_type_nil)
{
    ASSERT_STR_EQ(eval_print("(pathname-type #P\"foo\")"), "NIL");
}

TEST(pathname_type_txt)
{
    ASSERT_STR_EQ(eval_print("(pathname-type #P\"bar.txt\")"), "\"txt\"");
}

/* --- pathname-directory --- */

TEST(pathname_directory_absolute)
{
    ASSERT_STR_EQ(eval_print("(pathname-directory #P\"/foo/bar.lisp\")"), "(:ABSOLUTE \"foo\")");
}

TEST(pathname_directory_relative)
{
    ASSERT_STR_EQ(eval_print("(pathname-directory #P\"dir/file.txt\")"), "(:RELATIVE \"dir\")");
}

TEST(pathname_directory_nil)
{
    ASSERT_STR_EQ(eval_print("(pathname-directory #P\"file.txt\")"), "NIL");
}

TEST(pathname_directory_multi)
{
    ASSERT_STR_EQ(eval_print("(pathname-directory #P\"/a/b/c/d.txt\")"), "(:ABSOLUTE \"a\" \"b\" \"c\")");
}

/* --- pathname-device (Amiga) --- */

TEST(pathname_device_amiga)
{
    ASSERT_STR_EQ(eval_print("(pathname-device #P\"DH0:Work/test.lisp\")"), "\"DH0\"");
}

TEST(pathname_device_nil)
{
    ASSERT_STR_EQ(eval_print("(pathname-device #P\"/foo/bar.lisp\")"), "NIL");
}

/* --- namestring --- */

TEST(namestring_simple)
{
    ASSERT_STR_EQ(eval_print("(namestring #P\"foo.lisp\")"), "\"foo.lisp\"");
}

TEST(namestring_with_dir)
{
    ASSERT_STR_EQ(eval_print("(namestring #P\"/foo/bar.lisp\")"), "\"/foo/bar.lisp\"");
}

TEST(namestring_from_string)
{
    ASSERT_STR_EQ(eval_print("(namestring \"foo.lisp\")"), "\"foo.lisp\"");
}

/* --- type system --- */

TEST(pathname_typep)
{
    ASSERT_STR_EQ(eval_print("(typep #P\"test\" 'pathname)"), "T");
}

TEST(pathname_typep_neg)
{
    ASSERT_STR_EQ(eval_print("(typep \"test\" 'pathname)"), "NIL");
}

TEST(pathname_type_of)
{
    ASSERT_STR_EQ(eval_print("(type-of #P\"test\")"), "PATHNAME");
}

/* --- equal --- */

TEST(pathname_equal_same)
{
    ASSERT_STR_EQ(eval_print("(equal #P\"/foo/bar.lisp\" #P\"/foo/bar.lisp\")"), "T");
}

TEST(pathname_equal_diff)
{
    ASSERT_STR_EQ(eval_print("(equal #P\"/foo/bar.lisp\" #P\"/baz/bar.lisp\")"), "NIL");
}

/* --- make-pathname --- */

TEST(make_pathname_basic)
{
    ASSERT_STR_EQ(eval_print("(namestring (make-pathname :name \"foo\" :type \"lisp\"))"), "\"foo.lisp\"");
}

TEST(make_pathname_dir)
{
    ASSERT_STR_EQ(eval_print("(namestring (make-pathname :directory '(:absolute \"usr\" \"lib\") :name \"foo\" :type \"lisp\"))"),
                  "\"/usr/lib/foo.lisp\"");
}

/* --- merge-pathnames --- */

TEST(merge_pathnames_basic)
{
    ASSERT_STR_EQ(eval_print("(namestring (merge-pathnames #P\"bar.lisp\" #P\"/foo/\"))"),
                  "\"/foo/bar.lisp\"");
}

TEST(merge_pathnames_no_merge_needed)
{
    ASSERT_STR_EQ(eval_print("(namestring (merge-pathnames #P\"/full/path.lisp\" #P\"/other/\"))"),
                  "\"/full/path.lisp\"");
}

/* --- file-namestring --- */

TEST(file_namestring_basic)
{
    ASSERT_STR_EQ(eval_print("(file-namestring #P\"/foo/bar.lisp\")"), "\"bar.lisp\"");
}

/* --- directory-namestring --- */

TEST(directory_namestring_basic)
{
    ASSERT_STR_EQ(eval_print("(directory-namestring #P\"/foo/bar.lisp\")"), "\"/foo/\"");
}

/* --- Amiga-specific parsing --- */

TEST(pathname_amiga_device_dir)
{
    ASSERT_STR_EQ(eval_print("(pathname-device #P\"DH0:Work/test.lisp\")"), "\"DH0\"");
}

TEST(pathname_amiga_dir)
{
    ASSERT_STR_EQ(eval_print("(pathname-directory #P\"DH0:Work/test.lisp\")"), "(:ABSOLUTE \"Work\")");
}

TEST(pathname_amiga_name)
{
    ASSERT_STR_EQ(eval_print("(pathname-name #P\"DH0:Work/test.lisp\")"), "\"test\"");
}

TEST(pathname_amiga_type)
{
    ASSERT_STR_EQ(eval_print("(pathname-type #P\"DH0:Work/test.lisp\")"), "\"lisp\"");
}

TEST(pathname_amiga_namestring)
{
    ASSERT_STR_EQ(eval_print("(namestring #P\"DH0:Work/test.lisp\")"), "\"DH0:Work/test.lisp\"");
}

/* --- coerce --- */

TEST(coerce_string_to_pathname)
{
    ASSERT_STR_EQ(eval_print("(pathnamep (coerce \"foo.lisp\" 'pathname))"), "T");
}

/* --- *default-pathname-defaults* --- */

TEST(default_pathname_defaults)
{
    ASSERT_STR_EQ(eval_print("(pathnamep *default-pathname-defaults*)"), "T");
}

/* --- pathname-host / pathname-version --- */

TEST(pathname_host_nil)
{
    ASSERT_STR_EQ(eval_print("(pathname-host #P\"foo.lisp\")"), "NIL");
}

TEST(pathname_version_nil)
{
    ASSERT_STR_EQ(eval_print("(pathname-version #P\"foo.lisp\")"), "NIL");
}

/* --- directory only (trailing slash) --- */

TEST(pathname_dir_only)
{
    ASSERT_STR_EQ(eval_print("(pathname-directory #P\"/foo/bar/\")"), "(:ABSOLUTE \"foo\" \"bar\")");
}

TEST(pathname_dir_only_name_nil)
{
    ASSERT_STR_EQ(eval_print("(pathname-name #P\"/foo/bar/\")"), "NIL");
}

/* --- Run all tests --- */

int main(void)
{
    setup();

    RUN(pathname_read_simple);
    RUN(pathname_read_with_dir);
    RUN(pathname_read_relative);
    RUN(pathname_read_empty);
    RUN(pathname_read_case_insensitive);
    RUN(pathnamep_true);
    RUN(pathnamep_false_string);
    RUN(pathnamep_false_nil);
    RUN(pathnamep_false_number);
    RUN(pathname_from_string);
    RUN(pathname_from_pathname);
    RUN(pathname_name_simple);
    RUN(pathname_name_with_dir);
    RUN(pathname_name_no_ext);
    RUN(pathname_name_from_string);
    RUN(pathname_type_lisp);
    RUN(pathname_type_nil);
    RUN(pathname_type_txt);
    RUN(pathname_directory_absolute);
    RUN(pathname_directory_relative);
    RUN(pathname_directory_nil);
    RUN(pathname_directory_multi);
    RUN(pathname_device_amiga);
    RUN(pathname_device_nil);
    RUN(namestring_simple);
    RUN(namestring_with_dir);
    RUN(namestring_from_string);
    RUN(pathname_typep);
    RUN(pathname_typep_neg);
    RUN(pathname_type_of);
    RUN(pathname_equal_same);
    RUN(pathname_equal_diff);
    RUN(make_pathname_basic);
    RUN(make_pathname_dir);
    RUN(merge_pathnames_basic);
    RUN(merge_pathnames_no_merge_needed);
    RUN(file_namestring_basic);
    RUN(directory_namestring_basic);
    RUN(pathname_amiga_device_dir);
    RUN(pathname_amiga_dir);
    RUN(pathname_amiga_name);
    RUN(pathname_amiga_type);
    RUN(pathname_amiga_namestring);
    RUN(coerce_string_to_pathname);
    RUN(default_pathname_defaults);
    RUN(pathname_host_nil);
    RUN(pathname_version_nil);
    RUN(pathname_dir_only);
    RUN(pathname_dir_only_name_nil);

    teardown();
    REPORT();
}
