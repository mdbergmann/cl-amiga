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
CL_Obj cl_package_mop = CL_NIL;
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

/* See package.h for rationale. */
CL_Obj cl_normalize_nil_symbol(CL_Obj sym)
{
    if (sym == SYM_NIL && SYM_NIL != CL_NIL) return CL_NIL;
    return sym;
}

#define normalize_nil(sym) cl_normalize_nil_symbol(sym)

/* Search a single package's own symbol table (no use-list).
 * Returns CL_UNBOUND when not found — distinct from CL_NIL because the
 * symbol NIL itself is interned in COMMON-LISP, so a CL_NIL return would
 * be ambiguous between "found NIL" and "missing".
 *
 * Internal callers receive the raw heap symbol (so identity comparisons
 * against the exported_symbols / shadowing_symbols lists keep working);
 * the public boundary normalizes SYM_NIL to CL_NIL. */
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
    return CL_UNBOUND;
}

/* ---- nolock helpers for internal use under existing lock ---- */

/* Per-package exported-symbols list membership check.  Each package
 * tracks its own export set so a symbol can be EXTERNAL in its home
 * and INTERNAL in a using/importing package (CLHS 11.1).  Holders must
 * already own cl_package_rwlock for read or write. */
static int exported_p_nolock(CL_Obj sym, CL_Obj package)
{
    CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
    CL_Obj list = pkg->exported_symbols;
    while (!CL_NULL_P(list)) {
        if (cl_car(list) == sym) return 1;
        list = cl_cdr(list);
    }
    return 0;
}

/* Returns CL_UNBOUND when the name is not exported from package
 * (or not present at all).  See find_own_symbol comment about NIL.
 * Internal use only — does not normalize SYM_NIL. */
static CL_Obj find_external_nolock(const char *name, uint32_t len, CL_Obj package)
{
    CL_Obj sym = find_own_symbol(name, len, package);
    if (sym != CL_UNBOUND && exported_p_nolock(sym, package))
        return sym;
    return CL_UNBOUND;
}

int cl_symbol_external_p(CL_Obj sym, CL_Obj package)
{
    int r;
    if (CL_NULL_P(sym) || CL_NULL_P(package)) return 0;
    if (cl_package_rwlock) platform_rwlock_rdlock(cl_package_rwlock);
    r = exported_p_nolock(sym, package);
    if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
    return r;
}

/* Returns CL_UNBOUND when name is not present in package or its use-list. */
CL_Obj cl_package_find_symbol_nolock(const char *name, uint32_t len, CL_Obj package)
{
    CL_Obj sym;
    sym = find_own_symbol(name, len, package);
    if (sym != CL_UNBOUND) return sym;
    {
        CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
        CL_Obj uses = pkg->use_list;
        while (!CL_NULL_P(uses)) {
            CL_Obj found = find_external_nolock(name, len, cl_car(uses));
            if (found != CL_UNBOUND) return found;
            uses = cl_cdr(uses);
        }
    }
    return CL_UNBOUND;
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
    if (existing != CL_UNBOUND) {
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
    pkg->exported_symbols = CL_NIL;
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

/* Public API: returns CL_NIL when not found (preserved historical contract
 * for C callers that use CL_NULL_P-as-not-found).  Callers that need to
 * distinguish "found NIL" from "missing" should use
 * cl_find_symbol_with_status. */
CL_Obj cl_package_find_external(const char *name, uint32_t len, CL_Obj package)
{
    CL_Obj result;
    if (cl_package_rwlock) platform_rwlock_rdlock(cl_package_rwlock);
    result = find_external_nolock(name, len, package);
    if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
    if (result == CL_UNBOUND) return CL_NIL;
    return normalize_nil(result);
}

CL_Obj cl_package_find_symbol(const char *name, uint32_t len, CL_Obj package)
{
    CL_Obj result;
    if (cl_package_rwlock) platform_rwlock_rdlock(cl_package_rwlock);
    result = cl_package_find_symbol_nolock(name, len, package);
    if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
    if (result == CL_UNBOUND) return CL_NIL;
    return normalize_nil(result);
}

CL_Obj cl_find_symbol_with_status(const char *name, uint32_t len,
                                   CL_Obj package, int *status)
{
    CL_Obj sym;

    if (cl_package_rwlock) platform_rwlock_rdlock(cl_package_rwlock);

    /* Search own symbol table first */
    sym = find_own_symbol(name, len, package);
    if (sym != CL_UNBOUND) {
        if (exported_p_nolock(sym, package)) {
            *status = 2; /* :EXTERNAL */
        } else {
            *status = 1; /* :INTERNAL */
        }
        if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
        return normalize_nil(sym);
    }

    /* Search use-list — only exported symbols */
    {
        CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
        CL_Obj uses = pkg->use_list;
        while (!CL_NULL_P(uses)) {
            CL_Obj found = find_external_nolock(name, len, cl_car(uses));
            if (found != CL_UNBOUND) {
                *status = 3; /* :INHERITED */
                if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
                return normalize_nil(found);
            }
            uses = cl_cdr(uses);
        }
    }

    *status = 0; /* not found */
    if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
    return CL_NIL;
}

CL_Obj cl_find_package(const char *name, uint32_t len)
{
    CL_Obj reg;
    CL_Obj result = CL_NIL;

    if (cl_package_rwlock) platform_rwlock_rdlock(cl_package_rwlock);

    /* CDR-10: Check local nicknames of current package first */
    if (!CL_NULL_P(cl_current_package)) {
        CL_Package *cur = (CL_Package *)CL_OBJ_TO_PTR(cl_current_package);
        CL_Obj lnicks = cur->local_nicknames;
        while (!CL_NULL_P(lnicks)) {
            CL_Obj entry = cl_car(lnicks);
            if (str_eq(cl_car(entry), name, len)) {
                result = cl_cdr(entry);
                if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
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
            if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
            return pkg;
        }

        /* Check nicknames */
        {
            CL_Obj nicks = p->nicknames;
            while (!CL_NULL_P(nicks)) {
                if (str_eq(cl_car(nicks), name, len)) {
                    if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
                    return pkg;
                }
                nicks = cl_cdr(nicks);
            }
        }

        reg = cl_cdr(reg);
    }
    if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
    return CL_NIL;
}

void cl_register_package(CL_Obj pkg)
{
    CL_Package *p = (CL_Package *)CL_OBJ_TO_PTR(pkg);
    CL_Obj entry;

    CL_GC_PROTECT(pkg);
    entry = cl_cons(p->name, pkg);
    CL_GC_PROTECT(entry);
    if (cl_package_rwlock) platform_rwlock_wrlock(cl_package_rwlock);
    cl_package_registry = cl_cons(entry, cl_package_registry);
    if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
    CL_GC_UNPROTECT(2);
}

void cl_export_symbol(CL_Obj sym, CL_Obj package)
{
    CL_Symbol *s;
    CL_String *sname;
    if (CL_NULL_P(sym)) return;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    sname = (CL_String *)CL_OBJ_TO_PTR(s->name);

    if (cl_package_rwlock) platform_rwlock_wrlock(cl_package_rwlock);

    /* If symbol is not present in package's own table, import it first.
       Per CL spec: "If the symbol is accessible via use-package,
       it is first imported into package, after which it is exported." */
    if (find_own_symbol(sname->data, sname->length, package) == CL_UNBOUND) {
        import_symbol_nolock(sym, package);
    }

    /* Add to package's exported list (idempotent). */
    if (!exported_p_nolock(sym, package)) {
        CL_Package *pkg;
        CL_GC_PROTECT(package);
        CL_GC_PROTECT(sym);
        pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
        pkg->exported_symbols = cl_cons(sym, pkg->exported_symbols);
        CL_GC_UNPROTECT(2);
    }
    /* Keep the legacy global flag in sync (used by printer / describe
     * fast paths and by FASL loader as a "any package exports this"
     * heuristic).  Source of truth for find-symbol is the per-package
     * list above. */
    s->flags |= CL_SYM_EXPORTED;
    if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
}

/* Unexport SYM from PACKAGE.  Removes SYM from PACKAGE's exported list
 * (the per-package source of truth).  The legacy CL_SYM_EXPORTED flag is
 * cleared only when the symbol's home package is PACKAGE — keeping the
 * "any package considers this symbol exported" hint useful for printer
 * and describe fast paths. */
void cl_unexport_symbol(CL_Obj sym, CL_Obj package)
{
    CL_Symbol *s;
    CL_Package *pkg;
    CL_Obj prev = CL_NIL;
    CL_Obj list;
    if (CL_NULL_P(sym)) return;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    if (cl_package_rwlock) platform_rwlock_wrlock(cl_package_rwlock);
    pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
    list = pkg->exported_symbols;
    while (!CL_NULL_P(list)) {
        if (cl_car(list) == sym) {
            CL_Obj next = cl_cdr(list);
            if (CL_NULL_P(prev)) pkg->exported_symbols = next;
            else ((CL_Cons *)CL_OBJ_TO_PTR(prev))->cdr = next;
            break;
        }
        prev = list;
        list = cl_cdr(list);
    }
    if (s->package == package)
        s->flags &= ~CL_SYM_EXPORTED;
    if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
}

void cl_import_symbol(CL_Obj sym, CL_Obj package)
{
    if (CL_NULL_P(sym)) return;
    if (cl_package_rwlock) platform_rwlock_wrlock(cl_package_rwlock);
    import_symbol_nolock(sym, package);
    if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
}

void cl_shadow_symbol(const char *name, uint32_t len, CL_Obj package)
{
    CL_Obj existing;

    if (cl_package_rwlock) platform_rwlock_wrlock(cl_package_rwlock);
    existing = find_own_symbol(name, len, package);

    if (existing != CL_UNBOUND) {
        /* Symbol already directly present — nothing to do */
        if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
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
    if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
}

void cl_use_package(CL_Obj pkg_to_use, CL_Obj using_pkg)
{
    CL_Package *user;
    CL_Obj list;

    if (pkg_to_use == using_pkg) return;

    if (cl_package_rwlock) platform_rwlock_wrlock(cl_package_rwlock);
    user = (CL_Package *)CL_OBJ_TO_PTR(using_pkg);

    /* Check if already in use-list */
    list = user->use_list;
    while (!CL_NULL_P(list)) {
        if (cl_car(list) == pkg_to_use) {
            if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
            return; /* already using */
        }
        list = cl_cdr(list);
    }

    /* Add to use-list */
    CL_GC_PROTECT(using_pkg);
    CL_GC_PROTECT(pkg_to_use);
    user->use_list = cl_cons(pkg_to_use, user->use_list);
    CL_GC_UNPROTECT(2);
    if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
}

void cl_unuse_package(CL_Obj pkg_to_unuse, CL_Obj using_pkg)
{
    CL_Package *user;
    CL_Obj prev, list;

    if (cl_package_rwlock) platform_rwlock_wrlock(cl_package_rwlock);
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
            if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
            return;
        }
        prev = list;
        list = cl_cdr(list);
    }
    if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
}

void cl_add_package_local_nickname(CL_Obj nick_str, CL_Obj target_pkg, CL_Obj in_pkg)
{
    CL_Package *p;
    CL_String *ns;
    CL_Obj list;

    if (cl_package_rwlock) platform_rwlock_wrlock(cl_package_rwlock);
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
            if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
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
    if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
}

void cl_remove_package_local_nickname(const char *name, uint32_t len, CL_Obj from_pkg)
{
    CL_Package *p;
    CL_Obj prev, list;

    if (cl_package_rwlock) platform_rwlock_wrlock(cl_package_rwlock);
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
            if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
            return;
        }
        prev = list;
        list = cl_cdr(list);
    }
    if (cl_package_rwlock) platform_rwlock_unlock(cl_package_rwlock);
}

/* Export every symbol presently in PACKAGE's own symbol table.
 * Sets the legacy CL_SYM_EXPORTED flag and pushes each symbol onto
 * pkg->exported_symbols.
 *
 * GC compaction during cl_cons can relocate the package, the symbol
 * table vector, and the chain conses, so we re-read the chain pointer
 * for each iteration and use a counted advance to make progress. */
static void export_all_present_symbols(CL_Obj package)
{
    uint32_t i, table_len;
    CL_GC_PROTECT(package);
    {
        CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
        CL_Vector *tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
        table_len = tbl->length;
    }
    for (i = 0; i < table_len; i++) {
        uint32_t consumed = 0;
        for (;;) {
            CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
            CL_Vector *tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
            CL_Obj list = tbl->data[i];
            uint32_t skip = consumed;
            CL_Obj sym;
            CL_Symbol *s;
            while (skip-- > 0 && !CL_NULL_P(list)) list = cl_cdr(list);
            if (CL_NULL_P(list)) break;
            sym = cl_car(list);
            s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            if (!(s->flags & CL_SYM_EXPORTED))
                s->flags |= CL_SYM_EXPORTED;
            if (!exported_p_nolock(sym, package)) {
                CL_Obj cell;
                CL_GC_PROTECT(sym);
                cell = cl_cons(sym, CL_NIL);
                CL_GC_UNPROTECT(1);
                pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
                ((CL_Cons *)CL_OBJ_TO_PTR(cell))->cdr = pkg->exported_symbols;
                pkg->exported_symbols = cell;
            }
            consumed++;
        }
    }
    CL_GC_UNPROTECT(1);
}

void cl_package_export_all_cl_symbols(void)
{
    export_all_present_symbols(cl_package_cl);
    export_all_present_symbols(cl_package_keyword);
    export_all_present_symbols(cl_package_clamiga);
    export_all_present_symbols(cl_package_mop);
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
    /* Walk CL package, exporting each symbol with a real binding (or
     * already pre-flagged from boot).  Pushes onto cl_package_cl's
     * exported_symbols list so find-symbol returns :EXTERNAL.
     * Counted-advance pattern handles cl_cons-induced GC compaction. */
    uint32_t i, table_len;
    CL_Obj package = cl_package_cl;
    CL_GC_PROTECT(package);
    {
        CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
        CL_Vector *tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
        table_len = tbl->length;
    }
    for (i = 0; i < table_len; i++) {
        uint32_t consumed = 0;
        for (;;) {
            CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
            CL_Vector *tbl = (CL_Vector *)CL_OBJ_TO_PTR(pkg->symbols);
            CL_Obj list = tbl->data[i];
            uint32_t skip = consumed;
            CL_Obj sym;
            CL_Symbol *s;
            int should_export;
            while (skip-- > 0 && !CL_NULL_P(list)) list = cl_cdr(list);
            if (CL_NULL_P(list)) break;
            sym = cl_car(list);
            s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            /* Don't use the global CL_SYM_EXPORTED flag as a heuristic
             * here.  The flag is set whenever ANY package exports the
             * symbol, so a defpackage in user code that does
             * (:export #:body) would flip the flag on the same BODY
             * symbol that boot.lisp accidentally interned in CL —
             * which would then propagate through this discovery pass
             * and re-add BODY to COMMON-LISP's exported set, breaking
             * the ANSI NO-EXTRA-SYMBOLS test.  Restrict to actual
             * Lisp-level bindings: function, value, macro, type,
             * struct, or CLOS class. */
            should_export = symbol_has_binding(sym);
            if (should_export) {
                if (!(s->flags & CL_SYM_EXPORTED))
                    s->flags |= CL_SYM_EXPORTED;
                if (!exported_p_nolock(sym, package)) {
                    CL_Obj cell;
                    CL_GC_PROTECT(sym);
                    cell = cl_cons(sym, CL_NIL);
                    CL_GC_UNPROTECT(1);
                    pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
                    ((CL_Cons *)CL_OBJ_TO_PTR(cell))->cdr = pkg->exported_symbols;
                    pkg->exported_symbols = cell;
                }
            }
            consumed++;
        }
    }
    CL_GC_UNPROTECT(1);

    /* Keywords, CLAMIGA, and MOP are always fully exported */
    export_all_present_symbols(cl_package_keyword);
    export_all_present_symbols(cl_package_clamiga);
    export_all_present_symbols(cl_package_mop);
}

/* Internal helpers defined later by boot.lisp and clos.lisp.  Pre-interning
 * these in CLAMIGA before those files load means the reader resolves bare
 * %FOO via CLAMIGA's exports (CL and CL-USER both :use CLAMIGA), so the
 * defun creates the function on the CLAMIGA symbol rather than interning
 * a fresh CL::%FOO.  Keeping them out of CL is what makes the ANSI
 * NO-EXTRA-SYMBOLS-EXPORTED-FROM-COMMON-LISP test pass.
 *
 * Names that ARE registered with cl_package_clamiga from C (in builtins.c,
 * builtins_io.c, compiler.c etc.) are intentionally omitted — those slots
 * are already filled. */
static const char *const clos_internal_names[] = {
    "%ADD-METHOD-TO-GF", "%ASSIGN-SLOT-LOCATIONS",
    "%BOA-PATCH-DEFAULTS",
    "%BUILD-EFFECTIVE-METHOD", "%BUILD-LONG-EFFECTIVE-METHOD",
    "%BUILD-SHORT-EFFECTIVE-METHOD", "%BUILD-SLOT-INDEX-TABLE",
    "%C3-MERGE", "%CALL-METHOD-IMPL", "%CALL-WITH-METHOD-COMBINATION",
    "%CLASS-OF", "%CLONE-METHOD-COMBINATION",
    "%COMPUTE-APPLICABLE-METHODS",
    "%COMPUTE-BUILTIN-CPL", "%COMPUTE-CLASS-PRECEDENCE-LIST",
    "%COMPUTE-DEFAULT-INITARGS-DEFAULT",
    "%COMPUTE-EFFECTIVE-SLOT-DEFINITION-DEFAULT",
    "%COMPUTE-EFFECTIVE-SLOTS",
    "%COMPUTE-EQL-VALUE-SETS", "%COMPUTE-GF-CACHEABLE-P",
    "%COMPUTE-SLOTS-DEFAULT",
    "%COPY-STRUCT",
    "%DEFINE-LONG-METHOD-COMBINATION",
    "%DEFINE-SHORT-METHOD-COMBINATION",
    "%DEFSTRUCT-PARSE-SLOT",
    "%DIRECT-TO-EFFECTIVE", "%DISPATCH-BUILD-EMF",
    "%EFFECTIVE-SLOT-DEF-P",
    "%ENSURE-CLASS", "%ENSURE-DIRS-HELPER",
    "%EXPAND-EXTENDED-LOOP", "%EXPAND-SIMPLE-LOOP",
    "%FILTER-METHODS-BY-SPEC",
    "%FINALIZE-INHERITANCE-BODY",
    "%FIND-INHERITED-CLASS-SLOT-CELL",
    "%FIND-INITARG-VALUE", "%FIND-SLOT-DEF",
    "%FIND-STRUCT-SLOT-INDEX",
    "%FOLD-PARENT-INTO-EFFECTIVE",
    "%GET-DEFSETF-SETTER",
    "%GF-DISPATCH", "%GF-DISPATCH-CACHED",
    "%GF-DISPATCH-EQL", "%GF-OR-NAME",
    "%HASH-TABLE-PAIRS",
    "%INITARG-TO-SLOT-INDEX",
    "%INSTALL-METHOD-IN-GF",
    "%INVALIDATE-ALL-GF-CACHES",
    "%LOOP-ACCUM-BODY", "%LOOP-ACCUM-KEYWORD-P",
    "%LOOP-DESTRUCTURE-ASSIGNS", "%LOOP-DESTRUCTURE-BINDINGS",
    "%LOOP-DESTRUCTURE-VARS",
    "%LOOP-KEYWORD-P", "%LOOP-KEYWORD-SYM-P",
    "%LOOP-PARSE-COND-SUBCLAUSE", "%LOOP-PARSE-FOR",
    "%MAKE-AROUND-CHAIN",
    "%MAKE-BOOTSTRAP-CLASS",
    "%MAKE-DIRECT-SLOT-DEF", "%MAKE-EFFECTIVE-SLOT-DEF",
    "%MAKE-METHOD-CHAIN", "%MAKE-RECURSIVE-LOCK",
    "%MAKE-STRUCT",
    "%MATCH-QUALIFIER-PATTERN",
    "%METHOD-APPLICABLE-P", "%METHOD-COMBINATION-KEY",
    "%METHOD-MORE-SPECIFIC-P",
    "%NOTIFY-DEPENDENTS",
    "%PACKAGE-EXTERNAL-SYMBOLS", "%PACKAGE-SYMBOLS",
    "%PARSE-SLOT-SPEC", "%PARSE-SPECIALIZED-LAMBDA-LIST",
    "%PLACE-DIRECT-MUTATOR-P", "%PLACE-DIRECT-MUTATORS",
    "%REGISTER-CONDITION-CLASS", "%REGISTER-CONDITION-TYPE",
    "%REGISTER-SETF-EXPANDER", "%REGISTER-SETF-FUNCTION",
    "%REGISTER-STANDARD-COMBINATION",
    "%REGISTER-STRUCT-TYPE",
    "%REMF",
    "%RESOLVE-METHOD-COMBINATION", "%RESOLVE-SPECIALIZERS",
    "%SYMBOL-MACRO-EXPANSION",
    "%SET-CLASS-CPL",
    "%SET-CLASS-DEFAULT-INITARGS",
    "%SET-CLASS-DIRECT-DEFAULT-INITARGS",
    "%SET-CLASS-DIRECT-METHODS",
    "%SET-CLASS-DIRECT-SLOTS",
    "%SET-CLASS-DIRECT-SUBCLASSES",
    "%SET-CLASS-EFFECTIVE-SLOTS",
    "%SET-CLASS-FINALIZED-P",
    "%SET-CLASS-SLOT-INDEX-TABLE",
    "%SET-CLOS-CLASS-TABLE",
    "%SET-CONDITION-DEFAULT-INITARGS",
    "%SET-CONDITION-SLOT-VALUE",
    "%SET-DOCUMENTATION", "%SET-FIND-CLASS",
    "%SET-GF-CACHEABLE-P", "%SET-GF-DISCRIMINATING-FUNCTION",
    "%SET-GF-DISPATCH-CACHE", "%SET-GF-EQL-VALUE-SETS",
    "%SET-GF-METHOD-COMBINATION", "%SET-GF-METHODS",
    "%SET-MEMBER",
    "%SET-METHOD-GENERIC-FUNCTION",
    "%SET-SLOT-DEFINITION-DOCUMENTATION",
    "%SET-SLOT-DEFINITION-INITARGS",
    "%SET-SLOT-DEFINITION-INITFORM",
    "%SET-SLOT-DEFINITION-INITFUNCTION",
    "%SET-SLOT-DEFINITION-LOCATION",
    "%SET-SLOT-DEFINITION-TYPE",
    "%SET-SLOT-VALUE",
    "%SET-STANDARD-INSTANCE-ACCESS",
    "%SETF-ELT", "%SETF-READTABLE-CASE",
    "%SHORT-COMBINE",
    "%SLOT-ACCESS-PROTOCOL-GF-P",
    "%SLOT-SPEC->DIRECT-DEF-FORM",
    "%SLOT-SPEC-ALL-OPTIONS", "%SLOT-SPEC-HAS-OPTION-P",
    "%SLOT-SPEC-NAME", "%SLOT-SPEC-OPTION",
    "%STRUCT-CHANGE-CLASS",
    "%STRUCT-REF", "%STRUCT-SET",
    "%STRUCT-SLOT-NAMES", "%STRUCT-SLOT-SPECS",
    "%STRUCT-TYPE-NAME",
    "%SUBCLASSP", "%SUBST-IT",
    "%SYMBOL-CONSTANT-P",
    "%UNINSTALL-METHOD-FROM-GF",
    /* CL-Amiga internal CLOS state and helpers (non-MOP).  These are
     * implementation details that boot.lisp / clos.lisp introduce while
     * loading in the CL package; pre-interning them here keeps them in
     * CLAMIGA so they don't pollute COMMON-LISP's external symbols. */
    "*CALL-NEXT-METHOD-ARGS*", "*CALL-NEXT-METHOD-FUNCTION*",
    "*CLASS-TABLE*", "*CURRENT-METHOD-ARGS*",
    "*DOCUMENTATION-TABLE*", "*EQL-SPECIALIZER-TABLE*",
    "*GENERIC-FUNCTION-TABLE*", "*METAOBJECT-DEPENDENTS*",
    "*METHOD-COMBINATIONS*", "*NEXT-METHOD-P-FUNCTION*",
    /* *PRINT-OBJECT-HOOK* is interned directly in CLAMIGA by symbol.c. */
    "*SLOT-ACCESS-PROTOCOL-EXTENDED-P*",
    "*SLOT-UNBOUND-MARKER*",
    /* BODY and TEST are too generic to safely pre-intern in CLAMIGA —
     * doing so would alias user-code symbols of the same name through
     * the CL :use CLAMIGA inheritance chain.  They leak from
     * boot.lisp / clos.lisp macro expansions and need a source-level
     * fix (e.g. wrapping the offending macros with GENSYM).
     * CONDITIONP / CONDITION-TYPE-NAME / CONDITION-SLOT-VALUE are
     * registered as CLAMIGA builtins by builtins_condition.c. */
    "GF-CACHEABLE-P", "GF-DISCRIMINATING-FUNCTION",
    "GF-DISPATCH-CACHE", "GF-EQL-VALUE-SETS",
    "GF-LAMBDA-LIST", "GF-METHOD-COMBINATION",
    "GF-METHODS", "GF-NAME",
    "METHOD-COMBINATION-NAME", "METHOD-COMBINATION-OPTIONS",
    "METHOD-COMBINATION-TYPE",
    /* NAMED-LAMBDA, QUASIQUOTE, UNQUOTE, UNQUOTE-SPLICING are interned
     * directly in CLAMIGA by symbol.c (cl_symbol_init) — listed there
     * to avoid duplication. */
    /* STRUCTUREP, ADD/REMOVE-PACKAGE-LOCAL-NICKNAME, PACKAGE-LOCAL-NICKNAMES
     * are registered as CLAMIGA builtins by builtins_struct.c /
     * builtins_package.c. */
    NULL
};

/* CLOS Metaobject Protocol API names (AMOP / closer-mop).  These are
 * defined by clos.lisp while it loads in the CL package; pre-interning
 * them in MOP and having CL :use MOP makes the reader resolve bare
 * MOP-symbol references to the inherited MOP symbol so defun/defmethod
 * bind on the MOP symbol. */
static const char *const mop_api_names[] = {
    "ACCESSOR-METHOD-SLOT-DEFINITION",
    "ADD-DEPENDENT", "ADD-DIRECT-METHOD", "ADD-DIRECT-SUBCLASS",
    "CLASS-DEFAULT-INITARGS",
    "CLASS-DIRECT-DEFAULT-INITARGS",
    "CLASS-DIRECT-METHODS", "CLASS-DIRECT-SLOTS",
    "CLASS-DIRECT-SUBCLASSES", "CLASS-DIRECT-SUPERCLASSES",
    "CLASS-EFFECTIVE-SLOTS",
    "CLASS-FINALIZED-P", "CLASS-PRECEDENCE-LIST",
    "CLASS-PROTOTYPE",
    "CLASS-SLOT-INDEX-TABLE",
    "CLASS-SLOTS", "CLASSP",
    "COMPUTE-APPLICABLE-METHODS-USING-CLASSES",
    "COMPUTE-CLASS-PRECEDENCE-LIST",
    "COMPUTE-DEFAULT-INITARGS",
    "COMPUTE-DISCRIMINATING-FUNCTION",
    "COMPUTE-EFFECTIVE-METHOD",
    "COMPUTE-EFFECTIVE-SLOT-DEFINITION",
    "COMPUTE-SLOTS",
    "DIRECT-SLOT-DEFINITION", "DIRECT-SLOT-DEFINITION-CLASS",
    "EFFECTIVE-SLOT-DEFINITION", "EFFECTIVE-SLOT-DEFINITION-CLASS",
    "ENSURE-CLASS", "ENSURE-CLASS-USING-CLASS",
    "ENSURE-GENERIC-FUNCTION-USING-CLASS", "ENSURE-METHOD",
    "EQL-SPECIALIZER", "EQL-SPECIALIZER-OBJECT", "EQL-SPECIALIZER-P",
    "EXTRACT-LAMBDA-LIST", "EXTRACT-SPECIALIZER-NAMES",
    "FINALIZE-INHERITANCE",
    "FIND-METHOD-COMBINATION",
    "FORWARD-REFERENCED-CLASS",
    "FUNCALLABLE-STANDARD-CLASS",
    "FUNCALLABLE-STANDARD-INSTANCE-ACCESS",
    "FUNCALLABLE-STANDARD-OBJECT",
    "GENERIC-FUNCTION-ARGUMENT-PRECEDENCE-ORDER",
    "GENERIC-FUNCTION-DECLARATIONS", "GENERIC-FUNCTION-LAMBDA-LIST",
    "GENERIC-FUNCTION-METHOD-CLASS",
    "GENERIC-FUNCTION-METHOD-COMBINATION",
    "GENERIC-FUNCTION-METHODS", "GENERIC-FUNCTION-NAME",
    "INTERN-EQL-SPECIALIZER",
    "MAKE-METHOD-LAMBDA", "MAP-DEPENDENTS",
    "METAOBJECT",
    "METHOD-FUNCTION", "METHOD-GENERIC-FUNCTION",
    "METHOD-LAMBDA-LIST", "METHOD-SPECIALIZERS",
    "READER-METHOD-CLASS",
    "REMOVE-DEPENDENT", "REMOVE-DIRECT-METHOD",
    "REMOVE-DIRECT-SUBCLASS",
    "SET-FUNCALLABLE-INSTANCE-FUNCTION",
    "SLOT-BOUNDP-USING-CLASS",
    "SLOT-DEFINITION",
    "SLOT-DEFINITION-ALLOCATION", "SLOT-DEFINITION-DOCUMENTATION",
    "SLOT-DEFINITION-INITARGS", "SLOT-DEFINITION-INITFORM",
    "SLOT-DEFINITION-INITFUNCTION", "SLOT-DEFINITION-LOCATION",
    "SLOT-DEFINITION-NAME", "SLOT-DEFINITION-READERS",
    "SLOT-DEFINITION-TYPE", "SLOT-DEFINITION-WRITERS",
    "SLOT-MAKUNBOUND-USING-CLASS",
    "SLOT-VALUE-USING-CLASS",
    "SPECIALIZER",
    "SPECIALIZER-DIRECT-GENERIC-FUNCTIONS",
    "SPECIALIZER-DIRECT-METHODS",
    "STANDARD-ACCESSOR-METHOD",
    "STANDARD-DIRECT-SLOT-DEFINITION",
    "STANDARD-EFFECTIVE-SLOT-DEFINITION",
    "STANDARD-INSTANCE-ACCESS",
    "STANDARD-METHOD-COMBINATION",
    "STANDARD-READER-METHOD", "STANDARD-SLOT-DEFINITION",
    "STANDARD-WRITER-METHOD",
    "UPDATE-DEPENDENT", "VALIDATE-SUPERCLASS",
    "WRITER-METHOD-CLASS",
    NULL
};

void cl_intern_clos_internals_in_clamiga(void)
{
    const char *const *p;
    for (p = clos_internal_names; *p != NULL; p++) {
        (void)cl_intern_in(*p, (uint32_t)strlen(*p), cl_package_clamiga);
    }
    for (p = mop_api_names; *p != NULL; p++) {
        (void)cl_intern_in(*p, (uint32_t)strlen(*p), cl_package_mop);
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

    cl_package_mop = cl_make_package("MOP");
    CL_GC_PROTECT(cl_package_mop);

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
    cl_register_package(cl_package_mop);
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

    /* CL-USER uses CL, EXT, CLAMIGA, MOP, MP, FFI, AMIGA */
    cl_use_package(cl_package_cl, cl_package_cl_user);
    cl_use_package(cl_package_ext, cl_package_cl_user);
    cl_use_package(cl_package_clamiga, cl_package_cl_user);
    cl_use_package(cl_package_mop, cl_package_cl_user);
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
       access internal builtins without package qualification.  Same
       trick for MOP — clos.lisp defines MOP API symbols expecting them
       to live in MOP, but the file itself loads in CL; CL :use MOP
       makes the reader resolve unqualified MOP names to the inherited
       MOP symbol so defun/defmethod bind on the MOP symbol. */
    cl_use_package(cl_package_cl, cl_package_clamiga);
    cl_use_package(cl_package_clamiga, cl_package_cl);
    cl_use_package(cl_package_mop, cl_package_cl);
    cl_use_package(cl_package_cl, cl_package_mop);

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
            cl_export_symbol(sym, cl_package_cl);
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
    cl_gc_register_root(&cl_package_mop);
    cl_gc_register_root(&cl_package_mp);
    cl_gc_register_root(&cl_package_ffi);
#ifdef PLATFORM_AMIGA
    cl_gc_register_root(&cl_package_amiga);
#endif
    cl_gc_register_root(&cl_current_package);
}
