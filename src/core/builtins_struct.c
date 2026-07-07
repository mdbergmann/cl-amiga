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

/* Struct type registry:
 * alist of (name n-slots parent (slot-names...)) */
CL_Obj struct_table = CL_NIL;

/* Sentinel symbols matched by compile_call to emit OP_STRUCT_REF /
 * OP_STRUCT_SET — interned in cl_package_clamiga during init.  Same
 * pattern as cl_amiga_ffi_call_sym: external (.h) so the compiler can
 * reference them without depending on this file's internals. */
CL_Obj cl_struct_ref_sym = CL_NIL;
CL_Obj cl_struct_set_sym = CL_NIL;

/* --- Registry lookup helpers --- */

/* --- Registry hash index ---
 *
 * find_struct_entry used to walk the struct_table alist linearly.  The
 * registry is PREPENDED on registration, so early-defined types (base
 * conditions, library structs) sit at the tail and pay a full
 * O(registered-types) walk — and SLOT-VALUE on a struct instance
 * resolves its slot through here on EVERY access (2026-07-05 sento
 * runtime profile: the largest attackable CPU cluster after the VM
 * dispatch loop itself).
 *
 * The index is an open-addressing table mapping type-name symbol ->
 * registry entry, in the style of the FASL writer's MLF index (fasl.c):
 *
 * - Compaction moves symbols and entries, invalidating both the hashed
 *   key bits and the cached entry values.  cl_struct_index_gc_invalidate
 *   (called from the compaction update phase, all mutators stopped)
 *   marks the index dirty; it is rebuilt lazily on the next lookup.
 * - Registration marks the index dirty in the same wrlock critical
 *   section that prepends the entry, so a probe of a CLEAN index is
 *   always consistent with the alist.  Registration is rare and a
 *   rebuild is O(types) — negligible even during cold loads.
 * - Rebuilds allocate with platform_alloc only (never the arena), so a
 *   rebuild cannot itself trigger GC, and they walk the alist with raw
 *   cons access — cl_car can cl_error→longjmp, which must not happen
 *   while holding the wrlock (leaked-reader lesson, see fallback below).
 * - On OOM or a malformed alist cell the index is disabled permanently
 *   and lookups fall back to the linear walk — slower, never wrong. */

typedef struct {
    CL_Obj name;    /* type-name symbol; CL_NIL (== 0) = empty slot */
    CL_Obj entry;   /* registry entry (name n-slots parent (specs)) */
} StructIndexSlot;

static struct {
    StructIndexSlot *slots;
    uint32_t cap;   /* power of two; 0 = unallocated */
    int dirty;      /* alist changed or compaction moved objects */
    int disabled;   /* OOM / malformed cell — permanent linear fallback */
} struct_index;

static uint32_t struct_name_hash(CL_Obj obj)
{
    /* Heap offsets are CL_ALIGN-aligned, so shift the always-zero low
     * bits out before Knuth's golden-ratio multiplier (see
     * fasl_obj_hash in fasl.c). */
#if CL_ALIGN == 8
    return ((uint32_t)obj >> 3) * 2654435769u;
#else
    return ((uint32_t)obj >> 2) * 2654435769u;
#endif
}

/* Mark the index stale.  Called from gc_update_shared_roots during the
 * compaction update phase (world stopped) — and mirrored inline, under
 * the tables wrlock, by bi_register_struct_type. */
void cl_struct_index_gc_invalidate(void)
{
    struct_index.dirty = 1;
}

/* (Re)build the index from the current struct_table.  Caller holds the
 * tables wrlock.  Returns 1 when the index is usable; 0 disables it. */
static int struct_index_rebuild(void)
{
    uint32_t count = 0, cap, mask;
    StructIndexSlot *slots;
    CL_Obj list;

    /* Validate + count with raw cons access (no cl_car: it can
     * cl_error→longjmp out from under the held wrlock). */
    for (list = struct_table; !CL_NULL_P(list); ) {
        CL_Cons *c;
        if (!CL_CONS_P(list)) goto disable;
        c = (CL_Cons *)CL_OBJ_TO_PTR(list);
        if (!CL_CONS_P(c->car)) goto disable;
        if (!CL_SYMBOL_P(((CL_Cons *)CL_OBJ_TO_PTR(c->car))->car)) goto disable;
        count++;
        list = c->cdr;
    }

    cap = 64;
    while (cap < count * 2 + 1) cap <<= 1;
    slots = (StructIndexSlot *)platform_alloc(cap * sizeof(StructIndexSlot));
    if (!slots) goto disable;
    memset(slots, 0, cap * sizeof(StructIndexSlot));   /* CL_NIL == 0 */
    mask = cap - 1;

    /* Head-first insert, first key wins — matches the linear walk's
     * first-match semantics when a type has been re-registered. */
    for (list = struct_table; !CL_NULL_P(list); ) {
        CL_Cons *c = (CL_Cons *)CL_OBJ_TO_PTR(list);
        CL_Obj entry = c->car;
        CL_Obj name = ((CL_Cons *)CL_OBJ_TO_PTR(entry))->car;
        uint32_t slot = struct_name_hash(name) & mask;
        while (!CL_NULL_P(slots[slot].name) && slots[slot].name != name)
            slot = (slot + 1) & mask;
        if (CL_NULL_P(slots[slot].name)) {
            slots[slot].name = name;
            slots[slot].entry = entry;
        }
        list = c->cdr;
    }

    if (struct_index.slots) platform_free(struct_index.slots);
    struct_index.slots = slots;
    struct_index.cap = cap;
    struct_index.dirty = 0;
    return 1;

disable:
    if (struct_index.slots) platform_free(struct_index.slots);
    struct_index.slots = NULL;
    struct_index.cap = 0;
    struct_index.disabled = 1;
    return 0;
}

/* Probe a CLEAN index for TYPE_NAME.  Caller holds the tables lock.
 * Pure memory reads — cannot error, cannot allocate. */
static CL_Obj struct_index_probe(CL_Obj type_name)
{
    uint32_t mask = struct_index.cap - 1;
    uint32_t slot = struct_name_hash(type_name) & mask;
    while (!CL_NULL_P(struct_index.slots[slot].name)) {
        if (struct_index.slots[slot].name == type_name)
            return struct_index.slots[slot].entry;
        slot = (slot + 1) & mask;
    }
    return CL_NIL;
}

/* Find registry entry for a struct type name.
 * Returns the entry (name n-slots parent (slot-names...)) or NIL.
 *
 * Fast path: O(1) probe of the hash index under the rdlock.  A dirty
 * or unbuilt index is rebuilt under the wrlock first.
 *
 * Fallback (index disabled): snapshot-and-release linear walk —
 * struct_table is only ever PREPENDED to, so once we capture the head
 * pointer under the rdlock we can release the lock and walk the
 * snapshot.  Holding the rdlock across cl_car (which can cl_error →
 * longjmp on a corrupt cell) leaked readers and ultimately tripped the
 * bi_condition_wait safety abort in sento dispatcher tests. */
static CL_Obj find_struct_entry(CL_Obj type_name)
{
    CL_Obj list, result;

    cl_tables_rdlock();
    if (struct_index.slots && !struct_index.dirty && !struct_index.disabled) {
        result = struct_index_probe(type_name);
        cl_tables_rwunlock();
        return result;
    }
    cl_tables_rwunlock();

    if (!struct_index.disabled) {
        cl_tables_wrlock();
        if (!struct_index.disabled &&
            (struct_index.slots && !struct_index.dirty
             ? 1 : struct_index_rebuild())) {
            result = struct_index_probe(type_name);
            cl_tables_rwunlock();
            return result;
        }
        cl_tables_rwunlock();
    }

    cl_tables_rdlock();
    list = struct_table;
    cl_tables_rwunlock();
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
 * Returns 1 if obj_type is a subtype of test_type per CLOS CPL.
 *
 * Snapshot-and-release: capture the class metaobject under the rdlock,
 * release, then walk its CPL.  Holding the rdlock across cl_car (which
 * can cl_error→longjmp on a corrupt cell) leaks readers — and the CPL
 * walker calls into CLOS-aware code (cl_car can hit a malformed cell). */
int cl_clos_type_matches(CL_Obj obj_type, CL_Obj test_type)
{
    CL_Obj class_obj, cpl;
    CL_Struct *class_st;

    cl_tables_rdlock();
    if (CL_NULL_P(cl_clos_class_table)) {
        cl_tables_rwunlock();
        return 0;
    }
    class_obj = ht_eq_lookup(cl_clos_class_table, obj_type);
    cl_tables_rwunlock();

    if (CL_NULL_P(class_obj) || !CL_STRUCT_P(class_obj))
        return 0;

    /* CPL is in slot 3 of the class metaobject */
    class_st = (CL_Struct *)CL_OBJ_TO_PTR(class_obj);
    if (class_st->n_slots < 4)
        return 0;
    cpl = class_st->slots[3];

    while (!CL_NULL_P(cpl)) {
        CL_Obj cpl_class = cl_car(cpl);
        if (CL_STRUCT_P(cpl_class)) {
            CL_Struct *cpl_st = (CL_Struct *)CL_OBJ_TO_PTR(cpl_class);
            if (cpl_st->n_slots > 0 && cpl_st->slots[0] == test_type)
                return 1;
        }
        cpl = cl_cdr(cpl);
    }
    return 0;
}

/* Check if a name is in the CLOS class table. */
int cl_clos_class_exists(CL_Obj name)
{
    int result;
    cl_tables_rdlock();
    if (CL_NULL_P(cl_clos_class_table)) {
        cl_tables_rwunlock();
        return 0;
    }
    result = !CL_NULL_P(ht_eq_lookup(cl_clos_class_table, name));
    cl_tables_rwunlock();
    return result;
}

static CL_Obj bi_set_clos_class_table(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    cl_tables_wrlock();
    cl_clos_class_table = args[0];
    cl_tables_rwunlock();
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
    /* Prepend + index dirty-mark must be ONE wrlock critical section,
     * so a probe of a clean index is always consistent with the alist.
     * The head cell is consed OUTSIDE the lock (arena allocation under
     * the wrlock is an STW-vs-rwlock deadlock — see
     * cl_table_prepend_locked, whose shape this mirrors). */
    {
        CL_Obj cell = cl_cons(entry, CL_NIL);
        cl_tables_wrlock();
        ((CL_Cons *)CL_OBJ_TO_PTR(cell))->cdr = struct_table;
        struct_table = cell;
        struct_index.dirty = 1;
        cl_tables_rwunlock();
    }

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

/* (%register-funcallable-gf-type type-name) — declare TYPE-NAME (a struct-type
 * name symbol naming a STANDARD-GENERIC-FUNCTION subclass) as a funcallable
 * generic-function type, so its instances dispatch as functions.  Supports
 * custom generic-function metaclasses (CLHS :generic-function-class). */
static CL_Obj bi_register_funcallable_gf_type(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_SYMBOL_P(args[0]))
        cl_error(CL_ERR_TYPE,
                 "%%REGISTER-FUNCALLABLE-GF-TYPE: name must be a symbol");
    cl_register_funcallable_gf_type(args[0]);
    return args[0];
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
DEFINE_TYPE_PREDICATE(bi_structurep, CL_STRUCT_P)

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

/* (%struct-slot-index type-name slot-name) — position of SLOT-NAME in
 * struct type TYPE-NAME's slots, or NIL.  Zero-allocation: SLOT-VALUE /
 * (SETF SLOT-VALUE) / SLOT-BOUNDP on struct instances resolve slots
 * through here on every access (clos.lisp %find-struct-slot-index); the
 * previous shape (%STRUCT-SLOT-NAMES + Lisp-side linear match) consed a
 * fresh name list per access. */
static CL_Obj bi_struct_slot_index(CL_Obj *args, int n)
{
    CL_Obj specs = get_slot_specs(args[0]);
    int32_t idx = 0;
    CL_UNUSED(n);

    while (!CL_NULL_P(specs)) {
        CL_Obj spec = cl_car(specs);
        CL_Obj name = CL_CONS_P(spec) ? cl_car(spec) : spec;
        if (name == args[1])
            return CL_MAKE_FIXNUM(idx);
        idx++;
        specs = cl_cdr(specs);
    }
    return CL_NIL;
}

/* --- Fused fast-path slot access (spec 3.1, second half) ---
 *
 * SLOT-VALUE and the DEFCLASS-generated accessor functions used to pay
 * ~4 Lisp calls + ~8 builtin dispatches per access (class-of, the
 * slot-index-table branch, %STRUCT-SLOT-INDEX, then a separate
 * %STRUCT-REF).  These two builtins fuse the whole common case into ONE
 * builtin call: type-name -> registry entry (O(1) hash probe) -> slot
 * index (short EQ scan of the specs) -> direct slot read/write.
 *
 * They are deliberately NON-ERRORING and NON-ALLOCATING: any case the
 * fast path cannot handle (not a struct, unregistered type, no such
 * instance slot, index beyond n_slots on an obsolete instance) reports
 * a miss and the Lisp caller falls back to the full SLOT-VALUE protocol
 * — which owns :CLASS-allocated slots, conditions, SLOT-UNBOUND, and
 * the "no slot named X" error.  Because the registry entry is looked up
 * by the instance's own type_desc on EVERY access, the resolved index
 * is always the current layout: correct across class redefinition and
 * for subclass instances whose inherited slots sit at different indices
 * (the hazard a captured-index accessor closure would have).
 *
 * The specs list is walked with raw checked cons access, not cl_car:
 * these run on the hottest Lisp path and must not error out of a
 * malformed registry cell — a malformed cell is just a miss. */

/* Return the specs list ((name default) ...) of a registry entry, or
 * CL_NIL if the entry shape is unexpected.  Raw, non-erroring cadddr. */
static CL_Obj struct_entry_specs_raw(CL_Obj entry)
{
    int i;
    for (i = 0; i < 3; i++) {
        if (!CL_CONS_P(entry)) return CL_NIL;
        entry = ((CL_Cons *)CL_OBJ_TO_PTR(entry))->cdr;
    }
    if (!CL_CONS_P(entry)) return CL_NIL;
    return ((CL_Cons *)CL_OBJ_TO_PTR(entry))->car;
}

/* Resolve SLOT_NAME to its index in OBJ's registered layout.
 * Returns the index, or -1 on any miss.  Non-erroring, non-allocating. */
static int32_t struct_slot_resolve(CL_Obj obj, CL_Obj slot_name)
{
    CL_Struct *st;
    CL_Obj entry, specs;
    int32_t idx = 0;

    if (!CL_STRUCT_P(obj))
        return -1;
    st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
    /* find_struct_entry never allocates from the arena (index rebuilds
     * use platform_alloc), so st cannot move underneath us. */
    entry = find_struct_entry(st->type_desc);
    if (CL_NULL_P(entry))
        return -1;
    specs = struct_entry_specs_raw(entry);
    while (CL_CONS_P(specs)) {
        CL_Cons *c = (CL_Cons *)CL_OBJ_TO_PTR(specs);
        CL_Obj name = CL_CONS_P(c->car)
            ? ((CL_Cons *)CL_OBJ_TO_PTR(c->car))->car
            : c->car;
        if (name == slot_name)
            return ((uint32_t)idx < st->n_slots) ? idx : -1;
        idx++;
        specs = c->cdr;
    }
    return -1;
}

/* (%struct-slot-value obj slot-name miss) — value of the instance slot
 * SLOT-NAME in OBJ, or MISS when the fast path does not apply.  Callers
 * pass the slot-unbound marker as MISS, so an unbound CLOS slot (whose
 * storage holds that marker) also reads back as a miss and routes to
 * the slow path's SLOT-UNBOUND protocol for free. */
static CL_Obj bi_struct_slot_value(CL_Obj *args, int n)
{
    int32_t idx = struct_slot_resolve(args[0], args[1]);
    CL_UNUSED(n);
    if (idx < 0)
        return args[2];
    return ((CL_Struct *)CL_OBJ_TO_PTR(args[0]))->slots[idx];
}

/* (%struct-slot-store obj slot-name value) — store VALUE into the
 * instance slot SLOT-NAME of OBJ.  Returns T when stored, NIL when the
 * fast path does not apply (caller falls back to (SETF SLOT-VALUE)'s
 * full protocol). */
static CL_Obj bi_struct_slot_store(CL_Obj *args, int n)
{
    int32_t idx = struct_slot_resolve(args[0], args[1]);
    CL_UNUSED(n);
    if (idx < 0)
        return CL_NIL;
    ((CL_Struct *)CL_OBJ_TO_PTR(args[0]))->slots[idx] = args[2];
    return SYM_T;
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
/* Resolve a built-in class NAME to its canonical class-name symbol.
 *
 * The built-in class names (SYMBOL, CONS, STRING, FIXNUM, BIGNUM, RATIO, ...)
 * are all standard COMMON-LISP-package symbols, and *class-table* (see
 * clos.lisp) is keyed by those exact CL-package symbols.  They MUST be
 * resolved in the COMMON-LISP package — NOT via *PACKAGE*-relative cl_intern.
 *
 * cl_intern interns relative to cl_current_package, a *shared C global* synced
 * from the per-thread *PACKAGE* dynamic binding.  If *PACKAGE* is (or is
 * transiently clobbered to) a package that does not resolve the name to its
 * CL-package symbol — e.g. KEYWORD during a #. / #+ reader excursion, or
 * another thread's *PACKAGE* leaking through the global in a multi-threaded
 * session (Sly/slynk workers, sento, log4cl's watcher) — cl_intern would
 * return a *different* symbol.  (gethash that-symbol *class-table*) then misses
 * and class-of silently falls back to the T class, which makes CLOS dispatch
 * compute an empty applicable-method set even though a method plainly applies
 * ("No applicable method for ... (X SYMBOL)").  Struct/condition objects are
 * unaffected (they return their stored type_desc/type_name), which is exactly
 * why the field failures only ever mis-classified the built-in-typed argument.
 * Interning in cl_package_cl makes class-of independent of *PACKAGE* and of
 * the thread-shared cl_current_package. */
static CL_Obj class_name_sym(const char *name, uint32_t len)
{
    return cl_intern_in(name, len, cl_package_cl);
}

/* Core of %CLASS-OF: map any object to its CL class-name symbol.
 * May allocate ONLY on an intern miss (never after boot — every name
 * below is a standard preinterned CL symbol); callers that hold raw
 * CL_Obj locals across it must still GC-protect them. */
static CL_Obj class_of_type_name(CL_Obj obj)
{
    if (CL_NULL_P(obj))
        return class_name_sym("NULL", 4);
    if (CL_FIXNUM_P(obj))
        return class_name_sym("FIXNUM", 6);
    if (CL_CHAR_P(obj))
        return class_name_sym("CHARACTER", 9);

    if (CL_HEAP_P(obj)) {
        switch (CL_HDR_TYPE(CL_OBJ_TO_PTR(obj))) {
        case TYPE_CONS:
            return class_name_sym("CONS", 4);
        case TYPE_SYMBOL:
            return class_name_sym("SYMBOL", 6);
        case TYPE_STRING:
#ifdef CL_WIDE_STRINGS
        case TYPE_WIDE_STRING:
#endif
            return class_name_sym("STRING", 6);
        case TYPE_FUNCTION:
        case TYPE_CLOSURE:
            return class_name_sym("FUNCTION", 8);
        case TYPE_BYTECODE:
            return class_name_sym("COMPILED-FUNCTION", 17);
        case TYPE_VECTOR: {
            CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
            if (v->flags & CL_VEC_FLAG_STRING)
                return class_name_sym("STRING", 6);
            return (v->rank <= 1)
                ? class_name_sym("VECTOR", 6)
                : class_name_sym("ARRAY", 5);
        }
        case TYPE_PACKAGE:
            return class_name_sym("PACKAGE", 7);
        case TYPE_HASHTABLE:
            return class_name_sym("HASH-TABLE", 10);
        case TYPE_CONDITION: {
            CL_Condition *cond = (CL_Condition *)CL_OBJ_TO_PTR(obj);
            return cond->type_name;
        }
        case TYPE_STRUCT: {
            CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
            return st->type_desc;
        }
        case TYPE_BIGNUM:
            return class_name_sym("BIGNUM", 6);
        case TYPE_SINGLE_FLOAT:
            return class_name_sym("SINGLE-FLOAT", 12);
        case TYPE_DOUBLE_FLOAT:
            return class_name_sym("DOUBLE-FLOAT", 12);
        case TYPE_RATIO:
            return class_name_sym("RATIO", 5);
        case TYPE_STREAM:
            return class_name_sym("STREAM", 6);
        case TYPE_RANDOM_STATE:
            return class_name_sym("RANDOM-STATE", 12);
        case TYPE_BIT_VECTOR:
            return class_name_sym("BIT-VECTOR", 10);
        case TYPE_PATHNAME:
            return class_name_sym("PATHNAME", 8);
        default:
            break;
        }
    }
    return class_name_sym("T", 1);
}

static CL_Obj bi_class_of(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return class_of_type_name(args[0]);
}

/* (%gf-ic-emf gf a) / (%gf-ic-emf gf a b) — probe GF's inline cache
 * (GF slot 8: (class . emf) for 1-arg GFs, (class1 class2 . emf) for
 * 2-arg) against the receivers' classes.  Returns the cached EMF on a
 * hit, NIL on any miss — the Lisp slow path (%GF-DISPATCH-*-SLOW) owns
 * everything else, including populating the cache.
 *
 * This fuses the per-call dispatch chain the arity-specialized
 * discriminators used to run in Lisp — (gf-inline-cache gf) call,
 * CLASS-OF call (builtin + *CLASS-TABLE* gethash), EQ compare — into
 * one builtin call (spec 3.1, second half: the class-of/generic-lookup
 * chain was the measured remainder of accessor cost).
 *
 * Non-erroring by design: any unexpected shape is a miss.  A receiver
 * whose type name is not in *CLASS-TABLE* is a miss too — the slow
 * path's CLASS-OF resolves it to the T class and dispatches correctly
 * (such a receiver can never legitimately hit an IC entry, since ICs
 * are populated with table-resident classes).
 *
 * GC note: class_of_type_name can allocate only on an intern miss
 * (never after boot).  Both names are resolved BEFORE the ic cons is
 * read, and name1 is protected across the name2 resolution, so nothing
 * here holds a stale offset even in that worst case. */
static CL_Obj bi_gf_ic_emf(CL_Obj *args, int n)
{
    CL_Obj name1, name2 = CL_NIL, cls1, cls2 = CL_NIL, ic;
    CL_Struct *st;
    CL_Cons *c;
    int two = (n == 3);

    if (!CL_STRUCT_P(args[0]))
        return CL_NIL;

    name1 = class_of_type_name(args[1]);
    if (two) {
        CL_GC_PROTECT(name1);
        name2 = class_of_type_name(args[2]);
        CL_GC_UNPROTECT(1);
    }

    cl_tables_rdlock();
    if (CL_NULL_P(cl_clos_class_table)) {
        cl_tables_rwunlock();
        return CL_NIL;
    }
    cls1 = ht_eq_lookup(cl_clos_class_table, name1);
    if (two)
        cls2 = ht_eq_lookup(cl_clos_class_table, name2);
    cl_tables_rwunlock();
    if (CL_NULL_P(cls1) || (two && CL_NULL_P(cls2)))
        return CL_NIL;

    st = (CL_Struct *)CL_OBJ_TO_PTR(args[0]);
    if (st->n_slots < 9)
        return CL_NIL;
    ic = st->slots[8];
    if (!CL_CONS_P(ic))
        return CL_NIL;
    c = (CL_Cons *)CL_OBJ_TO_PTR(ic);
    if (c->car != cls1)
        return CL_NIL;
    if (!two)
        return c->cdr;
    if (!CL_CONS_P(c->cdr))
        return CL_NIL;
    c = (CL_Cons *)CL_OBJ_TO_PTR(c->cdr);
    if (c->car != cls2)
        return CL_NIL;
    return c->cdr;
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
    /* A re-initialized runtime (test harnesses: cl_mem_shutdown +
     * cl_mem_init + *_init again) starts with a FRESH arena, but these
     * statics survive and still hold the previous runtime's arena
     * offsets.  A stale struct_table tail gets marked/updated by the GC
     * as if live — garbage treated as object starts, corrupting the
     * heap on the next compaction (surfaced as a layout-dependent
     * "CAR: not of type LIST" in test_gc_markstack's fresh-runtime
     * phase).  Reset registry, CLOS class table and index; in a normal
     * once-per-process boot this is a no-op. */
    struct_table = CL_NIL;
    cl_clos_class_table = CL_NIL;
    if (struct_index.slots) platform_free(struct_index.slots);
    struct_index.slots = NULL;
    struct_index.cap = 0;
    struct_index.dirty = 0;
    struct_index.disabled = 0;

    cl_register_builtin("%REGISTER-STRUCT-TYPE", bi_register_struct_type, 4, 4, cl_package_clamiga);
    cl_register_builtin("%MAKE-STRUCT", bi_make_struct, 1, -1, cl_package_clamiga);
    cl_register_builtin("%REGISTER-FUNCALLABLE-GF-TYPE",
                        bi_register_funcallable_gf_type, 1, 1, cl_package_clamiga);
    cl_register_builtin("%STRUCT-REF", bi_struct_ref, 2, 2, cl_package_clamiga);
    cl_register_builtin("%STRUCT-SET", bi_struct_set, 3, 3, cl_package_clamiga);
    cl_register_builtin("%COPY-STRUCT", bi_copy_struct, 1, 1, cl_package_clamiga);
    defun("COPY-STRUCTURE", bi_copy_struct, 1, 1);
    cl_register_builtin("%STRUCT-TYPE-NAME", bi_struct_type_name, 1, 1, cl_package_clamiga);
    /* STRUCTUREP — implementation predicate in CLAMIGA.
     * CL :uses CLAMIGA so existing internal code sees it as a bare symbol.
     * User packages that only (:use :common-lisp) must add (:use :clamiga)
     * or qualify with the CLAMIGA: prefix. */
    cl_register_builtin("STRUCTUREP", bi_structurep, 1, 1, cl_package_clamiga);
    cl_register_builtin("%STRUCT-SLOT-NAMES", bi_struct_slot_names, 1, 1, cl_package_clamiga);
    cl_register_builtin("%STRUCT-SLOT-SPECS", bi_struct_slot_specs, 1, 1, cl_package_clamiga);
    cl_register_builtin("%STRUCT-SLOT-INDEX", bi_struct_slot_index, 2, 2, cl_package_clamiga);
    cl_register_builtin("%STRUCT-SLOT-VALUE", bi_struct_slot_value, 3, 3, cl_package_clamiga);
    cl_register_builtin("%STRUCT-SLOT-STORE", bi_struct_slot_store, 3, 3, cl_package_clamiga);
    cl_register_builtin("%STRUCT-SLOT-COUNT", bi_struct_slot_count, 1, 1, cl_package_clamiga);
    cl_register_builtin("%CLASS-OF", bi_class_of, 1, 1, cl_package_clamiga);
    cl_register_builtin("%GF-IC-EMF", bi_gf_ic_emf, 2, 3, cl_package_clamiga);
    cl_register_builtin("%SET-CLOS-CLASS-TABLE", bi_set_clos_class_table, 1, 1, cl_package_clamiga);
    cl_register_builtin("%STRUCT-CHANGE-CLASS", bi_struct_change_class, 3, 3, cl_package_clamiga);

    /* Cache the symbols compile_call matches against to emit dedicated
     * struct-access bytecodes.  The %STRUCT-REF / %STRUCT-SET builtins
     * stay registered above as the runtime fallback for callers with a
     * dynamically-computed slot index. */
    cl_struct_ref_sym = cl_intern_in("%STRUCT-REF", 11, cl_package_clamiga);
    cl_struct_set_sym = cl_intern_in("%STRUCT-SET", 11, cl_package_clamiga);
    cl_gc_register_root(&cl_struct_ref_sym);
    cl_gc_register_root(&cl_struct_set_sym);
}
