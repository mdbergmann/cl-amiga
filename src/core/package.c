#include "package.h"
#include "symbol.h"
#include "mem.h"
#include "error.h"
#include "compiler.h"
#include "../platform/platform.h"
#include "../platform/platform_thread.h"
#include "thread.h"
#include <string.h>

/* External tables for checking symbol bindings */
extern CL_Obj struct_table;           /* builtins_struct.c */
extern CL_Obj cl_clos_class_table;    /* builtins_struct.c */

CL_Obj cl_package_cl = CL_NIL;
CL_Obj cl_package_cl_user = CL_NIL;
CL_Obj cl_package_keyword = CL_NIL;
CL_Obj cl_package_ext = CL_NIL;
CL_Obj cl_package_clamiga = CL_NIL;
CL_Obj cl_package_mp = CL_NIL;
CL_Obj cl_package_ffi = CL_NIL;
CL_Obj cl_package_amiga = CL_NIL;
CL_Obj cl_current_package = CL_NIL;
CL_Obj cl_package_registry = CL_NIL;

void *cl_package_rwlock = NULL;

/* Refresh cl_current_package from the dynamic *PACKAGE* binding.
 * Safe to call before SYM_STAR_PACKAGE is interned (SYM_STAR_PACKAGE
 * starts at CL_NIL during early init) or before a package object is
 * installed — in both cases we leave cl_current_package unchanged. */
void cl_sync_current_package_from_dynamic(void)
{
    CL_Obj sp = SYM_STAR_PACKAGE;
    CL_Obj val;
    if (!CL_SYMBOL_P(sp))
        return;
    val = cl_symbol_value(sp);
    if (CL_NULL_P(val) || !CL_PACKAGE_P(val))
        return;
    cl_current_package = val;
}

/* ---- helpers ---- */

/* Compare CL_String object against C string */
static int str_eq(CL_Obj str_obj, const char *name, uint32_t len)
{
    CL_String *s = (CL_String *)CL_OBJ_TO_PTR(str_obj);
    return s->length == len && memcmp(s->data, name, len) == 0;
}

/* Search a single package's own symbol table (no use-list) */
static CL_Obj find_own_symbol(const char *name, uint32_t len, CL_Obj package)
{
    CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
    CL_Vector *tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
    uint32_t hash = cl_hash_string(name, len);
    uint32_t idx = hash % tbl->length;
    CL_Obj list = tbl->data[idx];

    while (!CL_NULL_P(list)) {
        CL_Obj sym = cl_car(list);
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
        CL_String *sname = (CL_String *)CL_OBJ_TO_PTR(s->name);
        if (sname->length == len && memcmp(sname->data, name, len) == 0) {
            return sym;
        }
        list = cl_cdr(list);
    }
    return CL_NIL;
}

/* ---- nolock helpers for internal use under existing lock ---- */

static CL_Obj find_external_nolock(const char *name, uint32_t len, CL_Obj package)
{
    CL_Obj sym = find_own_symbol(name, len, package);
    if (!CL_NULL_P(sym)) {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
        if (s->flags & CL_SYM_EXPORTED)
            return sym;
    }
    return CL_NIL;
}

CL_Obj cl_package_find_symbol_nolock(const char *name, uint32_t len, CL_Obj package)
{
    CL_Obj sym;
    sym = find_own_symbol(name, len, package);
    if (!CL_NULL_P(sym)) return sym;
    {
        CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
        CL_Obj uses = pkg->use_list;
        while (!CL_NULL_P(uses)) {
            CL_Obj found = find_external_nolock(name, len, cl_car(uses));
            if (!CL_NULL_P(found)) return found;
            uses = cl_cdr(uses);
        }
    }
    return CL_NIL;
}

static void import_symbol_nolock(CL_Obj sym, CL_Obj package)
{
    CL_Symbol *s;
    CL_String *sname;
    CL_Obj existing;

    if (CL_NULL_P(sym)) return;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    sname = (CL_String *)CL_OBJ_TO_PTR(s->name);

    existing = find_own_symbol(sname->data, sname->length, package);
    if (!CL_NULL_P(existing)) {
        if (existing == sym) return;
        cl_error(CL_ERR_GENERAL, "IMPORT conflict: symbol already present in package");
        return;
    }

    {
        CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
        CL_Vector *tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
        uint32_t idx = s->hash % tbl->length;

        CL_GC_PROTECT(package);
        CL_GC_PROTECT(sym);
        tbl->data[idx] = cl_cons(sym, tbl->data[idx]);
        CL_GC_UNPROTECT(2);
        pkg->sym_count++;
    }
}

/* ---- public API ---- */

CL_Obj cl_make_package(const char *name)
{
    CL_Obj name_str, symtab;
    CL_Package *pkg;

    name_str = cl_make_string(name, (uint32_t)strlen(name));
    CL_GC_PROTECT(name_str);

    symtab = cl_make_vector(CL_SYMTAB_SIZE);
    CL_GC_PROTECT(symtab);

    pkg = (CL_Package *)cl_alloc(TYPE_PACKAGE,
                                  sizeof(CL_Package));
    CL_GC_UNPROTECT(2);

    if (!pkg) return CL_NIL;
    pkg->name = name_str;
    pkg->symbols = symtab;
    pkg->use_list = CL_NIL;
    pkg->nicknames = CL_NIL;
    pkg->local_nicknames = CL_NIL;
    pkg->shadowing_symbols = CL_NIL;
    pkg->sym_count = 0;
    return CL_PTR_TO_OBJ(pkg);
}

void cl_package_add_symbol(CL_Obj package, CL_Obj symbol)
{
    CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
    CL_Vector *tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(symbol);
    uint32_t idx = sym->hash % tbl->length;
    CL_Obj chain = tbl->data[idx];

    /* Prepend to bucket — cl_cons may trigger GC/compaction */
    CL_GC_PROTECT(package);
    CL_GC_PROTECT(symbol);
    CL_GC_PROTECT(chain);
    chain = cl_cons(symbol, chain);
    CL_GC_UNPROTECT(3);

    /* Re-derive pointers after potential GC/compaction */
    pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
    tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
    sym = (CL_Symbol *)CL_OBJ_TO_PTR(symbol);
    tbl->data[idx] = chain;

    sym->package = package;
    pkg->sym_count++;
}

CL_Obj cl_package_find_external(const char *name, uint32_t len, CL_Obj package)
{
    CL_Obj result;
    if (CL_MT()) platform_rwlock_rdlock(cl_package_rwlock);
    result = find_external_nolock(name, len, package);
    if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
    return result;
}

CL_Obj cl_package_find_symbol(const char *name, uint32_t len, CL_Obj package)
{
    CL_Obj result;
    if (CL_MT()) platform_rwlock_rdlock(cl_package_rwlock);
    result = cl_package_find_symbol_nolock(name, len, package);
    if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
    return result;
}

CL_Obj cl_find_symbol_with_status(const char *name, uint32_t len,
                                   CL_Obj package, int *status)
{
    CL_Obj sym;

    if (CL_MT()) platform_rwlock_rdlock(cl_package_rwlock);

    /* Search own symbol table first */
    sym = find_own_symbol(name, len, package);
    if (!CL_NULL_P(sym)) {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
        if (s->flags & CL_SYM_EXPORTED) {
            *status = 2; /* :EXTERNAL */
        } else {
            *status = 1; /* :INTERNAL */
        }
        if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
        return sym;
    }

    /* Search use-list — only exported symbols */
    {
        CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
        CL_Obj uses = pkg->use_list;
        while (!CL_NULL_P(uses)) {
            CL_Obj found = find_external_nolock(name, len, cl_car(uses));
            if (!CL_NULL_P(found)) {
                *status = 3; /* :INHERITED */
                if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
                return found;
            }
            uses = cl_cdr(uses);
        }
    }

    *status = 0; /* not found */
    if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
    return CL_NIL;
}

CL_Obj cl_find_package(const char *name, uint32_t len)
{
    CL_Obj reg;
    CL_Obj result = CL_NIL;

    if (CL_MT()) platform_rwlock_rdlock(cl_package_rwlock);

    /* CDR-10: Check local nicknames of current package first */
    if (!CL_NULL_P(cl_current_package)) {
        CL_Package *cur = (CL_Package *)CL_OBJ_TO_PTR(cl_current_package);
        CL_Obj lnicks = cur->local_nicknames;
        while (!CL_NULL_P(lnicks)) {
            CL_Obj entry = cl_car(lnicks);
            if (str_eq(cl_car(entry), name, len)) {
                result = cl_cdr(entry);
                if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
                return result;
            }
            lnicks = cl_cdr(lnicks);
        }
    }

    reg = cl_package_registry;

    while (!CL_NULL_P(reg)) {
        CL_Obj entry = cl_car(reg);
        CL_Obj pkg = cl_cdr(entry);
        CL_Package *p = (CL_Package *)CL_OBJ_TO_PTR(pkg);

        /* Check primary name */
        if (str_eq(p->name, name, len)) {
            if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
            return pkg;
        }

        /* Check nicknames */
        {
            CL_Obj nicks = p->nicknames;
            while (!CL_NULL_P(nicks)) {
                if (str_eq(cl_car(nicks), name, len)) {
                    if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
                    return pkg;
                }
                nicks = cl_cdr(nicks);
            }
        }

        reg = cl_cdr(reg);
    }
    if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
    return CL_NIL;
}

void cl_register_package(CL_Obj pkg)
{
    CL_Package *p = (CL_Package *)CL_OBJ_TO_PTR(pkg);
    CL_Obj entry;

    CL_GC_PROTECT(pkg);
    entry = cl_cons(p->name, pkg);
    CL_GC_PROTECT(entry);
    if (CL_MT()) platform_rwlock_wrlock(cl_package_rwlock);
    cl_package_registry = cl_cons(entry, cl_package_registry);
    if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
    CL_GC_UNPROTECT(2);
}

void cl_export_symbol(CL_Obj sym, CL_Obj package)
{
    CL_Symbol *s;
    CL_String *sname;
    if (CL_NULL_P(sym)) return;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    sname = (CL_String *)CL_OBJ_TO_PTR(s->name);

    if (CL_MT()) platform_rwlock_wrlock(cl_package_rwlock);

    /* If symbol is not present in package's own table, import it first.
       Per CL spec: "If the symbol is accessible via use-package,
       it is first imported into package, after which it is exported." */
    if (CL_NULL_P(find_own_symbol(sname->data, sname->length, package))) {
        import_symbol_nolock(sym, package);
    }

    s->flags |= CL_SYM_EXPORTED;
    if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
}

/* Unexport SYM from PACKAGE.
 *
 * The CL_SYM_EXPORTED flag lives on the symbol, not per-package, so a naive
 * implementation that ignores PACKAGE would clobber the export status in the
 * symbol's home package whenever a using package tries to unexport an
 * inherited or imported symbol.  uiop:define-package's "remove no-longer
 * listed externals" pass relies on (unexport sym other-pkg) being safe; if we
 * cleared the flag globally, alexandria:array-index would lose its EXTERNAL
 * status the moment serapeum's define-package walked its own external list.
 *
 * Strategy: only clear the flag when SYM's home package is PACKAGE.  For
 * cross-package calls (sym home != package), this is a no-op — matching the
 * intent of "stop including this in package's external view" while preserving
 * the home package's export. */
void cl_unexport_symbol(CL_Obj sym, CL_Obj package)
{
    CL_Symbol *s;
    if (CL_NULL_P(sym)) return;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    if (s->package != package) return;
    if (CL_MT()) platform_rwlock_wrlock(cl_package_rwlock);
    s->flags &= ~CL_SYM_EXPORTED;
    if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
}

void cl_import_symbol(CL_Obj sym, CL_Obj package)
{
    if (CL_NULL_P(sym)) return;
    if (CL_MT()) platform_rwlock_wrlock(cl_package_rwlock);
    import_symbol_nolock(sym, package);
    if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
}

void cl_shadow_symbol(const char *name, uint32_t len, CL_Obj package)
{
    CL_Obj existing;

    if (CL_MT()) platform_rwlock_wrlock(cl_package_rwlock);
    existing = find_own_symbol(name, len, package);

    if (!CL_NULL_P(existing)) {
        /* Symbol already directly present — nothing to do */
        if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
        return;
    }

    /* Create new symbol in this package */
    {
        CL_Obj name_str = cl_make_string(name, len);
        CL_Obj sym;
        CL_Symbol *s;

        CL_GC_PROTECT(name_str);
        CL_GC_PROTECT(package);
        sym = cl_make_symbol(name_str);
        CL_GC_UNPROTECT(2);

        s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
        s->hash = cl_hash_string(name, len);
        cl_package_add_symbol(package, sym);
    }
    if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
}

void cl_use_package(CL_Obj pkg_to_use, CL_Obj using_pkg)
{
    CL_Package *user;
    CL_Obj list;

    if (pkg_to_use == using_pkg) return;

    if (CL_MT()) platform_rwlock_wrlock(cl_package_rwlock);
    user = (CL_Package *)CL_OBJ_TO_PTR(using_pkg);

    /* Check if already in use-list */
    list = user->use_list;
    while (!CL_NULL_P(list)) {
        if (cl_car(list) == pkg_to_use) {
            if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
            return; /* already using */
        }
        list = cl_cdr(list);
    }

    /* Add to use-list */
    CL_GC_PROTECT(using_pkg);
    CL_GC_PROTECT(pkg_to_use);
    user->use_list = cl_cons(pkg_to_use, user->use_list);
    CL_GC_UNPROTECT(2);
    if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
}

void cl_unuse_package(CL_Obj pkg_to_unuse, CL_Obj using_pkg)
{
    CL_Package *user;
    CL_Obj prev, list;

    if (CL_MT()) platform_rwlock_wrlock(cl_package_rwlock);
    user = (CL_Package *)CL_OBJ_TO_PTR(using_pkg);
    prev = CL_NIL;
    list = user->use_list;

    while (!CL_NULL_P(list)) {
        if (cl_car(list) == pkg_to_unuse) {
            if (CL_NULL_P(prev)) {
                user->use_list = cl_cdr(list);
            } else {
                CL_Cons *c = (CL_Cons *)CL_OBJ_TO_PTR(prev);
                c->cdr = cl_cdr(list);
            }
            if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
            return;
        }
        prev = list;
        list = cl_cdr(list);
    }
    if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
}

void cl_add_package_local_nickname(CL_Obj nick_str, CL_Obj target_pkg, CL_Obj in_pkg)
{
    CL_Package *p;
    CL_String *ns;
    CL_Obj list;

    if (CL_MT()) platform_rwlock_wrlock(cl_package_rwlock);
    p = (CL_Package *)CL_OBJ_TO_PTR(in_pkg);
    ns = (CL_String *)CL_OBJ_TO_PTR(nick_str);
    list = p->local_nicknames;

    /* Check for duplicate — replace if exists */
    while (!CL_NULL_P(list)) {
        CL_Obj entry = cl_car(list);
        if (str_eq(cl_car(entry), ns->data, ns->length)) {
            /* Replace target */
            CL_Cons *c = (CL_Cons *)CL_OBJ_TO_PTR(entry);
            c->cdr = target_pkg;
            if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
            return;
        }
        list = cl_cdr(list);
    }

    /* Prepend new entry */
    {
        CL_Obj entry;
        CL_GC_PROTECT(in_pkg);
        CL_GC_PROTECT(nick_str);
        CL_GC_PROTECT(target_pkg);
        entry = cl_cons(nick_str, target_pkg);
        CL_GC_PROTECT(entry);
        p = (CL_Package *)CL_OBJ_TO_PTR(in_pkg);
        p->local_nicknames = cl_cons(entry, p->local_nicknames);
        CL_GC_UNPROTECT(4);
    }
    if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
}

void cl_remove_package_local_nickname(const char *name, uint32_t len, CL_Obj from_pkg)
{
    CL_Package *p;
    CL_Obj prev, list;

    if (CL_MT()) platform_rwlock_wrlock(cl_package_rwlock);
    p = (CL_Package *)CL_OBJ_TO_PTR(from_pkg);
    prev = CL_NIL;
    list = p->local_nicknames;

    while (!CL_NULL_P(list)) {
        CL_Obj entry = cl_car(list);
        if (str_eq(cl_car(entry), name, len)) {
            if (CL_NULL_P(prev)) {
                p->local_nicknames = cl_cdr(list);
            } else {
                CL_Cons *c = (CL_Cons *)CL_OBJ_TO_PTR(prev);
                c->cdr = cl_cdr(list);
            }
            if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
            return;
        }
        prev = list;
        list = cl_cdr(list);
    }
    if (CL_MT()) platform_rwlock_unlock(cl_package_rwlock);
}

void cl_package_export_all_cl_symbols(void)
{
    CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(cl_package_cl);
    CL_Vector *tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
    uint32_t i;

    for (i = 0; i < tbl->length; i++) {
        CL_Obj list = tbl->data[i];
        while (!CL_NULL_P(list)) {
            CL_Obj sym = cl_car(list);
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            s->flags |= CL_SYM_EXPORTED;
            list = cl_cdr(list);
        }
    }

    /* Also export all keywords */
    pkg = (CL_Package *)CL_OBJ_TO_PTR(cl_package_keyword);
    tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
    for (i = 0; i < tbl->length; i++) {
        CL_Obj list = tbl->data[i];
        while (!CL_NULL_P(list)) {
            CL_Obj sym = cl_car(list);
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            s->flags |= CL_SYM_EXPORTED;
            list = cl_cdr(list);
        }
    }

    /* Also export all CLAMIGA internal symbols */
    pkg = (CL_Package *)CL_OBJ_TO_PTR(cl_package_clamiga);
    tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
    for (i = 0; i < tbl->length; i++) {
        CL_Obj list = tbl->data[i];
        while (!CL_NULL_P(list)) {
            CL_Obj sym = cl_car(list);
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            s->flags |= CL_SYM_EXPORTED;
            list = cl_cdr(list);
        }
    }
}

/* Helper: check if a CL symbol has a real definition (function, value,
   macro, type, struct, or CLOS class).  Used to filter out stray symbols
   that were accidentally interned in CL by boot.lisp macro bodies. */
static int symbol_has_binding(CL_Obj sym)
{
    extern int cl_is_struct_type(CL_Obj type_sym);
    extern int cl_clos_class_exists(CL_Obj name);
    extern int cl_is_builtin_type_name(const char *name);

    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);

    /* Has function or value binding */
    if (s->function != CL_UNBOUND) return 1;
    if (s->value != CL_UNBOUND)    return 1;

    /* Is special or constant */
    if (s->flags & (CL_SYM_SPECIAL | CL_SYM_CONSTANT)) return 1;

    /* Is a macro or type expander */
    if (cl_macro_p(sym))                          return 1;
    if (!CL_NULL_P(cl_get_type_expander(sym)))    return 1;

    /* Is a struct type or CLOS class */
    if (cl_is_struct_type(sym))   return 1;
    if (cl_clos_class_exists(sym)) return 1;

    /* Is a built-in type name recognized by typep */
    if (cl_is_builtin_type_name(cl_symbol_name(sym))) return 1;

    return 0;
}

void cl_package_export_defined_cl_symbols(void)
{
    CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(cl_package_cl);
    CL_Vector *tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
    uint32_t i;

    for (i = 0; i < tbl->length; i++) {
        CL_Obj list = tbl->data[i];
        while (!CL_NULL_P(list)) {
            CL_Obj sym = cl_car(list);
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            /* Skip already-exported (from pre-boot export) */
            if (!(s->flags & CL_SYM_EXPORTED)) {
                if (symbol_has_binding(sym))
                    s->flags |= CL_SYM_EXPORTED;
            }
            list = cl_cdr(list);
        }
    }

    /* Keywords and CLAMIGA are always fully exported */
    {
        CL_Obj pkgs[2];
        int p;
        pkgs[0] = cl_package_keyword;
        pkgs[1] = cl_package_clamiga;
        for (p = 0; p < 2; p++) {
            pkg = (CL_Package *)CL_OBJ_TO_PTR(pkgs[p]);
            tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
            for (i = 0; i < tbl->length; i++) {
                CL_Obj list = tbl->data[i];
                while (!CL_NULL_P(list)) {
                    CL_Obj sym = cl_car(list);
                    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
                    s->flags |= CL_SYM_EXPORTED;
                    list = cl_cdr(list);
                }
            }
        }
    }
}

void cl_package_init(void)
{
    if (!cl_package_rwlock)
        platform_rwlock_init(&cl_package_rwlock);

    /* Create the three standard packages */
    cl_package_cl = cl_make_package("COMMON-LISP");
    CL_GC_PROTECT(cl_package_cl);

    cl_package_keyword = cl_make_package("KEYWORD");
    CL_GC_PROTECT(cl_package_keyword);

    cl_package_cl_user = cl_make_package("COMMON-LISP-USER");
    CL_GC_PROTECT(cl_package_cl_user);

    cl_package_ext = cl_make_package("EXT");
    CL_GC_PROTECT(cl_package_ext);

    cl_package_clamiga = cl_make_package("CLAMIGA");
    CL_GC_PROTECT(cl_package_clamiga);

    cl_package_mp = cl_make_package("MP");
    CL_GC_PROTECT(cl_package_mp);

    cl_package_ffi = cl_make_package("FFI");
    CL_GC_PROTECT(cl_package_ffi);

#ifdef PLATFORM_AMIGA
    cl_package_amiga = cl_make_package("AMIGA");
    CL_GC_PROTECT(cl_package_amiga);
#endif

    /* Register in global registry */
    cl_register_package(cl_package_cl);
    cl_register_package(cl_package_keyword);
    cl_register_package(cl_package_cl_user);
    cl_register_package(cl_package_ext);
    cl_register_package(cl_package_clamiga);
    cl_register_package(cl_package_mp);
    cl_register_package(cl_package_ffi);
#ifdef PLATFORM_AMIGA
    cl_register_package(cl_package_amiga);
#endif

    /* Add nicknames */
    {
        CL_Package *cl_pkg = (CL_Package *)CL_OBJ_TO_PTR(cl_package_cl);
        CL_Obj nick = cl_make_string("CL", 2);
        cl_pkg->nicknames = cl_cons(nick, CL_NIL);
    }
    {
        CL_Package *user_pkg = (CL_Package *)CL_OBJ_TO_PTR(cl_package_cl_user);
        CL_Obj nick = cl_make_string("CL-USER", 7);
        user_pkg->nicknames = cl_cons(nick, CL_NIL);
    }

    /* CL-USER uses CL, EXT, CLAMIGA, MP, FFI, AMIGA */
    cl_use_package(cl_package_cl, cl_package_cl_user);
    cl_use_package(cl_package_ext, cl_package_cl_user);
    cl_use_package(cl_package_clamiga, cl_package_cl_user);
    cl_use_package(cl_package_mp, cl_package_cl_user);
    cl_use_package(cl_package_ffi, cl_package_cl_user);
#ifdef PLATFORM_AMIGA
    cl_use_package(cl_package_amiga, cl_package_cl_user);
#endif

    /* EXT uses CL */
    cl_use_package(cl_package_cl, cl_package_ext);

    /* MP uses CL */
    cl_use_package(cl_package_cl, cl_package_mp);

    /* FFI uses CL */
    cl_use_package(cl_package_cl, cl_package_ffi);

    /* AMIGA uses CL and FFI (Amiga only) */
#ifdef PLATFORM_AMIGA
    cl_use_package(cl_package_cl, cl_package_amiga);
    cl_use_package(cl_package_ffi, cl_package_amiga);
#endif

    /* CLAMIGA uses CL; CL uses CLAMIGA so boot.lisp/clos.lisp can
       access internal builtins without package qualification */
    cl_use_package(cl_package_cl, cl_package_clamiga);
    cl_use_package(cl_package_clamiga, cl_package_cl);

    cl_current_package = cl_package_cl_user;

    /* Pre-intern and export standard CL type names that are recognized by
       the C-level typep but have no Lisp-level function/value binding.
       Must happen early so packages that :use CL inherit them correctly. */
    {
        static const char *builtin_type_names[] = {
            "SIMPLE-VECTOR", "SIMPLE-ARRAY", "SIMPLE-BIT-VECTOR",
            "BASE-CHAR", "STANDARD-CHAR",
            "COMPILED-FUNCTION", "ATOM", "BOOLEAN",
            "FIXNUM", "BIGNUM", "BIT-VECTOR",
            "SINGLE-FLOAT", "DOUBLE-FLOAT", "SHORT-FLOAT", "LONG-FLOAT",
            NULL
        };
        const char **p;
        for (p = builtin_type_names; *p; p++) {
            CL_Obj sym = cl_intern_in(*p, (uint32_t)strlen(*p), cl_package_cl);
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            s->flags |= CL_SYM_EXPORTED;
        }
    }

    /* Note: roots are kept protected permanently (they're globals) */
#ifdef PLATFORM_AMIGA
    CL_GC_UNPROTECT(7);
#else
    CL_GC_UNPROTECT(6);
#endif

    /* Register package globals for GC compaction forwarding */
    cl_gc_register_root(&cl_package_cl);
    cl_gc_register_root(&cl_package_cl_user);
    cl_gc_register_root(&cl_package_keyword);
    cl_gc_register_root(&cl_package_ext);
    cl_gc_register_root(&cl_package_clamiga);
    cl_gc_register_root(&cl_package_mp);
    cl_gc_register_root(&cl_package_ffi);
#ifdef PLATFORM_AMIGA
    cl_gc_register_root(&cl_package_amiga);
#endif
    cl_gc_register_root(&cl_current_package);
}
