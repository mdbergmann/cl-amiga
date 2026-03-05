#include "package.h"
#include "symbol.h"
#include "mem.h"
#include "error.h"
#include "../platform/platform.h"
#include <string.h>

CL_Obj cl_package_cl = CL_NIL;
CL_Obj cl_package_cl_user = CL_NIL;
CL_Obj cl_package_keyword = CL_NIL;
CL_Obj cl_package_ext = CL_NIL;
CL_Obj cl_current_package = CL_NIL;
CL_Obj cl_package_registry = CL_NIL;

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

    /* Prepend to bucket */
    CL_GC_PROTECT(package);
    CL_GC_PROTECT(symbol);
    tbl->data[idx] = cl_cons(symbol, tbl->data[idx]);
    CL_GC_UNPROTECT(2);

    sym->package = package;
    pkg->sym_count++;
}

CL_Obj cl_package_find_external(const char *name, uint32_t len, CL_Obj package)
{
    CL_Obj sym = find_own_symbol(name, len, package);
    if (!CL_NULL_P(sym)) {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
        if (s->flags & CL_SYM_EXPORTED) {
            return sym;
        }
    }
    return CL_NIL;
}

CL_Obj cl_package_find_symbol(const char *name, uint32_t len, CL_Obj package)
{
    CL_Obj sym;

    /* Search own symbol table first */
    sym = find_own_symbol(name, len, package);
    if (!CL_NULL_P(sym)) return sym;

    /* Search use-list — only exported symbols */
    {
        CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
        CL_Obj uses = pkg->use_list;
        while (!CL_NULL_P(uses)) {
            CL_Obj found = cl_package_find_external(name, len, cl_car(uses));
            if (!CL_NULL_P(found)) return found;
            uses = cl_cdr(uses);
        }
    }

    return CL_NIL;
}

CL_Obj cl_find_symbol_with_status(const char *name, uint32_t len,
                                   CL_Obj package, int *status)
{
    CL_Obj sym;

    /* Search own symbol table first */
    sym = find_own_symbol(name, len, package);
    if (!CL_NULL_P(sym)) {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
        if (s->flags & CL_SYM_EXPORTED) {
            *status = 2; /* :EXTERNAL */
        } else {
            *status = 1; /* :INTERNAL */
        }
        return sym;
    }

    /* Search use-list — only exported symbols */
    {
        CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(package);
        CL_Obj uses = pkg->use_list;
        while (!CL_NULL_P(uses)) {
            CL_Obj found = cl_package_find_external(name, len, cl_car(uses));
            if (!CL_NULL_P(found)) {
                *status = 3; /* :INHERITED */
                return found;
            }
            uses = cl_cdr(uses);
        }
    }

    *status = 0; /* not found */
    return CL_NIL;
}

CL_Obj cl_find_package(const char *name, uint32_t len)
{
    CL_Obj reg;

    /* CDR-10: Check local nicknames of current package first */
    if (!CL_NULL_P(cl_current_package)) {
        CL_Package *cur = (CL_Package *)CL_OBJ_TO_PTR(cl_current_package);
        CL_Obj lnicks = cur->local_nicknames;
        while (!CL_NULL_P(lnicks)) {
            CL_Obj entry = cl_car(lnicks);
            if (str_eq(cl_car(entry), name, len)) {
                return cl_cdr(entry);
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
        if (str_eq(p->name, name, len)) return pkg;

        /* Check nicknames */
        {
            CL_Obj nicks = p->nicknames;
            while (!CL_NULL_P(nicks)) {
                if (str_eq(cl_car(nicks), name, len)) return pkg;
                nicks = cl_cdr(nicks);
            }
        }

        reg = cl_cdr(reg);
    }
    return CL_NIL;
}

void cl_register_package(CL_Obj pkg)
{
    CL_Package *p = (CL_Package *)CL_OBJ_TO_PTR(pkg);
    CL_Obj entry;

    CL_GC_PROTECT(pkg);
    entry = cl_cons(p->name, pkg);
    CL_GC_PROTECT(entry);
    cl_package_registry = cl_cons(entry, cl_package_registry);
    CL_GC_UNPROTECT(2);
}

void cl_export_symbol(CL_Obj sym, CL_Obj package)
{
    CL_Symbol *s;
    CL_String *sname;
    if (CL_NULL_P(sym)) return;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    sname = (CL_String *)CL_OBJ_TO_PTR(s->name);

    /* If symbol is not present in package's own table, import it first.
       Per CL spec: "If the symbol is accessible via use-package,
       it is first imported into package, after which it is exported." */
    if (CL_NULL_P(find_own_symbol(sname->data, sname->length, package))) {
        cl_import_symbol(sym, package);
    }

    s->flags |= CL_SYM_EXPORTED;
}

void cl_unexport_symbol(CL_Obj sym, CL_Obj package)
{
    CL_Symbol *s;
    (void)package;
    if (CL_NULL_P(sym)) return;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->flags &= ~CL_SYM_EXPORTED;
}

void cl_import_symbol(CL_Obj sym, CL_Obj package)
{
    CL_Symbol *s;
    CL_String *sname;
    CL_Obj existing;

    if (CL_NULL_P(sym)) return;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    sname = (CL_String *)CL_OBJ_TO_PTR(s->name);

    /* Check if already present in package */
    existing = find_own_symbol(sname->data, sname->length, package);
    if (!CL_NULL_P(existing)) {
        if (existing == sym) return; /* already imported */
        cl_error(CL_ERR_GENERAL, "IMPORT conflict: symbol already present in package");
        return;
    }

    /* Add to package's symbol table (don't change home package) */
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

void cl_shadow_symbol(const char *name, uint32_t len, CL_Obj package)
{
    CL_Obj existing = find_own_symbol(name, len, package);

    if (!CL_NULL_P(existing)) {
        /* Symbol already directly present — nothing to do */
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
}

void cl_use_package(CL_Obj pkg_to_use, CL_Obj using_pkg)
{
    CL_Package *user = (CL_Package *)CL_OBJ_TO_PTR(using_pkg);
    CL_Obj list;

    if (pkg_to_use == using_pkg) return;

    /* Check if already in use-list */
    list = user->use_list;
    while (!CL_NULL_P(list)) {
        if (cl_car(list) == pkg_to_use) return; /* already using */
        list = cl_cdr(list);
    }

    /* Add to use-list */
    CL_GC_PROTECT(using_pkg);
    CL_GC_PROTECT(pkg_to_use);
    user->use_list = cl_cons(pkg_to_use, user->use_list);
    CL_GC_UNPROTECT(2);
}

void cl_unuse_package(CL_Obj pkg_to_unuse, CL_Obj using_pkg)
{
    CL_Package *user = (CL_Package *)CL_OBJ_TO_PTR(using_pkg);
    CL_Obj prev = CL_NIL;
    CL_Obj list = user->use_list;

    while (!CL_NULL_P(list)) {
        if (cl_car(list) == pkg_to_unuse) {
            if (CL_NULL_P(prev)) {
                user->use_list = cl_cdr(list);
            } else {
                /* Mutate cdr of prev */
                CL_Cons *c = (CL_Cons *)CL_OBJ_TO_PTR(prev);
                c->cdr = cl_cdr(list);
            }
            return;
        }
        prev = list;
        list = cl_cdr(list);
    }
}

void cl_add_package_local_nickname(CL_Obj nick_str, CL_Obj target_pkg, CL_Obj in_pkg)
{
    CL_Package *p = (CL_Package *)CL_OBJ_TO_PTR(in_pkg);
    CL_String *ns = (CL_String *)CL_OBJ_TO_PTR(nick_str);
    CL_Obj list = p->local_nicknames;

    /* Check for duplicate — replace if exists */
    while (!CL_NULL_P(list)) {
        CL_Obj entry = cl_car(list);
        if (str_eq(cl_car(entry), ns->data, ns->length)) {
            /* Replace target */
            CL_Cons *c = (CL_Cons *)CL_OBJ_TO_PTR(entry);
            c->cdr = target_pkg;
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
}

void cl_remove_package_local_nickname(const char *name, uint32_t len, CL_Obj from_pkg)
{
    CL_Package *p = (CL_Package *)CL_OBJ_TO_PTR(from_pkg);
    CL_Obj prev = CL_NIL;
    CL_Obj list = p->local_nicknames;

    while (!CL_NULL_P(list)) {
        CL_Obj entry = cl_car(list);
        if (str_eq(cl_car(entry), name, len)) {
            if (CL_NULL_P(prev)) {
                p->local_nicknames = cl_cdr(list);
            } else {
                CL_Cons *c = (CL_Cons *)CL_OBJ_TO_PTR(prev);
                c->cdr = cl_cdr(list);
            }
            return;
        }
        prev = list;
        list = cl_cdr(list);
    }
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
}

void cl_package_init(void)
{
    /* Create the three standard packages */
    cl_package_cl = cl_make_package("COMMON-LISP");
    CL_GC_PROTECT(cl_package_cl);

    cl_package_keyword = cl_make_package("KEYWORD");
    CL_GC_PROTECT(cl_package_keyword);

    cl_package_cl_user = cl_make_package("COMMON-LISP-USER");
    CL_GC_PROTECT(cl_package_cl_user);

    cl_package_ext = cl_make_package("EXT");
    CL_GC_PROTECT(cl_package_ext);

    /* Register in global registry */
    cl_register_package(cl_package_cl);
    cl_register_package(cl_package_keyword);
    cl_register_package(cl_package_cl_user);
    cl_register_package(cl_package_ext);

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

    /* CL-USER uses CL and EXT */
    cl_use_package(cl_package_cl, cl_package_cl_user);
    cl_use_package(cl_package_ext, cl_package_cl_user);

    /* EXT uses CL */
    cl_use_package(cl_package_cl, cl_package_ext);

    cl_current_package = cl_package_cl_user;

    /* Note: roots are kept protected permanently (they're globals) */
    CL_GC_UNPROTECT(4);
}
