#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "vm.h"
#include "../platform/platform.h"
#include <string.h>

/* Helper to register a builtin */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    s->value = fn;
}

/* Pre-interned keyword symbols */
static CL_Obj KW_INTERNAL = CL_NIL;
static CL_Obj KW_EXTERNAL = CL_NIL;
static CL_Obj KW_INHERITED = CL_NIL;
static CL_Obj KW_NICKNAMES = CL_NIL;
static CL_Obj KW_USE = CL_NIL;
static CL_Obj KW_LOCAL_NICKNAMES = CL_NIL;

/* ---- Helpers ---- */

/* Coerce argument to package: if string, find-package; if package, return it */
static CL_Obj coerce_to_package(CL_Obj arg)
{
    if (CL_NULL_P(arg)) return cl_current_package;

    if (CL_PACKAGE_P(arg)) return arg;

    if (CL_STRING_P(arg)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(arg);
        CL_Obj pkg = cl_find_package(s->data, s->length);
        if (CL_NULL_P(pkg)) {
            cl_error(CL_ERR_GENERAL, "Package not found");
        }
        return pkg;
    }

    if (CL_SYMBOL_P(arg)) {
        CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(arg);
        CL_String *name = (CL_String *)CL_OBJ_TO_PTR(sym->name);
        CL_Obj pkg = cl_find_package(name->data, name->length);
        if (CL_NULL_P(pkg)) {
            cl_error(CL_ERR_GENERAL, "Package not found");
        }
        return pkg;
    }

    cl_error(CL_ERR_TYPE, "Not a package designator");
    return CL_NIL;
}

/* Get C string + length from a string or symbol name */
static void get_name_str(CL_Obj arg, const char **out_name, uint32_t *out_len)
{
    if (CL_STRING_P(arg)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(arg);
        *out_name = s->data;
        *out_len = s->length;
    } else if (CL_SYMBOL_P(arg)) {
        CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(arg);
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(sym->name);
        *out_name = s->data;
        *out_len = s->length;
    } else {
        cl_error(CL_ERR_TYPE, "Expected string or symbol");
    }
}

/* Return status keyword from int code */
static CL_Obj status_to_keyword(int status)
{
    switch (status) {
    case 1: return KW_INTERNAL;
    case 2: return KW_EXTERNAL;
    case 3: return KW_INHERITED;
    default: return CL_NIL;
    }
}

/* ---- Builtins ---- */

/* (make-package name &key nicknames use) */
static CL_Obj bi_make_package(CL_Obj *args, int nargs)
{
    const char *name;
    uint32_t len;
    CL_Obj pkg, existing;
    CL_Obj nicknames = CL_NIL;
    CL_Obj use = CL_NIL;
    CL_Obj local_nicks = CL_NIL;
    int i;

    get_name_str(args[0], &name, &len);

    /* Check for existing package */
    existing = cl_find_package(name, len);
    if (!CL_NULL_P(existing)) {
        cl_error(CL_ERR_GENERAL, "Package already exists");
        return CL_NIL;
    }

    /* Parse keyword args */
    for (i = 1; i + 1 < nargs; i += 2) {
        if (args[i] == KW_NICKNAMES) {
            nicknames = args[i + 1];
        } else if (args[i] == KW_USE) {
            use = args[i + 1];
        } else if (args[i] == KW_LOCAL_NICKNAMES) {
            local_nicks = args[i + 1];
        }
    }

    pkg = cl_make_package(name);
    CL_GC_PROTECT(pkg);

    /* Set nicknames */
    if (!CL_NULL_P(nicknames)) {
        CL_Package *p = (CL_Package *)CL_OBJ_TO_PTR(pkg);
        p->nicknames = nicknames;
    }

    /* Register */
    cl_register_package(pkg);

    /* Process :use list */
    while (!CL_NULL_P(use)) {
        CL_Obj used_pkg = coerce_to_package(cl_car(use));
        cl_use_package(used_pkg, pkg);
        use = cl_cdr(use);
    }

    /* Process :local-nicknames list — each entry is (nick-name target-package) */
    while (!CL_NULL_P(local_nicks)) {
        CL_Obj pair = cl_car(local_nicks);
        CL_Obj nick_designator = cl_car(pair);
        CL_Obj target_designator = cl_car(cl_cdr(pair));
        CL_Obj nick_str, target_pkg;
        const char *nick_name;
        uint32_t nick_len;

        get_name_str(nick_designator, &nick_name, &nick_len);
        nick_str = cl_make_string(nick_name, nick_len);
        target_pkg = coerce_to_package(target_designator);
        cl_add_package_local_nickname(nick_str, target_pkg, pkg);
        local_nicks = cl_cdr(local_nicks);
    }

    CL_GC_UNPROTECT(1);
    return pkg;
}

/* (find-package name) */
static CL_Obj bi_find_package(CL_Obj *args, int nargs)
{
    const char *name;
    uint32_t len;
    (void)nargs;

    if (CL_PACKAGE_P(args[0])) return args[0];

    get_name_str(args[0], &name, &len);
    return cl_find_package(name, len);
}

/* (delete-package package) */
static CL_Obj bi_delete_package(CL_Obj *args, int nargs)
{
    CL_Obj pkg = coerce_to_package(args[0]);
    CL_Obj prev = CL_NIL;
    CL_Obj reg = cl_package_registry;
    (void)nargs;

    /* Remove from registry */
    while (!CL_NULL_P(reg)) {
        CL_Obj entry = cl_car(reg);
        if (cl_cdr(entry) == pkg) {
            if (CL_NULL_P(prev)) {
                cl_package_registry = cl_cdr(reg);
            } else {
                CL_Cons *c = (CL_Cons *)CL_OBJ_TO_PTR(prev);
                c->cdr = cl_cdr(reg);
            }
            return SYM_T;
        }
        prev = reg;
        reg = cl_cdr(reg);
    }
    return CL_NIL;
}

/* (rename-package package new-name &optional new-nicknames) */
static CL_Obj bi_rename_package(CL_Obj *args, int nargs)
{
    CL_Obj pkg = coerce_to_package(args[0]);
    CL_Package *p = (CL_Package *)CL_OBJ_TO_PTR(pkg);
    const char *new_name;
    uint32_t new_len;

    get_name_str(args[1], &new_name, &new_len);
    p->name = cl_make_string(new_name, new_len);

    if (nargs > 2) {
        p->nicknames = args[2];
    } else {
        p->nicknames = CL_NIL;
    }

    return pkg;
}

/* (export symbols &optional package) */
static CL_Obj bi_export(CL_Obj *args, int nargs)
{
    CL_Obj symbols = args[0];
    CL_Obj pkg = (nargs > 1) ? coerce_to_package(args[1]) : cl_current_package;

    if (CL_SYMBOL_P(symbols) || CL_NULL_P(symbols)) {
        /* Single symbol */
        cl_export_symbol(symbols, pkg);
    } else {
        /* List of symbols */
        while (!CL_NULL_P(symbols)) {
            cl_export_symbol(cl_car(symbols), pkg);
            symbols = cl_cdr(symbols);
        }
    }
    return SYM_T;
}

/* (unexport symbols &optional package) */
static CL_Obj bi_unexport(CL_Obj *args, int nargs)
{
    CL_Obj symbols = args[0];
    CL_Obj pkg = (nargs > 1) ? coerce_to_package(args[1]) : cl_current_package;

    if (CL_SYMBOL_P(symbols) || CL_NULL_P(symbols)) {
        cl_unexport_symbol(symbols, pkg);
    } else {
        while (!CL_NULL_P(symbols)) {
            cl_unexport_symbol(cl_car(symbols), pkg);
            symbols = cl_cdr(symbols);
        }
    }
    return SYM_T;
}

/* (import symbols &optional package) */
static CL_Obj bi_import(CL_Obj *args, int nargs)
{
    CL_Obj symbols = args[0];
    CL_Obj pkg = (nargs > 1) ? coerce_to_package(args[1]) : cl_current_package;

    if (CL_SYMBOL_P(symbols) || CL_NULL_P(symbols)) {
        cl_import_symbol(symbols, pkg);
    } else {
        while (!CL_NULL_P(symbols)) {
            cl_import_symbol(cl_car(symbols), pkg);
            symbols = cl_cdr(symbols);
        }
    }
    return SYM_T;
}

/* (use-package packages-to-use &optional package) */
static CL_Obj bi_use_package(CL_Obj *args, int nargs)
{
    CL_Obj to_use = args[0];
    CL_Obj pkg = (nargs > 1) ? coerce_to_package(args[1]) : cl_current_package;

    if (CL_PACKAGE_P(to_use) || CL_STRING_P(to_use) || CL_SYMBOL_P(to_use)) {
        cl_use_package(coerce_to_package(to_use), pkg);
    } else {
        /* List */
        while (!CL_NULL_P(to_use)) {
            cl_use_package(coerce_to_package(cl_car(to_use)), pkg);
            to_use = cl_cdr(to_use);
        }
    }
    return SYM_T;
}

/* (unuse-package packages-to-unuse &optional package) */
static CL_Obj bi_unuse_package(CL_Obj *args, int nargs)
{
    CL_Obj to_unuse = args[0];
    CL_Obj pkg = (nargs > 1) ? coerce_to_package(args[1]) : cl_current_package;

    if (CL_PACKAGE_P(to_unuse) || CL_STRING_P(to_unuse) || CL_SYMBOL_P(to_unuse)) {
        cl_unuse_package(coerce_to_package(to_unuse), pkg);
    } else {
        while (!CL_NULL_P(to_unuse)) {
            cl_unuse_package(coerce_to_package(cl_car(to_unuse)), pkg);
            to_unuse = cl_cdr(to_unuse);
        }
    }
    return SYM_T;
}

/* (shadow symbols &optional package) */
static CL_Obj bi_shadow(CL_Obj *args, int nargs)
{
    CL_Obj symbols = args[0];
    CL_Obj pkg = (nargs > 1) ? coerce_to_package(args[1]) : cl_current_package;

    if (CL_STRING_P(symbols) || CL_SYMBOL_P(symbols)) {
        const char *name;
        uint32_t len;
        get_name_str(symbols, &name, &len);
        cl_shadow_symbol(name, len, pkg);
    } else {
        while (!CL_NULL_P(symbols)) {
            const char *name;
            uint32_t len;
            get_name_str(cl_car(symbols), &name, &len);
            cl_shadow_symbol(name, len, pkg);
            symbols = cl_cdr(symbols);
        }
    }
    return SYM_T;
}

/* (find-symbol name &optional package) — returns 2 values: symbol, status */
static CL_Obj bi_find_symbol(CL_Obj *args, int nargs)
{
    const char *name;
    uint32_t len;
    CL_Obj pkg;
    int status;
    CL_Obj sym;

    if (!CL_STRING_P(args[0])) {
        cl_error(CL_ERR_TYPE, "FIND-SYMBOL: name must be a string");
        return CL_NIL;
    }

    {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
        name = s->data;
        len = s->length;
    }

    pkg = (nargs > 1) ? coerce_to_package(args[1]) : cl_current_package;
    sym = cl_find_symbol_with_status(name, len, pkg, &status);

    cl_mv_count = 2;
    cl_mv_values[0] = sym;
    cl_mv_values[1] = status_to_keyword(status);
    return sym;
}

/* (intern name &optional package) — returns 2 values: symbol, status */
static CL_Obj bi_intern(CL_Obj *args, int nargs)
{
    const char *name;
    uint32_t len;
    CL_Obj pkg;
    int status;
    CL_Obj existing;
    CL_Obj sym;

    if (!CL_STRING_P(args[0])) {
        cl_error(CL_ERR_TYPE, "INTERN: name must be a string");
        return CL_NIL;
    }

    {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
        name = s->data;
        len = s->length;
    }

    pkg = (nargs > 1) ? coerce_to_package(args[1]) : cl_current_package;

    /* Check if already present */
    existing = cl_find_symbol_with_status(name, len, pkg, &status);
    if (status != 0) {
        cl_mv_count = 2;
        cl_mv_values[0] = existing;
        cl_mv_values[1] = status_to_keyword(status);
        return existing;
    }

    /* Create new symbol in package */
    sym = cl_intern_in(name, len, pkg);
    cl_mv_count = 2;
    cl_mv_values[0] = sym;
    cl_mv_values[1] = CL_NIL; /* NIL = newly created */
    return sym;
}

/* (unintern symbol &optional package) */
static CL_Obj bi_unintern(CL_Obj *args, int nargs)
{
    CL_Obj sym = args[0];
    CL_Obj pkg_obj;
    CL_Package *pkg;
    CL_Vector *tbl;
    CL_Symbol *s;
    uint32_t idx;
    CL_Obj prev, list;

    if (!CL_SYMBOL_P(sym)) {
        cl_error(CL_ERR_TYPE, "UNINTERN: not a symbol");
        return CL_NIL;
    }

    pkg_obj = (nargs > 1) ? coerce_to_package(args[1]) : cl_current_package;
    pkg = (CL_Package *)CL_OBJ_TO_PTR(pkg_obj);
    tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    idx = s->hash % tbl->length;

    /* Remove from bucket chain */
    prev = CL_NIL;
    list = tbl->data[idx];
    while (!CL_NULL_P(list)) {
        if (cl_car(list) == sym) {
            if (CL_NULL_P(prev)) {
                tbl->data[idx] = cl_cdr(list);
            } else {
                CL_Cons *c = (CL_Cons *)CL_OBJ_TO_PTR(prev);
                c->cdr = cl_cdr(list);
            }
            pkg->sym_count--;
            /* Clear home package if this was it */
            if (s->package == pkg_obj) {
                s->package = CL_NIL;
            }
            return SYM_T;
        }
        prev = list;
        list = cl_cdr(list);
    }
    return CL_NIL;
}

/* (package-name package) */
static CL_Obj bi_package_name(CL_Obj *args, int nargs)
{
    CL_Obj pkg = coerce_to_package(args[0]);
    CL_Package *p = (CL_Package *)CL_OBJ_TO_PTR(pkg);
    (void)nargs;
    return p->name;
}

/* (package-use-list package) */
static CL_Obj bi_package_use_list(CL_Obj *args, int nargs)
{
    CL_Obj pkg = coerce_to_package(args[0]);
    CL_Package *p = (CL_Package *)CL_OBJ_TO_PTR(pkg);
    (void)nargs;
    return p->use_list;
}

/* (package-nicknames package) */
static CL_Obj bi_package_nicknames(CL_Obj *args, int nargs)
{
    CL_Obj pkg = coerce_to_package(args[0]);
    CL_Package *p = (CL_Package *)CL_OBJ_TO_PTR(pkg);
    (void)nargs;
    return p->nicknames;
}

/* (list-all-packages) */
static CL_Obj bi_list_all_packages(CL_Obj *args, int nargs)
{
    CL_Obj result = CL_NIL;
    CL_Obj reg = cl_package_registry;
    (void)args;
    (void)nargs;

    CL_GC_PROTECT(result);
    while (!CL_NULL_P(reg)) {
        CL_Obj entry = cl_car(reg);
        result = cl_cons(cl_cdr(entry), result);
        reg = cl_cdr(reg);
    }
    CL_GC_UNPROTECT(1);
    return result;
}

/* (%package-symbols package) — list of all symbols in package */
static CL_Obj bi_package_symbols(CL_Obj *args, int nargs)
{
    CL_Obj pkg_obj = coerce_to_package(args[0]);
    CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(pkg_obj);
    CL_Vector *tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
    CL_Obj result = CL_NIL;
    uint32_t i;
    (void)nargs;

    CL_GC_PROTECT(result);
    for (i = 0; i < tbl->length; i++) {
        CL_Obj bucket = tbl->data[i];
        while (!CL_NULL_P(bucket)) {
            result = cl_cons(cl_car(bucket), result);
            bucket = cl_cdr(bucket);
        }
    }
    CL_GC_UNPROTECT(1);
    return result;
}

/* (%package-external-symbols package) — list of exported symbols in package */
static CL_Obj bi_package_external_symbols(CL_Obj *args, int nargs)
{
    CL_Obj pkg_obj = coerce_to_package(args[0]);
    CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(pkg_obj);
    CL_Vector *tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
    CL_Obj result = CL_NIL;
    uint32_t i;
    (void)nargs;

    CL_GC_PROTECT(result);
    for (i = 0; i < tbl->length; i++) {
        CL_Obj bucket = tbl->data[i];
        while (!CL_NULL_P(bucket)) {
            CL_Obj sym = cl_car(bucket);
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            if (s->flags & CL_SYM_EXPORTED) {
                result = cl_cons(sym, result);
            }
            bucket = cl_cdr(bucket);
        }
    }
    CL_GC_UNPROTECT(1);
    return result;
}

/* ---- CDR-10: Package-local nicknames ---- */

/* (package-local-nicknames package) — returns alist of (nick-string . package) */
static CL_Obj bi_package_local_nicknames(CL_Obj *args, int nargs)
{
    CL_Obj pkg = coerce_to_package(args[0]);
    CL_Package *p = (CL_Package *)CL_OBJ_TO_PTR(pkg);
    (void)nargs;
    return p->local_nicknames;
}

/* (add-package-local-nickname nick-name target-package &optional package) */
static CL_Obj bi_add_package_local_nickname(CL_Obj *args, int nargs)
{
    const char *nick_name;
    uint32_t nick_len;
    CL_Obj nick_str, target_pkg, in_pkg;

    get_name_str(args[0], &nick_name, &nick_len);
    nick_str = cl_make_string(nick_name, nick_len);
    target_pkg = coerce_to_package(args[1]);
    in_pkg = (nargs > 2) ? coerce_to_package(args[2]) : cl_current_package;
    cl_add_package_local_nickname(nick_str, target_pkg, in_pkg);
    return SYM_T;
}

/* (remove-package-local-nickname nick-name &optional package) */
static CL_Obj bi_remove_package_local_nickname(CL_Obj *args, int nargs)
{
    const char *nick_name;
    uint32_t nick_len;
    CL_Obj from_pkg;

    get_name_str(args[0], &nick_name, &nick_len);
    from_pkg = (nargs > 1) ? coerce_to_package(args[1]) : cl_current_package;
    cl_remove_package_local_nickname(nick_name, nick_len, from_pkg);
    return SYM_T;
}

/* ---- Init ---- */

void cl_builtins_package_init(void)
{
    /* Pre-intern keywords */
    KW_INTERNAL  = cl_intern_keyword("INTERNAL", 8);
    KW_EXTERNAL  = cl_intern_keyword("EXTERNAL", 8);
    KW_INHERITED = cl_intern_keyword("INHERITED", 9);
    KW_NICKNAMES       = cl_intern_keyword("NICKNAMES", 9);
    KW_USE             = cl_intern_keyword("USE", 3);
    KW_LOCAL_NICKNAMES = cl_intern_keyword("LOCAL-NICKNAMES", 15);

    defun("MAKE-PACKAGE", bi_make_package, 1, -1);
    defun("FIND-PACKAGE", bi_find_package, 1, 1);
    defun("DELETE-PACKAGE", bi_delete_package, 1, 1);
    defun("RENAME-PACKAGE", bi_rename_package, 2, 3);
    defun("EXPORT", bi_export, 1, 2);
    defun("UNEXPORT", bi_unexport, 1, 2);
    defun("IMPORT", bi_import, 1, 2);
    defun("USE-PACKAGE", bi_use_package, 1, 2);
    defun("UNUSE-PACKAGE", bi_unuse_package, 1, 2);
    defun("SHADOW", bi_shadow, 1, 2);
    defun("FIND-SYMBOL", bi_find_symbol, 1, 2);
    defun("INTERN", bi_intern, 1, 2);
    defun("UNINTERN", bi_unintern, 1, 2);
    defun("PACKAGE-NAME", bi_package_name, 1, 1);
    defun("PACKAGE-USE-LIST", bi_package_use_list, 1, 1);
    defun("PACKAGE-NICKNAMES", bi_package_nicknames, 1, 1);
    defun("LIST-ALL-PACKAGES", bi_list_all_packages, 0, 0);
    defun("%PACKAGE-SYMBOLS", bi_package_symbols, 1, 1);
    defun("%PACKAGE-EXTERNAL-SYMBOLS", bi_package_external_symbols, 1, 1);
    defun("PACKAGE-LOCAL-NICKNAMES", bi_package_local_nicknames, 1, 1);
    defun("ADD-PACKAGE-LOCAL-NICKNAME", bi_add_package_local_nickname, 2, 3);
    defun("REMOVE-PACKAGE-LOCAL-NICKNAME", bi_remove_package_local_nickname, 1, 2);
}
