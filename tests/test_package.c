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
        /* Reset VM state after error (prevent stale frames) */
        cl_vm.sp = 0;
        cl_vm.fp = 0;
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
    /* CL-USER uses CL and EXT */
    ASSERT_STR_EQ(eval_print("(length (package-use-list (find-package \"CL-USER\")))"), "2");
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

/* ---- Reader qualified syntax tests ---- */

TEST(c_make_uninterned_symbol)
{
    CL_Obj name_str = cl_make_string("TEMP", 4);
    CL_Obj sym = cl_make_uninterned_symbol(name_str);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    ASSERT(CL_SYMBOL_P(sym));
    ASSERT(CL_NULL_P(s->package));
    ASSERT_STR_EQ(cl_symbol_name(sym), "TEMP");
}

TEST(c_uninterned_symbols_are_unique)
{
    CL_Obj name1 = cl_make_string("X", 1);
    CL_Obj name2 = cl_make_string("X", 1);
    CL_Obj sym1 = cl_make_uninterned_symbol(name1);
    CL_Obj sym2 = cl_make_uninterned_symbol(name2);
    ASSERT(sym1 != sym2);  /* Each uninterned symbol is unique */
}

TEST(eval_read_pkg_external)
{
    /* Create package FOO with exported symbol BAR */
    eval_print("(make-package \"QR-FOO\")");
    eval_print("(intern \"BAR\" (find-package \"QR-FOO\"))");
    eval_print("(export (find-symbol \"BAR\" (find-package \"QR-FOO\")) (find-package \"QR-FOO\"))");
    /* QR-FOO:BAR should resolve to the exported symbol */
    ASSERT_STR_EQ(eval_print("(eq (find-symbol \"BAR\" (find-package \"QR-FOO\")) 'QR-FOO:BAR)"), "T");
}

TEST(eval_read_pkg_external_error)
{
    /* Create package with non-exported symbol */
    eval_print("(make-package \"QR-FOO2\")");
    eval_print("(intern \"SECRET\" (find-package \"QR-FOO2\"))");
    /* QR-FOO2:SECRET should error (not exported) */
    {
        const char *result = eval_print("QR-FOO2:SECRET");
        /* Should produce an error */
        ASSERT(strncmp(result, "ERROR:", 6) == 0);
    }
}

TEST(eval_read_pkg_internal)
{
    /* Create package with non-exported symbol */
    eval_print("(make-package \"QR-FOO3\")");
    eval_print("(intern \"HIDDEN\" (find-package \"QR-FOO3\"))");
    /* QR-FOO3::HIDDEN should work (internal access) */
    ASSERT_STR_EQ(eval_print("(eq (find-symbol \"HIDDEN\" (find-package \"QR-FOO3\")) 'QR-FOO3::HIDDEN)"), "T");
}

TEST(eval_read_pkg_internal_creates)
{
    /* pkg::sym should create symbol if it doesn't exist */
    eval_print("(make-package \"QR-FOO4\")");
    ASSERT_STR_EQ(eval_print("(symbolp 'QR-FOO4::NEWSYM)"), "T");
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s st) (find-symbol \"NEWSYM\" (find-package \"QR-FOO4\")) st)"),
        ":INTERNAL");
}

TEST(eval_read_pkg_not_found)
{
    /* Non-existent package should error */
    {
        const char *result = eval_print("NONEXISTENT-PKG:FOO");
        ASSERT(strncmp(result, "ERROR:", 6) == 0);
    }
}

TEST(eval_read_cl_qualified)
{
    /* CL:CAR should resolve to the standard CAR */
    ASSERT_STR_EQ(eval_print("(eq 'CL:CAR 'CAR)"), "T");
}

TEST(eval_read_keyword_qualified)
{
    /* KEYWORD:TEST should be same as :TEST */
    ASSERT_STR_EQ(eval_print("(eq 'KEYWORD:TEST :TEST)"), "T");
}

TEST(eval_read_uninterned)
{
    /* #:SYM creates uninterned symbol */
    ASSERT_STR_EQ(eval_print("(symbolp '#:TEMP)"), "T");
    ASSERT_STR_EQ(eval_print("(null (symbol-package '#:TEMP))"), "T");
}

TEST(eval_read_uninterned_unique)
{
    /* Two #:SYM are not eq even with same name */
    ASSERT_STR_EQ(eval_print("(eq '#:X '#:X)"), "NIL");
}

TEST(eval_print_uninterned)
{
    /* Uninterned symbols print as #:NAME */
    ASSERT_STR_EQ(eval_print("(let ((s '#:HELLO)) (prin1-to-string s))"), "\"#:HELLO\"");
}

TEST(eval_print_keyword_unchanged)
{
    /* Keywords still print with : prefix */
    ASSERT_STR_EQ(eval_print("(prin1-to-string :foo)"), "\":FOO\"");
}

TEST(eval_print_current_pkg_no_prefix)
{
    /* Symbols in current package print without prefix */
    ASSERT_STR_EQ(eval_print("(prin1-to-string 'CAR)"), "\"CAR\"");
}

TEST(eval_print_other_pkg_prefix)
{
    /* Symbols from other packages print with package prefix */
    eval_print("(make-package \"PR-FOO\")");
    eval_print("(intern \"XSYM\" (find-package \"PR-FOO\"))");
    eval_print("(export (find-symbol \"XSYM\" (find-package \"PR-FOO\")) (find-package \"PR-FOO\"))");
    ASSERT_STR_EQ(eval_print("(prin1-to-string 'PR-FOO:XSYM)"), "\"PR-FOO:XSYM\"");
}

TEST(eval_print_other_pkg_internal_prefix)
{
    /* Internal symbols from other packages print with :: prefix */
    eval_print("(make-package \"PR-FOO2\")");
    eval_print("(intern \"ISYM\" (find-package \"PR-FOO2\"))");
    ASSERT_STR_EQ(eval_print("(prin1-to-string 'PR-FOO2::ISYM)"), "\"PR-FOO2::ISYM\"");
}

/* ---- in-package tests ---- */

TEST(eval_star_package_initial)
{
    /* *PACKAGE* should initially be CL-USER */
    ASSERT_STR_EQ(eval_print("(package-name *package*)"), "\"COMMON-LISP-USER\"");
}

TEST(eval_in_package_basic)
{
    /* Create a package that uses CL, and switch to it */
    eval_print("(make-package \"IP-TEST1\" :use '(\"COMMON-LISP\"))");
    eval_print("(in-package \"IP-TEST1\")");
    ASSERT_STR_EQ(eval_print("(package-name *package*)"), "\"IP-TEST1\"");
    /* Switch back */
    eval_print("(in-package \"COMMON-LISP-USER\")");
    ASSERT_STR_EQ(eval_print("(package-name *package*)"), "\"COMMON-LISP-USER\"");
}

TEST(eval_in_package_returns_package)
{
    eval_print("(make-package \"IP-TEST2\" :use '(\"COMMON-LISP\"))");
    ASSERT_STR_EQ(eval_print("(in-package \"IP-TEST2\")"), "#<PACKAGE IP-TEST2>");
    eval_print("(in-package \"COMMON-LISP-USER\")");
}

TEST(eval_in_package_symbol_name)
{
    /* in-package should accept unquoted symbol name */
    eval_print("(make-package \"IP-TEST3\" :use '(\"COMMON-LISP\"))");
    eval_print("(in-package IP-TEST3)");
    ASSERT_STR_EQ(eval_print("(package-name *package*)"), "\"IP-TEST3\"");
    eval_print("(in-package \"COMMON-LISP-USER\")");
}

/* ---- defpackage tests ---- */

TEST(eval_defpackage_basic)
{
    ASSERT_STR_EQ(eval_print(
        "(package-name (defpackage \"DP-TEST1\"))"),
        "\"DP-TEST1\"");
    ASSERT_STR_EQ(eval_print("(find-package \"DP-TEST1\")"), "#<PACKAGE DP-TEST1>");
}

TEST(eval_defpackage_with_use)
{
    eval_print("(defpackage \"DP-USE\" (:use \"COMMON-LISP\"))");
    /* Should inherit CL symbols */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s st) (find-symbol \"CAR\" (find-package \"DP-USE\")) st)"),
        ":INHERITED");
}

TEST(eval_defpackage_with_export)
{
    eval_print("(defpackage \"DP-EXP\" (:export \"MY-FUNC\"))");
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s st) (find-symbol \"MY-FUNC\" (find-package \"DP-EXP\")) st)"),
        ":EXTERNAL");
}

TEST(eval_defpackage_full)
{
    eval_print("(defpackage \"DP-FULL\" (:use \"COMMON-LISP\") (:export \"PUB-SYM\"))");
    /* Should have use-list */
    ASSERT_STR_EQ(eval_print("(length (package-use-list (find-package \"DP-FULL\")))"), "1");
    /* PUB-SYM should be exported */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s st) (find-symbol \"PUB-SYM\" (find-package \"DP-FULL\")) st)"),
        ":EXTERNAL");
}

TEST(eval_defpackage_idempotent)
{
    /* defpackage on existing package should return it without error */
    eval_print("(defpackage \"DP-IDEM\" (:export \"A\"))");
    eval_print("(defpackage \"DP-IDEM\" (:export \"B\"))");
    /* Both A and B should be exported */
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s st) (find-symbol \"A\" (find-package \"DP-IDEM\")) st)"),
        ":EXTERNAL");
    ASSERT_STR_EQ(eval_print(
        "(multiple-value-bind (s st) (find-symbol \"B\" (find-package \"DP-IDEM\")) st)"),
        ":EXTERNAL");
}

/* ---- defpackage + in-package integration ---- */

TEST(eval_defpackage_in_package_workflow)
{
    eval_print("(defpackage \"MY-LIB\" (:use \"COMMON-LISP\") (:export \"MY-FUN\"))");
    eval_print("(in-package \"MY-LIB\")");
    eval_print("(defun my-fun (x) (* x 2))");
    eval_print("(in-package \"COMMON-LISP-USER\")");
    ASSERT_STR_EQ(eval_print("(my-lib:my-fun 21)"), "42");
}

/* ---- %package-symbols / %package-external-symbols ---- */

TEST(eval_package_symbols)
{
    eval_print("(make-package \"PS-TEST\")");
    eval_print("(intern \"A\" (find-package \"PS-TEST\"))");
    eval_print("(intern \"B\" (find-package \"PS-TEST\"))");
    eval_print("(intern \"C\" (find-package \"PS-TEST\"))");
    ASSERT_STR_EQ(eval_print("(length (%package-symbols (find-package \"PS-TEST\")))"), "3");
}

TEST(eval_package_external_symbols)
{
    eval_print("(make-package \"PES-TEST\")");
    eval_print("(intern \"X\" (find-package \"PES-TEST\"))");
    eval_print("(intern \"Y\" (find-package \"PES-TEST\"))");
    eval_print("(export (find-symbol \"X\" (find-package \"PES-TEST\")) (find-package \"PES-TEST\"))");
    /* Only X is exported */
    ASSERT_STR_EQ(eval_print("(length (%package-external-symbols (find-package \"PES-TEST\")))"), "1");
}

/* ---- do-symbols / do-external-symbols ---- */

TEST(eval_do_symbols)
{
    eval_print("(make-package \"DS-TEST\")");
    eval_print("(intern \"S1\" (find-package \"DS-TEST\"))");
    eval_print("(intern \"S2\" (find-package \"DS-TEST\"))");
    ASSERT_STR_EQ(eval_print(
        "(let ((count 0)) (do-symbols (s \"DS-TEST\") (incf count)) count)"),
        "2");
}

TEST(eval_do_external_symbols)
{
    eval_print("(make-package \"DES-TEST\")");
    eval_print("(intern \"E1\" (find-package \"DES-TEST\"))");
    eval_print("(intern \"E2\" (find-package \"DES-TEST\"))");
    eval_print("(export (find-symbol \"E1\" (find-package \"DES-TEST\")) (find-package \"DES-TEST\"))");
    ASSERT_STR_EQ(eval_print(
        "(let ((count 0)) (do-external-symbols (s \"DES-TEST\") (incf count)) count)"),
        "1");
}

TEST(eval_do_external_symbols_finds_car)
{
    /* CAR should be among CL's external symbols */
    ASSERT_STR_EQ(eval_print(
        "(let ((found nil)) (do-external-symbols (s \"COMMON-LISP\") (when (eq s 'car) (setq found t))) found)"),
        "T");
}

TEST(eval_do_symbols_keyword)
{
    /* KEYWORD package should have at least some symbols */
    ASSERT_STR_EQ(eval_print(
        "(let ((count 0)) (do-symbols (s \"KEYWORD\") (incf count)) (> count 0))"),
        "T");
}

/* ---- CDR-10: Package-local nicknames ---- */

TEST(c_local_nicknames_basic)
{
    CL_Obj pkg = cl_make_package("LN-TEST1");
    CL_Obj nick_str = cl_make_string("KW", 2);
    CL_Obj saved_pkg;
    CL_Obj found;

    cl_register_package(pkg);
    cl_add_package_local_nickname(nick_str, cl_package_keyword, pkg);

    /* Set as current package to test resolution */
    saved_pkg = cl_current_package;
    cl_current_package = pkg;

    found = cl_find_package("KW", 2);
    ASSERT(!CL_NULL_P(found));
    ASSERT(found == cl_package_keyword);

    cl_current_package = saved_pkg;
}

TEST(c_local_nicknames_scoped)
{
    CL_Obj pkg_a = cl_make_package("LN-SCOPE-A");
    CL_Obj pkg_b = cl_make_package("LN-SCOPE-B");
    CL_Obj nick_str = cl_make_string("MY-CL", 5);
    CL_Obj found;

    cl_register_package(pkg_a);
    cl_register_package(pkg_b);

    /* Add local nickname only to pkg_a */
    cl_add_package_local_nickname(nick_str, cl_package_cl, pkg_a);

    /* Should NOT resolve when pkg_b is current */
    cl_current_package = pkg_b;
    found = cl_find_package("MY-CL", 5);
    ASSERT(CL_NULL_P(found));

    /* SHOULD resolve when pkg_a is current */
    cl_current_package = pkg_a;
    found = cl_find_package("MY-CL", 5);
    ASSERT(!CL_NULL_P(found));
    ASSERT(found == cl_package_cl);

    cl_current_package = cl_package_cl_user;
}

TEST(c_local_nicknames_remove)
{
    CL_Obj pkg = cl_make_package("LN-REM");
    CL_Obj nick_str = cl_make_string("REMKW", 5);
    CL_Obj saved_pkg;
    CL_Obj found;

    cl_register_package(pkg);
    cl_add_package_local_nickname(nick_str, cl_package_keyword, pkg);

    saved_pkg = cl_current_package;
    cl_current_package = pkg;

    found = cl_find_package("REMKW", 5);
    ASSERT(!CL_NULL_P(found));

    cl_remove_package_local_nickname("REMKW", 5, pkg);
    found = cl_find_package("REMKW", 5);
    ASSERT(CL_NULL_P(found));

    cl_current_package = saved_pkg;
}

TEST(eval_add_local_nickname)
{
    eval_print("(make-package \"LN-EVAL1\" :use '(\"COMMON-LISP\"))");
    eval_print("(add-package-local-nickname \"K\" (find-package \"KEYWORD\") (find-package \"LN-EVAL1\"))");
    eval_print("(in-package \"LN-EVAL1\")");
    ASSERT_STR_EQ(eval_print("(find-package \"K\")"), "#<PACKAGE KEYWORD>");
    eval_print("(in-package \"COMMON-LISP-USER\")");
}

TEST(eval_remove_local_nickname)
{
    eval_print("(make-package \"LN-EVAL2\" :use '(\"COMMON-LISP\"))");
    eval_print("(add-package-local-nickname \"K2\" (find-package \"KEYWORD\") (find-package \"LN-EVAL2\"))");
    eval_print("(in-package \"LN-EVAL2\")");
    ASSERT_STR_EQ(eval_print("(find-package \"K2\")"), "#<PACKAGE KEYWORD>");
    eval_print("(remove-package-local-nickname \"K2\")");
    ASSERT_STR_EQ(eval_print("(find-package \"K2\")"), "NIL");
    eval_print("(in-package \"COMMON-LISP-USER\")");
}

TEST(eval_package_local_nicknames)
{
    eval_print("(make-package \"LN-EVAL3\" :use '(\"COMMON-LISP\"))");
    eval_print("(add-package-local-nickname \"MY-KW\" (find-package \"KEYWORD\") (find-package \"LN-EVAL3\"))");
    ASSERT_STR_EQ(eval_print("(length (package-local-nicknames (find-package \"LN-EVAL3\")))"), "1");
}

TEST(eval_defpackage_local_nicknames)
{
    eval_print("(defpackage \"LN-DP\" (:use \"COMMON-LISP\") (:local-nicknames (\"K\" \"KEYWORD\")))");
    eval_print("(in-package \"LN-DP\")");
    ASSERT_STR_EQ(eval_print("(find-package \"K\")"), "#<PACKAGE KEYWORD>");
    eval_print("(in-package \"COMMON-LISP-USER\")");
}

TEST(eval_local_nickname_reader)
{
    eval_print("(defpackage \"LN-RD\" (:use \"COMMON-LISP\") (:local-nicknames (\"K\" \"KEYWORD\")))");
    eval_print("(in-package \"LN-RD\")");
    /* K:TEST should resolve to KEYWORD:TEST = :TEST */
    ASSERT_STR_EQ(eval_print("(eq 'K:TEST :TEST)"), "T");
    eval_print("(in-package \"COMMON-LISP-USER\")");
}

TEST(eval_local_nickname_scope)
{
    /* Create two packages, only one has local nickname */
    eval_print("(defpackage \"LN-SC1\" (:use \"COMMON-LISP\") (:local-nicknames (\"KW\" \"KEYWORD\")))");
    eval_print("(defpackage \"LN-SC2\" (:use \"COMMON-LISP\"))");

    /* In LN-SC1, KW should resolve */
    eval_print("(in-package \"LN-SC1\")");
    ASSERT_STR_EQ(eval_print("(find-package \"KW\")"), "#<PACKAGE KEYWORD>");
    eval_print("(in-package \"COMMON-LISP-USER\")");

    /* In LN-SC2, KW should NOT resolve */
    eval_print("(in-package \"LN-SC2\")");
    ASSERT_STR_EQ(eval_print("(find-package \"KW\")"), "NIL");
    eval_print("(in-package \"COMMON-LISP-USER\")");
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

    /* Reader qualified syntax tests */
    RUN(c_make_uninterned_symbol);
    RUN(c_uninterned_symbols_are_unique);
    RUN(eval_read_pkg_external);
    RUN(eval_read_pkg_external_error);
    RUN(eval_read_pkg_internal);
    RUN(eval_read_pkg_internal_creates);
    RUN(eval_read_pkg_not_found);
    RUN(eval_read_cl_qualified);
    RUN(eval_read_keyword_qualified);
    RUN(eval_read_uninterned);
    RUN(eval_read_uninterned_unique);
    RUN(eval_print_uninterned);
    RUN(eval_print_keyword_unchanged);
    RUN(eval_print_current_pkg_no_prefix);
    RUN(eval_print_other_pkg_prefix);
    RUN(eval_print_other_pkg_internal_prefix);

    /* in-package tests */
    RUN(eval_star_package_initial);
    RUN(eval_in_package_basic);
    RUN(eval_in_package_returns_package);
    RUN(eval_in_package_symbol_name);

    /* defpackage tests */
    RUN(eval_defpackage_basic);
    RUN(eval_defpackage_with_use);
    RUN(eval_defpackage_with_export);
    RUN(eval_defpackage_full);
    RUN(eval_defpackage_idempotent);
    RUN(eval_defpackage_in_package_workflow);

    /* %package-symbols / %package-external-symbols */
    RUN(eval_package_symbols);
    RUN(eval_package_external_symbols);

    /* do-symbols / do-external-symbols */
    RUN(eval_do_symbols);
    RUN(eval_do_external_symbols);
    RUN(eval_do_external_symbols_finds_car);
    RUN(eval_do_symbols_keyword);

    /* CDR-10: Package-local nicknames */
    RUN(c_local_nicknames_basic);
    RUN(c_local_nicknames_scoped);
    RUN(c_local_nicknames_remove);
    RUN(eval_add_local_nickname);
    RUN(eval_remove_local_nickname);
    RUN(eval_package_local_nicknames);
    RUN(eval_defpackage_local_nicknames);
    RUN(eval_local_nickname_reader);
    RUN(eval_local_nickname_scope);

    teardown();
    REPORT();
}
