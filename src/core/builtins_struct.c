/*
 * builtins_struct.c — Structure type infrastructure
 *
 * Provides struct type registry, constructors, accessors, copier,
 * and type predicates for defstruct.
 *
 * Registry format (struct_table alist):
 *   ((name n-slots parent (slot-names...)) ...)
 */

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

/* Struct type registry:
 * alist of (name n-slots parent (slot-names...)) */
static CL_Obj struct_table = CL_NIL;

/* --- Registry lookup helpers --- */

/* Find registry entry for a struct type name.
 * Returns the entry (name n-slots parent (slot-names...)) or NIL. */
static CL_Obj find_struct_entry(CL_Obj type_name)
{
    CL_Obj list = struct_table;
    while (!CL_NULL_P(list)) {
        CL_Obj entry = cl_car(list);
        if (cl_car(entry) == type_name)
            return entry;
        list = cl_cdr(list);
    }
    return CL_NIL;
}

/* --- Public C API for printer and typep integration --- */

/* Get slot specs list for a struct type.
 * Returns list of ((name default) ...), or NIL.
 * entry = (name n-slots parent ((slot-name default) ...)) */
static CL_Obj get_slot_specs(CL_Obj type_name)
{
    CL_Obj entry = find_struct_entry(type_name);
    if (CL_NULL_P(entry)) return CL_NIL;
    return cl_car(cl_cdr(cl_cdr(cl_cdr(entry))));  /* cadddr */
}

/* Get slot names list for a struct type (for printer).
 * Extracts just the names from slot specs.
 * Returns list of slot name symbols, or NIL. */
CL_Obj cl_struct_slot_names(CL_Obj type_name)
{
    CL_Obj specs = get_slot_specs(type_name);
    CL_Obj result = CL_NIL;
    CL_Obj tail = CL_NIL;

    /* Extract car of each spec pair */
    while (!CL_NULL_P(specs)) {
        CL_Obj spec = cl_car(specs);
        CL_Obj name = CL_CONS_P(spec) ? cl_car(spec) : spec;
        CL_Obj cell = cl_cons(name, CL_NIL);
        if (CL_NULL_P(result)) {
            result = cell;
        } else {
            ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
        }
        tail = cell;
        specs = cl_cdr(specs);
    }
    return result;
}

/* Check if a symbol is a registered struct type. */
int cl_is_struct_type(CL_Obj type_sym)
{
    return !CL_NULL_P(find_struct_entry(type_sym));
}

/* Check if obj_type is a subtype of (or equal to) test_type.
 * Walks the parent chain in the struct registry. */
int cl_struct_type_matches(CL_Obj obj_type, CL_Obj test_type)
{
    CL_Obj entry;

    if (obj_type == test_type)
        return 1;

    /* Walk parent chain */
    entry = find_struct_entry(obj_type);
    if (CL_NULL_P(entry)) return 0;

    /* entry = (name n-slots parent (slot-names...)) */
    {
        CL_Obj parent = cl_car(cl_cdr(cl_cdr(entry)));  /* caddr */
        if (CL_NULL_P(parent)) return 0;
        return cl_struct_type_matches(parent, test_type);
    }
}

/* --- Builtins --- */

/* (%register-struct-type name n-slots parent slot-names) */
static CL_Obj bi_register_struct_type(CL_Obj *args, int n)
{
    CL_Obj name = args[0];
    CL_Obj n_slots_obj = args[1];
    CL_Obj parent = args[2];
    CL_Obj slot_names = args[3];
    CL_Obj entry;
    CL_UNUSED(n);

    if (!CL_SYMBOL_P(name))
        cl_error(CL_ERR_TYPE, "%%REGISTER-STRUCT-TYPE: name must be a symbol");
    if (!CL_FIXNUM_P(n_slots_obj))
        cl_error(CL_ERR_TYPE, "%%REGISTER-STRUCT-TYPE: n-slots must be a fixnum");

    /* Build entry: (name n-slots parent (slot-names...)) */
    CL_GC_PROTECT(name);
    CL_GC_PROTECT(parent);
    CL_GC_PROTECT(slot_names);

    entry = cl_cons(slot_names, CL_NIL);
    entry = cl_cons(parent, entry);
    entry = cl_cons(n_slots_obj, entry);
    entry = cl_cons(name, entry);
    struct_table = cl_cons(entry, struct_table);

    CL_GC_UNPROTECT(3);
    return name;
}

/* (%make-struct name slot-val...) */
static CL_Obj bi_make_struct(CL_Obj *args, int n)
{
    CL_Obj name = args[0];
    int n_slots = n - 1;
    CL_Obj obj;
    CL_Struct *st;
    int i;

    if (!CL_SYMBOL_P(name))
        cl_error(CL_ERR_TYPE, "%%MAKE-STRUCT: name must be a symbol");

    obj = cl_make_struct(name, (uint32_t)n_slots);
    st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
    for (i = 0; i < n_slots; i++)
        st->slots[i] = args[i + 1];

    return obj;
}

/* (%struct-ref obj index) */
static CL_Obj bi_struct_ref(CL_Obj *args, int n)
{
    CL_Obj obj = args[0];
    int idx;
    CL_Struct *st;
    CL_UNUSED(n);

    if (!CL_STRUCT_P(obj))
        cl_error(CL_ERR_TYPE, "%%STRUCT-REF: not a structure");
    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "%%STRUCT-REF: index must be a fixnum");

    idx = CL_FIXNUM_VAL(args[1]);
    st = (CL_Struct *)CL_OBJ_TO_PTR(obj);

    if (idx < 0 || (uint32_t)idx >= st->n_slots)
        cl_error(CL_ERR_ARGS, "%%STRUCT-REF: index %d out of range", idx);

    return st->slots[idx];
}

/* (%struct-set obj index val) */
static CL_Obj bi_struct_set(CL_Obj *args, int n)
{
    CL_Obj obj = args[0];
    int idx;
    CL_Struct *st;
    CL_UNUSED(n);

    if (!CL_STRUCT_P(obj))
        cl_error(CL_ERR_TYPE, "%%STRUCT-SET: not a structure");
    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "%%STRUCT-SET: index must be a fixnum");

    idx = CL_FIXNUM_VAL(args[1]);
    st = (CL_Struct *)CL_OBJ_TO_PTR(obj);

    if (idx < 0 || (uint32_t)idx >= st->n_slots)
        cl_error(CL_ERR_ARGS, "%%STRUCT-SET: index %d out of range", idx);

    st->slots[idx] = args[2];
    return args[2];
}

/* (%copy-struct obj) */
static CL_Obj bi_copy_struct(CL_Obj *args, int n)
{
    CL_Obj obj = args[0];
    CL_Struct *src;
    CL_Obj copy;
    CL_Struct *dst;
    uint32_t i;
    CL_UNUSED(n);

    if (!CL_STRUCT_P(obj))
        cl_error(CL_ERR_TYPE, "%%COPY-STRUCT: not a structure");

    src = (CL_Struct *)CL_OBJ_TO_PTR(obj);

    CL_GC_PROTECT(obj);
    copy = cl_make_struct(src->type_desc, src->n_slots);
    CL_GC_UNPROTECT(1);

    /* Re-fetch src after potential GC */
    src = (CL_Struct *)CL_OBJ_TO_PTR(obj);
    dst = (CL_Struct *)CL_OBJ_TO_PTR(copy);
    for (i = 0; i < src->n_slots; i++)
        dst->slots[i] = src->slots[i];

    return copy;
}

/* (%struct-type-name obj) */
static CL_Obj bi_struct_type_name(CL_Obj *args, int n)
{
    CL_Struct *st;
    CL_UNUSED(n);

    if (!CL_STRUCT_P(args[0]))
        cl_error(CL_ERR_TYPE, "%%STRUCT-TYPE-NAME: not a structure");

    st = (CL_Struct *)CL_OBJ_TO_PTR(args[0]);
    return st->type_desc;
}

/* (structurep obj) */
static CL_Obj bi_structurep(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_STRUCT_P(args[0]) ? SYM_T : CL_NIL;
}

/* (%struct-slot-names type-name) — for :include support in macro */
static CL_Obj bi_struct_slot_names(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return cl_struct_slot_names(args[0]);
}

/* (%struct-slot-specs type-name) — returns ((name default) ...) for :include */
static CL_Obj bi_struct_slot_specs(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return get_slot_specs(args[0]);
}

/* (%struct-slot-count type-name) — for :include support in macro */
static CL_Obj bi_struct_slot_count(CL_Obj *args, int n)
{
    CL_Obj entry;
    CL_UNUSED(n);

    entry = find_struct_entry(args[0]);
    if (CL_NULL_P(entry)) return CL_MAKE_FIXNUM(0);

    /* entry = (name n-slots parent ((slot-spec) ...)) */
    return cl_car(cl_cdr(entry));  /* cadr = n-slots */
}

/* --- Registration --- */

void cl_builtins_struct_init(void)
{
    CL_GC_PROTECT(struct_table);
    CL_GC_UNPROTECT(1);

    defun("%REGISTER-STRUCT-TYPE", bi_register_struct_type, 4, 4);
    defun("%MAKE-STRUCT", bi_make_struct, 1, -1);
    defun("%STRUCT-REF", bi_struct_ref, 2, 2);
    defun("%STRUCT-SET", bi_struct_set, 3, 3);
    defun("%COPY-STRUCT", bi_copy_struct, 1, 1);
    defun("%STRUCT-TYPE-NAME", bi_struct_type_name, 1, 1);
    defun("STRUCTUREP", bi_structurep, 1, 1);
    defun("%STRUCT-SLOT-NAMES", bi_struct_slot_names, 1, 1);
    defun("%STRUCT-SLOT-SPECS", bi_struct_slot_specs, 1, 1);
    defun("%STRUCT-SLOT-COUNT", bi_struct_slot_count, 1, 1);
}
