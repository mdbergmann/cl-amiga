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
    cl_vm_init();
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
        return buf;
    }
}

/* ---- C-level tests ---- */

TEST(c_find_package_by_name)
{
    CL_Obj pkg = cl_find_package("COMMON-LISP", 11);
    ASSERT(!CL_NULL_P(pkg));
    ASSERT(CL_PACKAGE_P(pkg));
    ASSERT(pkg == cl_package_cl);
}

TEST(c_find_package_by_nickname)
{
    CL_Obj pkg = cl_find_package("CL", 2);
    ASSERT(!CL_NULL_P(pkg));
    ASSERT(pkg == cl_package_cl);
}

TEST(c_find_package_cl_user)
{
    CL_Obj pkg1 = cl_find_package("COMMON-LISP-USER", 16);
    CL_Obj pkg2 = cl_find_package("CL-USER", 7);
    ASSERT(!CL_NULL_P(pkg1));
    ASSERT(pkg1 == cl_package_cl_user);
    ASSERT(pkg2 == cl_package_cl_user);
}

TEST(c_find_package_keyword)
{
    CL_Obj pkg = cl_find_package("KEYWORD", 7);
    ASSERT(!CL_NULL_P(pkg));
    ASSERT(pkg == cl_package_keyword);
}

TEST(c_find_package_not_found)
{
    CL_Obj pkg = cl_find_package("NONEXISTENT", 11);
    ASSERT(CL_NULL_P(pkg));
}

TEST(c_exported_flag)
{
    /* CAR should be exported from CL */
    CL_Obj car_sym = cl_intern_in("CAR", 3, cl_package_cl);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(car_sym);
    ASSERT(s->flags & CL_SYM_EXPORTED);
}

TEST(c_find_external)
{
    /* CAR is exported from CL */
    CL_Obj found = cl_package_find_external("CAR", 3, cl_package_cl);
    ASSERT(!CL_NULL_P(found));

    /* Non-exported symbol shouldn't be found externally */
    {
        CL_Obj my_sym = cl_intern_in("MY-INTERNAL-SYM", 15, cl_package_cl_user);
        CL_Obj found2;
        (void)my_sym;
        found2 = cl_package_find_external("MY-INTERNAL-SYM", 15, cl_package_cl_user);
        ASSERT(CL_NULL_P(found2));
    }
}

TEST(c_find_symbol_with_status)
{
    int status;
    CL_Obj sym;

    /* CAR is external in CL */
    sym = cl_find_symbol_with_status("CAR", 3, cl_package_cl, &status);
    ASSERT(!CL_NULL_P(sym));
    ASSERT_EQ_INT(status, 2); /* :EXTERNAL */

    /* CAR is inherited in CL-USER */
    sym = cl_find_symbol_with_status("CAR", 3, cl_package_cl_user, &status);
    ASSERT(!CL_NULL_P(sym));
    ASSERT_EQ_INT(status, 3); /* :INHERITED */

    /* Nonexistent symbol */
    sym = cl_find_symbol_with_status("ZZZZZ", 5, cl_package_cl, &status);
    ASSERT(CL_NULL_P(sym));
    ASSERT_EQ_INT(status, 0); /* not found */
}

TEST(c_cl_user_can_access_cl_symbols)
{
    /* Regression: CL-USER should still see all CL symbols */
    CL_Obj car_sym = cl_package_find_symbol("CAR", 3, cl_package_cl_user);
    CL_Obj cons_sym = cl_package_find_symbol("CONS", 4, cl_package_cl_user);
    CL_Obj t_sym = cl_package_find_symbol("T", 1, cl_package_cl_user);
    ASSERT(!CL_NULL_P(car_sym));
    ASSERT(!CL_NULL_P(cons_sym));
    ASSERT(!CL_NULL_P(t_sym));
    ASSERT(t_sym == SYM_T);
}

TEST(c_register_package)
{
    CL_Obj pkg = cl_make_package("TEST-PKG-C");
    CL_Obj found;
    cl_register_package(pkg);
    found = cl_find_package("TEST-PKG-C", 10);
    ASSERT(!CL_NULL_P(found));
    ASSERT(found == pkg);
}

TEST(c_export_symbol)
{
    CL_Obj pkg = cl_make_package("EXPORT-TEST");
    CL_Obj sym = cl_intern_in("MYSYM", 5, pkg);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    CL_Obj found;

    /* Not exported yet */
    ASSERT(!(s->flags & CL_SYM_EXPORTED));
    found = cl_package_find_external("MYSYM", 5, pkg);
    ASSERT(CL_NULL_P(found));

    /* Export it */
    cl_export_symbol(sym, pkg);
    ASSERT(s->flags & CL_SYM_EXPORTED);
    found = cl_package_find_external("MYSYM", 5, pkg);
    ASSERT(!CL_NULL_P(found));
    ASSERT(found == sym);
}

TEST(c_use_package)
{
    CL_Obj pkg_a = cl_make_package("USE-A");
    CL_Obj pkg_b = cl_make_package("USE-B");
    CL_Obj sym, found;

    sym = cl_intern_in("SHARED", 6, pkg_a);
    cl_export_symbol(sym, pkg_a);
    cl_use_package(pkg_a, pkg_b);

    /* pkg_b should now inherit SHARED from pkg_a */
    found = cl_package_find_symbol("SHARED", 6, pkg_b);
    ASSERT(!CL_NULL_P(found));
    ASSERT(found == sym);
}

TEST(c_use_package_unexported_hidden)
{
    CL_Obj pkg_a = cl_make_package("HIDE-A");
    CL_Obj pkg_b = cl_make_package("HIDE-B");
    CL_Obj sym, found;

    sym = cl_intern_in("HIDDEN", 6, pkg_a);
    (void)sym;
    cl_use_package(pkg_a, pkg_b);

    /* HIDDEN is not exported, should not be found in pkg_b */
    found = cl_package_find_symbol("HIDDEN", 6, pkg_b);
    ASSERT(CL_NULL_P(found));
}

TEST(c_import_symbol)
{
    CL_Obj pkg_a = cl_make_package("IMP-A");
    CL_Obj pkg_b = cl_make_package("IMP-B");
    CL_Obj sym, found;
    int status;

    sym = cl_intern_in("IMPORTED", 8, pkg_a);
    cl_import_symbol(sym, pkg_b);

    found = cl_find_symbol_with_status("IMPORTED", 8, pkg_b, &status);
    ASSERT(!CL_NULL_P(found));
    ASSERT(found == sym);
    ASSERT_EQ_INT(status, 1); /* :INTERNAL — imported but not exported */
}

TEST(c_shadow_symbol)
{
    CL_Obj pkg = cl_make_package("SHADOW-TEST");
    CL_Obj cl_car;
    CL_Obj shadow_car;

    cl_use_package(cl_package_cl, pkg);

    /* CAR is inherited from CL */
    cl_car = cl_package_find_symbol("CAR", 3, pkg);
    ASSERT(!CL_NULL_P(cl_car));

    /* Shadow it */
    cl_shadow_symbol("CAR", 3, pkg);
    shadow_car = cl_package_find_symbol("CAR", 3, pkg);
    ASSERT(!CL_NULL_P(shadow_car));
    ASSERT(shadow_car != cl_car); /* Different symbol! */
}

/* ---- Eval-level tests ---- */

TEST(eval_find_package)
{
    ASSERT_STR_EQ(eval_print("(find-package \"COMMON-LISP\")"), "#<PACKAGE COMMON-LISP>");
    ASSERT_STR_EQ(eval_print("(find-package \"CL\")"), "#<PACKAGE COMMON-LISP>");
    ASSERT_STR_EQ(eval_print("(find-package \"CL-USER\")"), "#<PACKAGE COMMON-LISP-USER>");
    ASSERT_STR_EQ(eval_print("(find-package \"KEYWORD\")"), "#<PACKAGE KEYWORD>");
}

TEST(eval_find_package_nil)
{
    ASSERT_STR_EQ(eval_print("(find-package \"NONEXISTENT\")"), "NIL");
}

TEST(eval_make_package_basic)
{
    ASSERT_STR_EQ(eval_print("(package-name (make-package \"EVAL-PKG1\"))"), "\"EVAL-PKG1\"");
    ASSERT_STR_EQ(eval_print("(find-package \"EVAL-PKG1\")"), "#<PACKAGE EVAL-PKG1>");
}

TEST(eval_package_name)
{
    ASSERT_STR_EQ(eval_print("(package-name (find-package \"CL\"))"), "\"COMMON-LISP\"");
}

TEST(eval_list_all_packages)
{
    /* At least the 3 standard packages exist */
    ASSERT_STR_EQ(eval_print("(>= (length (list-all-packages)) 3)"), "T");
}

TEST(eval_package_nicknames)
{
    ASSERT_STR_EQ(eval_print("(car (package-nicknames (find-package \"CL\")))"), "\"CL\"");
}

TEST(eval_package_use_list)
{
    /* CL-USER uses CL */
    ASSERT_STR_EQ(eval_print("(length (package-use-list (find-package \"CL-USER\")))"), "1");
}

TEST(eval_find_symbol_external)
{
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (sym status) (find-symbol \"CAR\" (find-package \"CL\")) status)"),
        ":EXTERNAL");
}

TEST(eval_find_symbol_inherited)
{
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (sym status) (find-symbol \"CAR\" (find-package \"CL-USER\")) status)"),
        ":INHERITED");
}

TEST(eval_find_symbol_not_found)
{
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (sym status) (find-symbol \"ZZZZZ\" (find-package \"CL\")) status)"),
        "NIL");
}

TEST(eval_intern_new)
{
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (sym status) (intern \"NEW-TEST-SYM\") status)"),
        "NIL");
    /* Now it should be found */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (sym status) (find-symbol \"NEW-TEST-SYM\") status)"),
        ":INTERNAL");
}

TEST(eval_intern_existing)
{
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (sym status) (intern \"CAR\") status)"),
        ":INHERITED");
}

TEST(eval_export_unexport)
{
    eval_print("(make-package \"EXP-TEST\")");
    eval_print("(intern \"MY-EXP\" (find-package \"EXP-TEST\"))");
    /* Not exported yet */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s st) (find-symbol \"MY-EXP\" (find-package \"EXP-TEST\")) st)"),
        ":INTERNAL");
    /* Export it */
    eval_print("(export (find-symbol \"MY-EXP\" (find-package \"EXP-TEST\")) (find-package \"EXP-TEST\"))");
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s st) (find-symbol \"MY-EXP\" (find-package \"EXP-TEST\")) st)"),
        ":EXTERNAL");
    /* Unexport it */
    eval_print("(unexport (find-symbol \"MY-EXP\" (find-package \"EXP-TEST\")) (find-package \"EXP-TEST\"))");
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s st) (find-symbol \"MY-EXP\" (find-package \"EXP-TEST\")) st)"),
        ":INTERNAL");
}

TEST(eval_use_package)
{
    eval_print("(make-package \"USE-SRC\")");
    eval_print("(make-package \"USE-DST\")");
    eval_print("(intern \"SRC-SYM\" (find-package \"USE-SRC\"))");
    eval_print("(export (find-symbol \"SRC-SYM\" (find-package \"USE-SRC\")) (find-package \"USE-SRC\"))");
    eval_print("(use-package \"USE-SRC\" \"USE-DST\")");
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s st) (find-symbol \"SRC-SYM\" (find-package \"USE-DST\")) st)"),
        ":INHERITED");
}

TEST(eval_delete_package)
{
    eval_print("(make-package \"DEL-PKG\")");
    ASSERT_STR_EQ(eval_print("(find-package \"DEL-PKG\")"), "#<PACKAGE DEL-PKG>");
    eval_print("(delete-package (find-package \"DEL-PKG\"))");
    ASSERT_STR_EQ(eval_print("(find-package \"DEL-PKG\")"), "NIL");
}

TEST(eval_rename_package)
{
    eval_print("(make-package \"OLD-NAME\")");
    eval_print("(rename-package (find-package \"OLD-NAME\") \"NEW-NAME\")");
    ASSERT_STR_EQ(eval_print("(package-name (find-package \"NEW-NAME\"))"), "\"NEW-NAME\"");
}

TEST(eval_shadow)
{
    eval_print("(make-package \"SHAD-PKG\")");
    eval_print("(use-package \"CL\" \"SHAD-PKG\")");
    eval_print("(shadow \"CAR\" (find-package \"SHAD-PKG\"))");
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s st) (find-symbol \"CAR\" (find-package \"SHAD-PKG\")) st)"),
        ":INTERNAL");
}

TEST(eval_unintern)
{
    eval_print("(make-package \"UNINT-PKG\")");
    eval_print("(intern \"REMOVE-ME\" (find-package \"UNINT-PKG\"))");
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s st) (find-symbol \"REMOVE-ME\" (find-package \"UNINT-PKG\")) st)"),
        ":INTERNAL");
    eval_print("(unintern (find-symbol \"REMOVE-ME\" (find-package \"UNINT-PKG\")) (find-package \"UNINT-PKG\"))");
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s st) (find-symbol \"REMOVE-ME\" (find-package \"UNINT-PKG\")) st)"),
        "NIL");
}

/* ---- Main ---- */

int main(void)
{
    setup();
    printf("\n");

    /* C-level tests */
    RUN(c_find_package_by_name);
    RUN(c_find_package_by_nickname);
    RUN(c_find_package_cl_user);
    RUN(c_find_package_keyword);
    RUN(c_find_package_not_found);
    RUN(c_exported_flag);
    RUN(c_find_external);
    RUN(c_find_symbol_with_status);
    RUN(c_cl_user_can_access_cl_symbols);
    RUN(c_register_package);
    RUN(c_export_symbol);
    RUN(c_use_package);
    RUN(c_use_package_unexported_hidden);
    RUN(c_import_symbol);
    RUN(c_shadow_symbol);

    /* Eval-level tests */
    RUN(eval_find_package);
    RUN(eval_find_package_nil);
    RUN(eval_make_package_basic);
    RUN(eval_package_name);
    RUN(eval_list_all_packages);
    RUN(eval_package_nicknames);
    RUN(eval_package_use_list);
    RUN(eval_find_symbol_external);
    RUN(eval_find_symbol_inherited);
    RUN(eval_find_symbol_not_found);
    RUN(eval_intern_new);
    RUN(eval_intern_existing);
    RUN(eval_export_unexport);
    RUN(eval_use_package);
    RUN(eval_delete_package);
    RUN(eval_rename_package);
    RUN(eval_shadow);
    RUN(eval_unintern);

    teardown();
    REPORT();
}
