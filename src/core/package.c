#include "package.h"
#include "symbol.h"
#include "mem.h"
#include "../platform/platform.h"
#include <string.h>

CL_Obj cl_package_cl = CL_NIL;
CL_Obj cl_package_cl_user = CL_NIL;
CL_Obj cl_package_keyword = CL_NIL;
CL_Obj cl_current_package = CL_NIL;

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

CL_Obj cl_package_find_symbol(const char *name, uint32_t len, CL_Obj package)
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

    /* Search use-list */
    {
        CL_Obj uses = pkg->use_list;
        while (!CL_NULL_P(uses)) {
            CL_Obj found = cl_package_find_symbol(name, len, cl_car(uses));
            if (!CL_NULL_P(found)) return found;
            uses = cl_cdr(uses);
        }
    }

    return CL_NIL;
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

    /* CL-USER uses CL */
    {
        CL_Package *user = (CL_Package *)CL_OBJ_TO_PTR(cl_package_cl_user);
        user->use_list = cl_cons(cl_package_cl, CL_NIL);
    }

    cl_current_package = cl_package_cl_user;

    /* Note: roots are kept protected permanently (they're globals) */
    CL_GC_UNPROTECT(3);
}
