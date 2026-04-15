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
#include "compiler.h"
#include "vm.h"
#include "../platform/platform.h"
#include "../platform/platform_thread.h"
#include <string.h>
#include <stdio.h>

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
CL_Obj struct_table = CL_NIL;

/* --- Registry lookup helpers --- */

/* Find registry entry for a struct type name.
 * Returns the entry (name n-slots parent (slot-names...)) or NIL. */
static CL_Obj find_struct_entry(CL_Obj type_name)
{
    CL_Obj result = CL_NIL;
    CL_Obj list;
    if (CL_MT()) platform_rwlock_rdlock(cl_tables_rwlock);
    list = struct_table;
    while (!CL_NULL_P(list)) {
        CL_Obj entry = cl_car(list);
        if (cl_car(entry) == type_name) {
            result = entry;
            break;
        }
        list = cl_cdr(list);
    }
    if (CL_MT()) platform_rwlock_unlock(cl_tables_rwlock);
    return result;
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

    CL_GC_PROTECT(specs);
    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);

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
    CL_GC_UNPROTECT(3);
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

/* --- CLOS class table for typep with multiple inheritance ---
 *
 * The struct registry only tracks a single parent, but CLOS classes
 * can have multiple superclasses. The class-precedence-list (CPL)
 * stored in each class metaobject (slot 3) is the correct authority.
 * This global is set from Lisp via %set-clos-class-table. */

CL_Obj cl_clos_class_table = 0;  /* NIL until CLOS loads */

/* Eq hash table lookup from C (for *class-table* which uses :test 'eq) */
static CL_Obj ht_eq_lookup(CL_Obj ht_obj, CL_Obj key)
{
    CL_Hashtable *ht;
    uint32_t hash, bucket_idx;
    CL_Obj chain;

    if (!CL_HASHTABLE_P(ht_obj)) return CL_NIL;

    ht = (CL_Hashtable *)CL_OBJ_TO_PTR(ht_obj);
    hash = (uint32_t)key;
    hash ^= hash >> 16;
    hash *= 0x45d9f3bU;
    hash ^= hash >> 16;
    bucket_idx = hash & (ht->bucket_count - 1);
    /* Access buckets through indirection (supports rehashed tables) */
    if (!CL_NULL_P(ht->bucket_vec)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(ht->bucket_vec);
        chain = v->data[bucket_idx];
    } else {
        chain = ht->buckets[bucket_idx];
    }

    while (!CL_NULL_P(chain)) {
        CL_Obj pair = cl_car(chain);
        if (cl_car(pair) == key)
            return cl_cdr(pair);
        chain = cl_cdr(chain);
    }
    return CL_NIL;
}

/* Check CLOS class hierarchy via class-precedence-list.
 * Returns 1 if obj_type is a subtype of test_type per CLOS CPL. */
int cl_clos_type_matches(CL_Obj obj_type, CL_Obj test_type)
{
    CL_Obj class_obj, cpl;
    CL_Struct *class_st;
    int result = 0;

    if (CL_MT()) platform_rwlock_rdlock(cl_tables_rwlock);
    if (CL_NULL_P(cl_clos_class_table)) {
        if (CL_MT()) platform_rwlock_unlock(cl_tables_rwlock);
        return 0;
    }

    /* Look up the class metaobject for obj_type */
    class_obj = ht_eq_lookup(cl_clos_class_table, obj_type);
    if (CL_NULL_P(class_obj) || !CL_STRUCT_P(class_obj)) {
        if (CL_MT()) platform_rwlock_unlock(cl_tables_rwlock);
        return 0;
    }

    /* CPL is in slot 3 of the class metaobject */
    class_st = (CL_Struct *)CL_OBJ_TO_PTR(class_obj);
    if (class_st->n_slots < 4) {
        if (CL_MT()) platform_rwlock_unlock(cl_tables_rwlock);
        return 0;
    }
    cpl = class_st->slots[3];

    /* Walk CPL — each element is a class metaobject, slot 0 = name */
    while (!CL_NULL_P(cpl)) {
        CL_Obj cpl_class = cl_car(cpl);
        if (CL_STRUCT_P(cpl_class)) {
            CL_Struct *cpl_st = (CL_Struct *)CL_OBJ_TO_PTR(cpl_class);
            if (cpl_st->n_slots > 0 && cpl_st->slots[0] == test_type) {
                result = 1;
                break;
            }
        }
        cpl = cl_cdr(cpl);
    }
    if (CL_MT()) platform_rwlock_unlock(cl_tables_rwlock);
    return result;
}

/* Check if a name is in the CLOS class table. */
int cl_clos_class_exists(CL_Obj name)
{
    int result;
    if (CL_MT()) platform_rwlock_rdlock(cl_tables_rwlock);
    if (CL_NULL_P(cl_clos_class_table)) {
        if (CL_MT()) platform_rwlock_unlock(cl_tables_rwlock);
        return 0;
    }
    result = !CL_NULL_P(ht_eq_lookup(cl_clos_class_table, name));
    if (CL_MT()) platform_rwlock_unlock(cl_tables_rwlock);
    return result;
}

static CL_Obj bi_set_clos_class_table(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (CL_MT()) platform_rwlock_wrlock(cl_tables_rwlock);
    cl_clos_class_table = args[0];
    if (CL_MT()) platform_rwlock_unlock(cl_tables_rwlock);
    return args[0];
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
    if (CL_MT()) platform_rwlock_wrlock(cl_tables_rwlock);
    struct_table = cl_cons(entry, struct_table);
    if (CL_MT()) platform_rwlock_unlock(cl_tables_rwlock);

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

    /* Validate struct object is within arena bounds */
    if (obj >= cl_heap.arena_size) {
        fprintf(stderr, "[BUG] %%STRUCT-REF: obj 0x%08x beyond arena (size 0x%08x)\n",
                (unsigned)obj, (unsigned)cl_heap.arena_size);
        cl_capture_backtrace();
        fprintf(stderr, "%s", cl_backtrace_buf);
        cl_error(CL_ERR_GENERAL, "%%STRUCT-REF: struct 0x%08x out of arena", (unsigned)obj);
    }

    idx = CL_FIXNUM_VAL(args[1]);
    st = (CL_Struct *)CL_OBJ_TO_PTR(obj);

    /* Validate n_slots field is sane */
    if (st->n_slots > 1000) {
        fprintf(stderr, "[BUG] %%STRUCT-REF: obj 0x%08x n_slots=%u (likely corrupt)\n",
                (unsigned)obj, (unsigned)st->n_slots);
        cl_capture_backtrace();
        fprintf(stderr, "%s", cl_backtrace_buf);
        cl_error(CL_ERR_GENERAL, "%%STRUCT-REF: corrupt struct (n_slots=%u)", (unsigned)st->n_slots);
    }

    if (idx < 0 || (uint32_t)idx >= st->n_slots)
        cl_error(CL_ERR_ARGS, "%%STRUCT-REF: index %d out of range (n_slots=%u)",
                 idx, (unsigned)st->n_slots);

    /* Validate slot value is a valid CL_Obj before returning */
    {
        CL_Obj val = st->slots[idx];
        if (CL_HEAP_P(val) && val >= cl_heap.arena_size) {
            fprintf(stderr, "[BUG] %%STRUCT-REF: obj 0x%08x slot[%d]=0x%08x beyond arena\n",
                    (unsigned)obj, idx, (unsigned)val);
            cl_capture_backtrace();
            fprintf(stderr, "%s", cl_backtrace_buf);
            cl_error(CL_ERR_GENERAL, "%%STRUCT-REF: slot[%d]=0x%08x out of arena", idx, (unsigned)val);
        }
        return val;
    }
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

/*
 * (%class-of obj) — return CL type-name symbol for any object.
 * For structs: returns the struct type name (e.g., POINT).
 * For built-in types: returns canonical CL class name symbol.
 * Used by CLOS class-of to map objects to class metaobjects.
 */
static CL_Obj bi_class_of(CL_Obj *args, int n)
{
    CL_Obj obj = args[0];
    const char *name;
    CL_UNUSED(n);

    if (CL_NULL_P(obj))
        return cl_intern("NULL", 4);
    if (CL_FIXNUM_P(obj))
        return cl_intern("FIXNUM", 6);
    if (CL_CHAR_P(obj))
        return cl_intern("CHARACTER", 9);

    if (CL_HEAP_P(obj)) {
        switch (CL_HDR_TYPE(CL_OBJ_TO_PTR(obj))) {
        case TYPE_CONS:
            return cl_intern("CONS", 4);
        case TYPE_SYMBOL:
            return cl_intern("SYMBOL", 6);
        case TYPE_STRING:
            return cl_intern("STRING", 6);
        case TYPE_FUNCTION:
        case TYPE_CLOSURE:
            return cl_intern("FUNCTION", 8);
        case TYPE_BYTECODE:
            return cl_intern("COMPILED-FUNCTION", 17);
        case TYPE_VECTOR: {
            CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
            if (v->flags & CL_VEC_FLAG_STRING)
                return cl_intern("STRING", 6);
            return (v->rank <= 1)
                ? cl_intern("VECTOR", 6)
                : cl_intern("ARRAY", 5);
        }
        case TYPE_PACKAGE:
            return cl_intern("PACKAGE", 7);
        case TYPE_HASHTABLE:
            return cl_intern("HASH-TABLE", 10);
        case TYPE_CONDITION: {
            CL_Condition *cond = (CL_Condition *)CL_OBJ_TO_PTR(obj);
            return cond->type_name;
        }
        case TYPE_STRUCT: {
            CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
            return st->type_desc;
        }
        case TYPE_BIGNUM:
            return cl_intern("BIGNUM", 6);
        case TYPE_SINGLE_FLOAT:
            return cl_intern("SINGLE-FLOAT", 12);
        case TYPE_DOUBLE_FLOAT:
            return cl_intern("DOUBLE-FLOAT", 12);
        case TYPE_RATIO:
            return cl_intern("RATIO", 5);
        case TYPE_STREAM:
            return cl_intern("STREAM", 6);
        case TYPE_RANDOM_STATE:
            return cl_intern("RANDOM-STATE", 12);
        case TYPE_BIT_VECTOR:
            return cl_intern("BIT-VECTOR", 10);
        case TYPE_PATHNAME:
            return cl_intern("PATHNAME", 8);
        default:
            break;
        }
    }
    name = "T";
    return cl_intern(name, (uint32_t)strlen(name));
}

/* --- Registration --- */

/* (%struct-change-class obj new-type-name new-slot-count)
 * Modify a struct's type_desc and n_slots in-place if the allocated size allows.
 * Returns T on success, NIL if the struct doesn't have enough allocated space. */
static CL_Obj bi_struct_change_class(CL_Obj *args, int n)
{
    CL_Struct *st;
    uint32_t new_n_slots, alloc_size, needed_size;
    CL_UNUSED(n);

    if (!CL_STRUCT_P(args[0]))
        cl_error(CL_ERR_TYPE, "%%STRUCT-CHANGE-CLASS: not a structure");
    if (!CL_FIXNUM_P(args[2]))
        cl_error(CL_ERR_TYPE, "%%STRUCT-CHANGE-CLASS: slot count must be a fixnum");

    st = (CL_Struct *)CL_OBJ_TO_PTR(args[0]);
    new_n_slots = (uint32_t)CL_FIXNUM_VAL(args[2]);

    /* Check if current allocation can hold the new slot count */
    alloc_size = CL_HDR_SIZE(st);
    needed_size = sizeof(CL_Struct) + new_n_slots * sizeof(CL_Obj);
    if (needed_size > alloc_size)
        return CL_NIL;  /* Not enough space */

    /* Zero any new slots beyond old n_slots */
    if (new_n_slots > st->n_slots) {
        uint32_t i;
        for (i = st->n_slots; i < new_n_slots; i++)
            st->slots[i] = CL_NIL;
    }

    st->type_desc = args[1];
    st->n_slots = new_n_slots;
    return SYM_T;
}

void cl_builtins_struct_init(void)
{
    cl_register_builtin("%REGISTER-STRUCT-TYPE", bi_register_struct_type, 4, 4, cl_package_clamiga);
    cl_register_builtin("%MAKE-STRUCT", bi_make_struct, 1, -1, cl_package_clamiga);
    cl_register_builtin("%STRUCT-REF", bi_struct_ref, 2, 2, cl_package_clamiga);
    cl_register_builtin("%STRUCT-SET", bi_struct_set, 3, 3, cl_package_clamiga);
    cl_register_builtin("%COPY-STRUCT", bi_copy_struct, 1, 1, cl_package_clamiga);
    cl_register_builtin("%STRUCT-TYPE-NAME", bi_struct_type_name, 1, 1, cl_package_clamiga);
    defun("STRUCTUREP", bi_structurep, 1, 1);
    cl_register_builtin("%STRUCT-SLOT-NAMES", bi_struct_slot_names, 1, 1, cl_package_clamiga);
    cl_register_builtin("%STRUCT-SLOT-SPECS", bi_struct_slot_specs, 1, 1, cl_package_clamiga);
    cl_register_builtin("%STRUCT-SLOT-COUNT", bi_struct_slot_count, 1, 1, cl_package_clamiga);
    cl_register_builtin("%CLASS-OF", bi_class_of, 1, 1, cl_package_clamiga);
    cl_register_builtin("%SET-CLOS-CLASS-TABLE", bi_set_clos_class_table, 1, 1, cl_package_clamiga);
    cl_register_builtin("%STRUCT-CHANGE-CLASS", bi_struct_change_class, 3, 3, cl_package_clamiga);
}
