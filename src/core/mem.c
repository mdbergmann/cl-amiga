#include "mem.h"
#include "error.h"
#include "float.h"
#include "vm.h"
#include "readtable.h"
#include "package.h"
#include "stream.h"
#include "../platform/platform.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* External roots needed for GC marking */
extern CL_Obj macro_table, setf_table, setf_fn_table, setf_expander_table, type_table;
extern CL_Obj cl_clos_class_table;
extern CL_Obj struct_table;  /* builtins_struct.c: struct type registry */
extern CL_Obj condition_hierarchy;     /* builtins_condition.c */
extern CL_Obj condition_slot_table;    /* builtins_condition.c */

/* Active compiler chain for GC root marking (compiler_internal.h) */
typedef struct CL_Compiler_s CL_Compiler;
extern CL_Compiler *cl_active_compiler;

CL_Heap cl_heap;
uint8_t *cl_arena_base = NULL;  /* Global arena base for offset↔pointer conversion */

/* GC root stack — stores pointers to CL_Obj variables */
static CL_Obj *gc_root_stack[CL_GC_ROOT_STACK_SIZE];
int gc_root_count = 0;

/* GC mark stack (iterative marking) */
static CL_Obj gc_mark_stack[CL_GC_MARK_STACK_SIZE];
static int gc_mark_top = 0;
static int gc_mark_overflow = 0;  /* Set when mark stack overflows */

/* Forward declarations */
static void gc_mark(void);
static void gc_sweep(void);
void gc_mark_obj(CL_Obj obj);
static void gc_mark_push(CL_Obj obj);
static void *alloc_from_free_list(uint32_t *sizep);
static void *alloc_from_bump(uint32_t size);

/* Align size up to CL_ALIGN boundary */
static uint32_t align_up(uint32_t size)
{
    return (size + CL_ALIGN - 1) & ~(CL_ALIGN - 1);
}

void cl_mem_init(uint32_t heap_size)
{
    if (heap_size == 0)
        heap_size = CL_DEFAULT_HEAP_SIZE;

    cl_heap.arena = (uint8_t *)platform_alloc(heap_size);
    if (!cl_heap.arena) {
        platform_write_string("FATAL: Failed to allocate heap\n");
        return;
    }
    cl_arena_base = cl_heap.arena;
    cl_heap.arena_size = heap_size;
    cl_heap.bump = CL_ALIGN;  /* Skip offset 0 so it stays NIL */
    cl_heap.free_list = 0;
    cl_heap.total_allocated = 0;
    cl_heap.total_consed = 0;
    cl_heap.gc_count = 0;

    gc_root_count = 0;
    gc_mark_top = 0;
}

void cl_mem_shutdown(void)
{
    if (cl_heap.arena) {
        platform_free(cl_heap.arena);
        cl_heap.arena = NULL;
    }
}

static void *alloc_from_bump(uint32_t size)
{
    if (cl_heap.bump + size <= cl_heap.arena_size) {
        void *ptr = cl_heap.arena + cl_heap.bump;
        cl_heap.bump += size;
        return ptr;
    }
    return NULL;
}

static void *alloc_from_free_list(uint32_t *sizep)
{
    uint32_t size = *sizep;
    uint32_t *prev_off = &cl_heap.free_list;
    uint32_t cur_off = cl_heap.free_list;

    while (cur_off) {
        CL_FreeBlock *block = (CL_FreeBlock *)(cl_heap.arena + cur_off);
        if (block->size >= size) {
            uint32_t remainder = block->size - size;
            if (remainder >= CL_MIN_ALLOC_SIZE) {
                /* Split block */
                uint32_t new_off = cur_off + size;
                CL_FreeBlock *new_free = (CL_FreeBlock *)(cl_heap.arena + new_off);
                new_free->size = remainder;
                new_free->next_offset = block->next_offset;
                *prev_off = new_off;
            } else {
                /* Use entire block — report actual size so header matches */
                size = block->size;
                *sizep = size;
                *prev_off = block->next_offset;
            }
            memset(block, 0, size);
            return block;
        }
        prev_off = &block->next_offset;
        cur_off = block->next_offset;
    }
    return NULL;
}

/* Signal a storage error without allocating (safe when heap is exhausted
 * or corrupted).  Uses direct longjmp, bypassing cl_error() which would
 * try to allocate condition objects. */
void cl_storage_error(const char *fmt, ...)
{
    va_list ap;
    cl_error_code = CL_ERR_STORAGE;
    va_start(ap, fmt);
    vsnprintf(cl_error_msg, sizeof(cl_error_msg), fmt, ap);
    va_end(ap);
    /* Skip cl_capture_backtrace() — VM frames may reference corrupted heap */
    cl_backtrace_buf[0] = '\0';
    cl_gc_reset_roots();
    cl_vm.sp = 0;
    cl_vm.fp = 0;
    cl_nlx_top = 0;
    cl_pending_throw = 0;
    cl_dynbind_restore_to(0);
    cl_handler_top = 0;
    cl_restart_top = 0;
    if (cl_error_frame_top > 0) {
        cl_error_frame_top--;
        cl_error_frames[cl_error_frame_top].active = 0;
        longjmp(cl_error_frames[cl_error_frame_top].buf, CL_ERR_STORAGE);
    }
    /* No error frame — fatal */
    platform_write_string("FATAL: ");
    platform_write_string(cl_error_msg);
    platform_write_string("\n");
    exit(1);
}

void *cl_alloc(uint8_t type, uint32_t size)
{
    void *ptr;

    size = align_up(size);
    if (size < CL_MIN_ALLOC_SIZE)
        size = CL_MIN_ALLOC_SIZE;

    /* Try bump allocator first */
    ptr = alloc_from_bump(size);
    if (!ptr) {
        /* Try free list (may update size if using entire oversized block) */
        ptr = alloc_from_free_list(&size);
    }
    if (!ptr) {
        /* Run GC and retry */
        cl_gc();
        ptr = alloc_from_free_list(&size);
        if (!ptr) {
            ptr = alloc_from_bump(size);
        }
    }
    if (!ptr) {
        cl_storage_error("Heap exhausted (requested %u bytes)", (unsigned)size);
    }

    /* Guard: size must fit in the 23-bit header size field.
     * If this fires, an object exceeds ~8MB — architecturally unsupported. */
    if (size > CL_HDR_SIZE_MASK) {
        cl_storage_error("Allocation too large for header: %u bytes (max %u)",
                         (unsigned)size, (unsigned)CL_HDR_SIZE_MASK);
    }

    /* Initialize header */
    ((CL_Header *)ptr)->header = CL_MAKE_HDR(type, size);
    cl_heap.total_allocated += size;
    cl_heap.total_consed += size;

    return ptr;
}

/* --- Convenience allocators --- */

CL_Obj cl_cons(CL_Obj car, CL_Obj cdr)
{
    CL_Cons *c;

    /* Protect args from GC during allocation */
    CL_GC_PROTECT(car);
    CL_GC_PROTECT(cdr);

    c = (CL_Cons *)cl_alloc(TYPE_CONS, sizeof(CL_Cons));
    CL_GC_UNPROTECT(2);

    if (!c) return CL_NIL;
    c->car = car;
    c->cdr = cdr;
    return CL_PTR_TO_OBJ(c);
}

CL_Obj cl_make_string(const char *str, uint32_t len)
{
    uint32_t alloc_size = sizeof(CL_String) + len + 1;
    CL_String *s = (CL_String *)cl_alloc(TYPE_STRING, alloc_size);
    if (!s) return CL_NIL;
    s->length = len;
    if (str)
        memcpy(s->data, str, len);
    else
        memset(s->data, 0, len);
    s->data[len] = '\0';
    return CL_PTR_TO_OBJ(s);
}

CL_Obj cl_make_symbol(CL_Obj name)
{
    CL_Symbol *sym;

    CL_GC_PROTECT(name);
    sym = (CL_Symbol *)cl_alloc(TYPE_SYMBOL, sizeof(CL_Symbol));
    CL_GC_UNPROTECT(1);

    if (!sym) return CL_NIL;
    sym->name = name;
    sym->value = CL_UNBOUND;
    sym->function = CL_UNBOUND;
    sym->plist = CL_NIL;
    sym->package = CL_NIL;
    sym->hash = 0;
    sym->flags = 0;
    return CL_PTR_TO_OBJ(sym);
}

CL_Obj cl_make_function(CL_CFunc func, CL_Obj name, int min_args, int max_args)
{
    CL_Function *f;

    CL_GC_PROTECT(name);
    f = (CL_Function *)cl_alloc(TYPE_FUNCTION, sizeof(CL_Function));
    CL_GC_UNPROTECT(1);

    if (!f) return CL_NIL;
    f->func = func;
    f->name = name;
    f->min_args = min_args;
    f->max_args = max_args;
    return CL_PTR_TO_OBJ(f);
}

CL_Obj cl_make_vector(uint32_t length)
{
    uint32_t alloc_size = sizeof(CL_Vector) + length * sizeof(CL_Obj);
    CL_Vector *v = (CL_Vector *)cl_alloc(TYPE_VECTOR, alloc_size);
    if (!v) return CL_NIL;
    v->length = length;
    v->fill_pointer = CL_NO_FILL_POINTER;
    v->flags = 0;
    v->rank = 0;
    v->_reserved = 0;
    /* data[] already zeroed (= CL_NIL) by cl_alloc */
    return CL_PTR_TO_OBJ(v);
}

CL_Obj cl_make_array(uint32_t total, uint8_t rank, uint32_t *dims,
                     uint8_t flags, uint32_t fill_ptr)
{
    /* For multi-dim (rank>1): store dimensions in data[0..rank-1], elements at data[rank..] */
    uint32_t n_data = (rank > 1) ? (uint32_t)rank + total : total;
    uint32_t alloc_size;
    CL_Vector *v;
    /* Adjustable vectors need at least 1 data slot for displacement pointer */
    if ((flags & CL_VEC_FLAG_ADJUSTABLE) && n_data == 0)
        n_data = 1;
    alloc_size = sizeof(CL_Vector) + n_data * sizeof(CL_Obj);
    v = (CL_Vector *)cl_alloc(TYPE_VECTOR, alloc_size);
    if (!v) return CL_NIL;
    v->length = total;
    v->fill_pointer = fill_ptr;
    v->flags = flags;
    v->rank = rank;
    v->_reserved = 0;
    /* Store dimensions as fixnums for multi-dim */
    if (rank > 1 && dims) {
        uint8_t i;
        for (i = 0; i < rank; i++)
            v->data[i] = CL_MAKE_FIXNUM((int32_t)dims[i]);
    }
    /* Element slots already zeroed (= CL_NIL) by cl_alloc */
    return CL_PTR_TO_OBJ(v);
}

/* Follow displacement chain to get the actual data pointer */
CL_Obj *cl_vector_data_fn(CL_Vector *v)
{
    while (v->flags & CL_VEC_FLAG_DISPLACED)
        v = (CL_Vector *)CL_OBJ_TO_PTR(v->data[0]);
    return v->rank > 1 ? &v->data[v->rank] : v->data;
}

CL_Obj cl_make_hashtable(uint32_t bucket_count, uint32_t test)
{
    uint32_t alloc_size;
    CL_Hashtable *ht;

    /* Round up to power of 2 for fast bitmask indexing (avoids division) */
    if (bucket_count < 2) bucket_count = 2;
    {
        uint32_t p = 1;
        while (p < bucket_count) p <<= 1;
        bucket_count = p;
    }

    alloc_size = sizeof(CL_Hashtable) + bucket_count * sizeof(CL_Obj);
    ht = (CL_Hashtable *)cl_alloc(TYPE_HASHTABLE, alloc_size);
    if (!ht) return CL_NIL;
    ht->test = test;
    ht->count = 0;
    ht->bucket_count = bucket_count;
    /* buckets[] already zeroed (= CL_NIL) by cl_alloc */
    return CL_PTR_TO_OBJ(ht);
}

CL_Obj cl_make_condition(CL_Obj type_name, CL_Obj slots, CL_Obj report_string)
{
    CL_Condition *cond;

    CL_GC_PROTECT(type_name);
    CL_GC_PROTECT(slots);
    CL_GC_PROTECT(report_string);

    cond = (CL_Condition *)cl_alloc(TYPE_CONDITION, sizeof(CL_Condition));
    CL_GC_UNPROTECT(3);

    if (!cond) return CL_NIL;
    cond->type_name = type_name;
    cond->slots = slots;
    cond->report_string = report_string;
    return CL_PTR_TO_OBJ(cond);
}

CL_Obj cl_make_struct(CL_Obj type_name, uint32_t n_slots)
{
    uint32_t alloc_size = sizeof(CL_Struct) + n_slots * sizeof(CL_Obj);
    CL_Struct *st;

    CL_GC_PROTECT(type_name);
    st = (CL_Struct *)cl_alloc(TYPE_STRUCT, alloc_size);
    CL_GC_UNPROTECT(1);

    if (!st) return CL_NIL;
    st->type_desc = type_name;
    st->n_slots = n_slots;
    /* slots[] already zeroed (= CL_NIL) by cl_alloc */
    return CL_PTR_TO_OBJ(st);
}

CL_Obj cl_make_bignum(uint32_t n_limbs, uint32_t sign)
{
    uint32_t alloc_size = sizeof(CL_Bignum) + n_limbs * sizeof(uint16_t);
    CL_Bignum *bn = (CL_Bignum *)cl_alloc(TYPE_BIGNUM, alloc_size);
    if (!bn) return CL_NIL;
    bn->length = n_limbs;
    bn->sign = sign;
    /* limbs[] already zeroed by cl_alloc */
    return CL_PTR_TO_OBJ(bn);
}

CL_Obj cl_make_ratio(CL_Obj numerator, CL_Obj denominator)
{
    CL_Ratio *r;

    CL_GC_PROTECT(numerator);
    CL_GC_PROTECT(denominator);

    r = (CL_Ratio *)cl_alloc(TYPE_RATIO, sizeof(CL_Ratio));
    CL_GC_UNPROTECT(2);

    if (!r) return CL_NIL;
    r->numerator = numerator;
    r->denominator = denominator;
    return CL_PTR_TO_OBJ(r);
}

CL_Obj cl_make_complex(CL_Obj realpart, CL_Obj imagpart)
{
    CL_Complex *c;

    CL_GC_PROTECT(realpart);
    CL_GC_PROTECT(imagpart);

    c = (CL_Complex *)cl_alloc(TYPE_COMPLEX, sizeof(CL_Complex));
    CL_GC_UNPROTECT(2);

    if (!c) return CL_NIL;
    c->realpart = realpart;
    c->imagpart = imagpart;
    return CL_PTR_TO_OBJ(c);
}

CL_Obj cl_make_single_float(float value)
{
    CL_SingleFloat *sf = (CL_SingleFloat *)cl_alloc(TYPE_SINGLE_FLOAT,
                                                     sizeof(CL_SingleFloat));
    if (!sf) return CL_NIL;
    sf->value = value;
    return CL_PTR_TO_OBJ(sf);
}

CL_Obj cl_make_double_float(double value)
{
    CL_DoubleFloat *df = (CL_DoubleFloat *)cl_alloc(TYPE_DOUBLE_FLOAT,
                                                      sizeof(CL_DoubleFloat));
    if (!df) return CL_NIL;
    df->value = value;
    return CL_PTR_TO_OBJ(df);
}

CL_Obj cl_make_random_state(uint32_t seed)
{
    CL_RandomState *rs = (CL_RandomState *)cl_alloc(TYPE_RANDOM_STATE,
                                                      sizeof(CL_RandomState));
    if (!rs) return CL_NIL;
    /* Seed all 4 state words from seed using splitmix32-like mixing */
    {
        uint32_t z = seed;
        z = (z ^ (z >> 16)) * 0x45d9f3bU; z ^= z >> 16;
        rs->s[0] = z ? z : 1;
        z = (seed + 0x9e3779b9U);
        z = (z ^ (z >> 16)) * 0x45d9f3bU; z ^= z >> 16;
        rs->s[1] = z ? z : 1;
        z = (seed + 0x9e3779b9U * 2);
        z = (z ^ (z >> 16)) * 0x45d9f3bU; z ^= z >> 16;
        rs->s[2] = z ? z : 1;
        z = (seed + 0x9e3779b9U * 3);
        z = (z ^ (z >> 16)) * 0x45d9f3bU; z ^= z >> 16;
        rs->s[3] = z ? z : 1;
    }
    return CL_PTR_TO_OBJ(rs);
}

CL_Obj cl_make_bit_vector(uint32_t nbits)
{
    uint32_t nwords = CL_BV_WORDS(nbits);
    uint32_t alloc_size = sizeof(CL_BitVector) + nwords * sizeof(uint32_t);
    CL_BitVector *bv = (CL_BitVector *)cl_alloc(TYPE_BIT_VECTOR, alloc_size);
    if (!bv) return CL_NIL;
    bv->length = nbits;
    bv->fill_pointer = CL_NO_FILL_POINTER;
    bv->flags = 0;
    bv->_pad[0] = bv->_pad[1] = bv->_pad[2] = 0;
    /* data[] already zeroed by cl_alloc */
    return CL_PTR_TO_OBJ(bv);
}

CL_Obj cl_make_cell(CL_Obj value)
{
    CL_Cell *cell;

    CL_GC_PROTECT(value);
    cell = (CL_Cell *)cl_alloc(TYPE_CELL, sizeof(CL_Cell));
    CL_GC_UNPROTECT(1);

    if (!cell) return CL_NIL;
    cell->value = value;
    return CL_PTR_TO_OBJ(cell);
}

CL_Obj cl_make_pathname(CL_Obj host, CL_Obj device, CL_Obj directory,
                        CL_Obj name, CL_Obj type, CL_Obj version)
{
    CL_Pathname *pn;

    CL_GC_PROTECT(host);
    CL_GC_PROTECT(device);
    CL_GC_PROTECT(directory);
    CL_GC_PROTECT(name);
    CL_GC_PROTECT(type);
    CL_GC_PROTECT(version);

    pn = (CL_Pathname *)cl_alloc(TYPE_PATHNAME, sizeof(CL_Pathname));
    CL_GC_UNPROTECT(6);

    if (!pn) return CL_NIL;
    pn->host = host;
    pn->device = device;
    pn->directory = directory;
    pn->name = name;
    pn->type = type;
    pn->version = version;
    return CL_PTR_TO_OBJ(pn);
}

/* --- GC Root Stack --- */

void cl_gc_push_root(CL_Obj *root)
{
    if (gc_root_count < CL_GC_ROOT_STACK_SIZE) {
        gc_root_stack[gc_root_count++] = root;
    } else {
        fprintf(stderr, "FATAL: GC root stack overflow (%d/%d) — increase CL_GC_ROOT_STACK_SIZE\n",
                gc_root_count, CL_GC_ROOT_STACK_SIZE);
    }
}

void cl_gc_pop_roots(int n)
{
    gc_root_count -= n;
    if (gc_root_count < 0) gc_root_count = 0;
}

void cl_gc_reset_roots(void)
{
    gc_root_count = 0;
}

/* --- Mark Phase --- */

static void gc_mark_push(CL_Obj obj)
{
    /* Skip immediates and out-of-bounds */
    if (CL_NULL_P(obj) || CL_FIXNUM_P(obj) || CL_CHAR_P(obj))
        return;
    if (obj >= cl_heap.arena_size)
        return;

    /* Skip already-marked objects — avoids duplicate pushes and makes
     * overflow re-scan efficient (only pushes truly unmarked children) */
    if (CL_HDR_MARKED(CL_OBJ_TO_PTR(obj)))
        return;

    if (gc_mark_top < CL_GC_MARK_STACK_SIZE) {
        gc_mark_stack[gc_mark_top++] = obj;
    } else {
#ifdef DEBUG_GC
        if (!gc_mark_overflow) {
            platform_write_string("GC: mark stack overflow, will re-scan heap\n");
        }
#endif
        gc_mark_overflow = 1;
    }
}

static void gc_mark_children(void *ptr, uint8_t type)
{
    switch (type) {
    case TYPE_CONS: {
        CL_Cons *c = (CL_Cons *)ptr;
        gc_mark_push(c->car);
        gc_mark_push(c->cdr);
        break;
    }
    case TYPE_SYMBOL: {
        CL_Symbol *s = (CL_Symbol *)ptr;
        gc_mark_push(s->name);
        if (s->value != CL_UNBOUND) gc_mark_push(s->value);
        if (s->function != CL_UNBOUND) gc_mark_push(s->function);
        gc_mark_push(s->plist);
        gc_mark_push(s->package);
        break;
    }
    case TYPE_FUNCTION: {
        CL_Function *f = (CL_Function *)ptr;
        gc_mark_push(f->name);
        break;
    }
    case TYPE_CLOSURE: {
        CL_Closure *cl = (CL_Closure *)ptr;
        uint32_t size = CL_HDR_SIZE(ptr);
        uint32_t n_upvals = (size - sizeof(CL_Closure)) / sizeof(CL_Obj);
        uint32_t i;
        gc_mark_push(cl->bytecode);
        for (i = 0; i < n_upvals; i++)
            gc_mark_push(cl->upvalues[i]);
        break;
    }
    case TYPE_BYTECODE: {
        CL_Bytecode *bc = (CL_Bytecode *)ptr;
        uint16_t i;
        gc_mark_push(bc->name);
        for (i = 0; i < bc->n_constants; i++)
            gc_mark_push(bc->constants[i]);
        /* Mark keyword symbols if present */
        if (bc->key_syms) {
            for (i = 0; i < bc->n_keys; i++)
                gc_mark_push(bc->key_syms[i]);
        }
        break;
    }
    case TYPE_VECTOR: {
        CL_Vector *v = (CL_Vector *)ptr;
        uint32_t i;
        if (v->flags & CL_VEC_FLAG_DISPLACED) {
            /* Displaced: data[0] is the backing vector reference */
            gc_mark_push(v->data[0]);
        } else {
            /* For multi-dim: data[0..rank-1] are dim fixnums, elements at data[rank..] */
            uint32_t n_entries = (v->rank > 1) ? (uint32_t)v->rank + v->length : v->length;
            for (i = 0; i < n_entries; i++)
                gc_mark_push(v->data[i]);
        }
        break;
    }
    case TYPE_PACKAGE: {
        CL_Package *p = (CL_Package *)ptr;
        gc_mark_push(p->name);
        gc_mark_push(p->symbols);
        gc_mark_push(p->use_list);
        gc_mark_push(p->nicknames);
        gc_mark_push(p->local_nicknames);
        gc_mark_push(p->shadowing_symbols);
        break;
    }
    case TYPE_HASHTABLE: {
        CL_Hashtable *ht = (CL_Hashtable *)ptr;
        uint32_t i;
        for (i = 0; i < ht->bucket_count; i++)
            gc_mark_push(ht->buckets[i]);
        break;
    }
    case TYPE_CONDITION: {
        CL_Condition *cond = (CL_Condition *)ptr;
        gc_mark_push(cond->type_name);
        gc_mark_push(cond->slots);
        gc_mark_push(cond->report_string);
        break;
    }
    case TYPE_STRUCT: {
        CL_Struct *st = (CL_Struct *)ptr;
        uint32_t i;
        gc_mark_push(st->type_desc);
        for (i = 0; i < st->n_slots; i++)
            gc_mark_push(st->slots[i]);
        break;
    }
    case TYPE_STREAM: {
        CL_Stream *st = (CL_Stream *)ptr;
        gc_mark_push(st->string_buf);
        gc_mark_push(st->element_type);
        break;
    }
    case TYPE_RATIO: {
        CL_Ratio *r = (CL_Ratio *)ptr;
        gc_mark_push(r->numerator);
        gc_mark_push(r->denominator);
        break;
    }
    case TYPE_COMPLEX: {
        CL_Complex *cx = (CL_Complex *)ptr;
        gc_mark_push(cx->realpart);
        gc_mark_push(cx->imagpart);
        break;
    }
    case TYPE_PATHNAME: {
        CL_Pathname *pn = (CL_Pathname *)ptr;
        gc_mark_push(pn->host);
        gc_mark_push(pn->device);
        gc_mark_push(pn->directory);
        gc_mark_push(pn->name);
        gc_mark_push(pn->type);
        gc_mark_push(pn->version);
        break;
    }
    case TYPE_CELL: {
        CL_Cell *cell = (CL_Cell *)ptr;
        gc_mark_push(cell->value);
        break;
    }
    case TYPE_BIGNUM:
    case TYPE_SINGLE_FLOAT:
    case TYPE_DOUBLE_FLOAT:
    case TYPE_RANDOM_STATE:
    case TYPE_BIT_VECTOR:
        /* No children — raw numeric/state data */
        break;
    default:
        break;
    }
}

void gc_mark_obj(CL_Obj obj)
{
    void *ptr;
    if (CL_NULL_P(obj) || CL_FIXNUM_P(obj) || CL_CHAR_P(obj))
        return;

    /* Check if offset is within arena bounds */
    if (obj >= cl_heap.arena_size)
        return;

    ptr = CL_OBJ_TO_PTR(obj);

    if (CL_HDR_MARKED(ptr)) return;
    CL_HDR_SET_MARK(ptr);
    gc_mark_children(ptr, CL_HDR_TYPE(ptr));
}

static void gc_mark(void)
{
    int i;

    gc_mark_overflow = 0;

    /* Mark all roots directly (not via gc_mark_push).
     * gc_mark_obj immediately sets the mark bit then pushes children.
     * This is critical: if we used gc_mark_push for roots and the mark
     * stack overflowed, dropped roots would never be marked and the
     * heap re-scan couldn't recover them (it only processes children of
     * already-marked objects).  With gc_mark_obj, even if children
     * overflow, the root itself IS marked and recoverable by re-scan. */
    for (i = 0; i < gc_root_count; i++) {
        gc_mark_obj(*gc_root_stack[i]);
    }

    /* Mark package registry — transitively marks all packages, all symbols,
     * and all their values/functions/plists */
    gc_mark_obj(cl_package_registry);

    /* Mark compiler tables (alists not reachable through packages) */
    gc_mark_obj(macro_table);
    gc_mark_obj(setf_table);
    gc_mark_obj(setf_fn_table);
    gc_mark_obj(setf_expander_table);
    gc_mark_obj(type_table);
    gc_mark_obj(cl_clos_class_table);
    gc_mark_obj(struct_table);
    gc_mark_obj(condition_hierarchy);
    gc_mark_obj(condition_slot_table);

    /* Mark dynamic binding stack (saved old values) */
    for (i = 0; i < cl_dyn_top; i++) {
        gc_mark_obj(cl_dyn_stack[i].symbol);
        gc_mark_obj(cl_dyn_stack[i].old_value);
    }

    /* Mark NLX stack (catch tags, results, and saved bytecodes) */
    for (i = 0; i < cl_nlx_top; i++) {
        gc_mark_obj(cl_nlx_stack[i].tag);
        gc_mark_obj(cl_nlx_stack[i].result);
        gc_mark_obj(cl_nlx_stack[i].bytecode);
    }

    /* Mark handler stack */
    for (i = 0; i < cl_handler_top; i++) {
        gc_mark_obj(cl_handler_stack[i].type_name);
        gc_mark_obj(cl_handler_stack[i].handler);
    }

    /* Mark restart stack */
    for (i = 0; i < cl_restart_top; i++) {
        gc_mark_obj(cl_restart_stack[i].name);
        gc_mark_obj(cl_restart_stack[i].handler);
        gc_mark_obj(cl_restart_stack[i].tag);
    }

    /* Mark VM execution stack */
    if (cl_vm.stack) {
        for (i = 0; i < cl_vm.sp; i++) {
            gc_mark_obj(cl_vm.stack[i]);
        }
    }

    /* Mark bytecode objects referenced by active VM frames.
     * Stub bytecodes from cl_vm_apply are only reachable through
     * frame->bytecode — without this, GC sweeps them during nested calls. */
    for (i = 0; i < cl_vm.fp; i++) {
        gc_mark_obj(cl_vm.frames[i].bytecode);
    }

    /* Mark multiple values and pending throw state */
    for (i = 0; i < CL_MAX_MV; i++) {
        gc_mark_obj(cl_mv_values[i]);
    }
    gc_mark_obj(cl_pending_tag);
    gc_mark_obj(cl_pending_value);

    /* Mark readtable user macro closures */
    {
        int rt, ch;
        for (rt = 0; rt < CL_RT_POOL_SIZE; rt++) {
            if (!(cl_readtable_alloc_mask & (1u << rt)))
                continue;
            for (ch = 0; ch < CL_RT_CHARS; ch++) {
                if (!CL_NULL_P(cl_readtable_pool[rt].macro_fn[ch]))
                    gc_mark_obj(cl_readtable_pool[rt].macro_fn[ch]);
                if (!CL_NULL_P(cl_readtable_pool[rt].dispatch_fn[ch]))
                    gc_mark_obj(cl_readtable_pool[rt].dispatch_fn[ch]);
            }
        }
    }

    /* Mark compiler constants (active compilers may hold CL_Obj values
     * in platform_alloc'd memory not reachable from the GC arena) */
    {
        extern void cl_compiler_gc_mark(void);
        cl_compiler_gc_mark();
    }

    /* Mark VM-internal buffers (e.g. vm_extra_args during &rest processing) */
    {
        extern void cl_vm_gc_mark_extra(void);
        cl_vm_gc_mark_extra();
    }

    /* Drain mark stack iteratively (children pushed by gc_mark_obj above).
     * Do NOT clear gc_mark_overflow here — it may have been set during
     * root marking above, and the re-scan loop below must handle it. */
    while (gc_mark_top > 0) {
        CL_Obj obj = gc_mark_stack[--gc_mark_top];
        gc_mark_obj(obj);
    }

    /* Handle mark stack overflow: re-scan heap for marked objects whose
     * children may not have been pushed.  Repeat until no overflow. */
    while (gc_mark_overflow) {
        uint8_t *ptr = cl_heap.arena + CL_ALIGN;
        uint8_t *end = cl_heap.arena + cl_heap.bump;

        gc_mark_overflow = 0;
        while (ptr < end) {
            uint32_t size = CL_HDR_SIZE(ptr);
            if (size == 0) break;
            if (CL_HDR_MARKED(ptr)) {
                /* Re-push children — gc_mark_obj will skip already-marked ones */
                gc_mark_children(ptr, CL_HDR_TYPE(ptr));
            }
            ptr += size;
        }
        /* Drain anything newly pushed */
        while (gc_mark_top > 0) {
            CL_Obj obj = gc_mark_stack[--gc_mark_top];
            gc_mark_obj(obj);
        }
    }
}

/* --- Sweep Phase --- */

static void gc_sweep(void)
{
    uint8_t *ptr = cl_heap.arena + CL_ALIGN;  /* Skip offset 0 (reserved for NIL) */
    uint8_t *end = cl_heap.arena + cl_heap.bump;

    cl_heap.free_list = 0;
    cl_heap.total_allocated = 0;

    while (ptr < end) {
        uint32_t size = CL_HDR_SIZE(ptr);

        if (size == 0) break;  /* Safety: shouldn't happen */

        if (CL_HDR_MARKED(ptr)) {
            /* Live object — clear mark for next cycle */
            CL_HDR_CLR_MARK(ptr);
            cl_heap.total_allocated += size;
        } else {
            /* Dead object — finalize, then add to free list and coalesce.
             * Limit coalesced size to CL_HDR_SIZE_MASK (23 bits, ~8MB) because
             * free block's size field occupies the same position as the object
             * header, and the next sweep reads CL_HDR_SIZE() which masks to
             * 23 bits.  Blocks larger than that would be mis-parsed. */
            CL_FreeBlock *fb = (CL_FreeBlock *)ptr;
            uint32_t total = size;

            /* Finalize: release external resources for dead objects */
            if (CL_HDR_TYPE(ptr) == TYPE_STREAM) {
                CL_Stream *st = (CL_Stream *)ptr;
                if ((st->direction & CL_STREAM_OUTPUT) && st->out_buf_handle != 0)
                    cl_stream_free_outbuf(st->out_buf_handle);
            }

            /* Coalesce adjacent free blocks up to max representable size */
            while (ptr + total < end) {
                CL_Header *next = (CL_Header *)(ptr + total);
                uint32_t next_size = next->header & CL_HDR_SIZE_MASK;
                if (next_size == 0) break;
                if (next->header & CL_HDR_MARK_BIT) break;  /* Next is live */
                if (total + next_size > CL_HDR_SIZE_MASK) break;  /* Would overflow 23-bit size */
                /* Finalize the coalesced dead object too */
                if (CL_HDR_TYPE((uint8_t *)next) == TYPE_STREAM) {
                    CL_Stream *st = (CL_Stream *)next;
                    if ((st->direction & CL_STREAM_OUTPUT) && st->out_buf_handle != 0)
                        cl_stream_free_outbuf(st->out_buf_handle);
                }
                total += next_size;
            }

            fb->size = total;
            fb->next_offset = cl_heap.free_list;
            cl_heap.free_list = (uint32_t)(ptr - cl_heap.arena);
#ifdef DEBUG_GC
            /* Poison free block data after the 8-byte header (size +
             * next_offset) to detect use-after-free. */
            if (total > sizeof(CL_FreeBlock))
                memset((uint8_t *)fb + sizeof(CL_FreeBlock), 0xDE,
                       total - sizeof(CL_FreeBlock));
#endif
            size = total;  /* advance past entire coalesced region */
        }
        ptr += size;
    }
}

/* Post-GC verification: check all marked objects have valid children.
 * Must be called AFTER gc_mark() but BEFORE gc_sweep() (marks still set).
 * Reports any marked object that points to an unmarked heap object. */
#ifdef DEBUG_GC
static int gc_verify_errors;

static void gc_verify_check_ref(CL_Obj parent_offset, const char *field,
                                CL_Obj child)
{
    void *child_ptr;
    if (CL_NULL_P(child) || CL_FIXNUM_P(child) || CL_CHAR_P(child))
        return;
    if (child >= cl_heap.arena_size) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "GC VERIFY: marked @0x%08x.%s -> OUT OF BOUNDS 0x%08x (arena 0x%08x)\n",
                 (unsigned)parent_offset, field,
                 (unsigned)child, (unsigned)cl_heap.arena_size);
        platform_write_string(buf);
        gc_verify_errors++;
        return;
    }
    child_ptr = CL_OBJ_TO_PTR(child);
    if (!CL_HDR_MARKED(child_ptr)) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "GC VERIFY: marked @0x%08x.%s -> unmarked @0x%08x (type %u)\n",
                 (unsigned)parent_offset, field,
                 (unsigned)child, (unsigned)CL_HDR_TYPE(child_ptr));
        platform_write_string(buf);
        gc_verify_errors++;
    }
}

static void gc_verify_marked(void)
{
    uint8_t *ptr = cl_heap.arena + CL_ALIGN;
    uint8_t *end = cl_heap.arena + cl_heap.bump;
    gc_verify_errors = 0;

    while (ptr < end) {
        uint32_t size = CL_HDR_SIZE(ptr);
        if (size == 0) break;

        if (CL_HDR_MARKED(ptr)) {
            CL_Obj parent_off = (CL_Obj)(ptr - cl_heap.arena);
            uint8_t type = CL_HDR_TYPE(ptr);

            switch (type) {
            case TYPE_CONS: {
                CL_Cons *c = (CL_Cons *)ptr;
                gc_verify_check_ref(parent_off, "car", c->car);
                gc_verify_check_ref(parent_off, "cdr", c->cdr);
                break;
            }
            case TYPE_SYMBOL: {
                CL_Symbol *s = (CL_Symbol *)ptr;
                gc_verify_check_ref(parent_off, "name", s->name);
                if (s->value != CL_UNBOUND)
                    gc_verify_check_ref(parent_off, "value", s->value);
                if (s->function != CL_UNBOUND)
                    gc_verify_check_ref(parent_off, "function", s->function);
                gc_verify_check_ref(parent_off, "plist", s->plist);
                gc_verify_check_ref(parent_off, "package", s->package);
                break;
            }
            case TYPE_CLOSURE: {
                CL_Closure *cl = (CL_Closure *)ptr;
                uint32_t n = (size - sizeof(CL_Closure)) / sizeof(CL_Obj);
                uint32_t i;
                gc_verify_check_ref(parent_off, "bytecode", cl->bytecode);
                for (i = 0; i < n; i++)
                    gc_verify_check_ref(parent_off, "upval", cl->upvalues[i]);
                break;
            }
            case TYPE_BYTECODE: {
                CL_Bytecode *bc = (CL_Bytecode *)ptr;
                uint16_t i;
                gc_verify_check_ref(parent_off, "name", bc->name);
                for (i = 0; i < bc->n_constants; i++)
                    gc_verify_check_ref(parent_off, "const", bc->constants[i]);
                break;
            }
            case TYPE_VECTOR: {
                CL_Vector *v = (CL_Vector *)ptr;
                uint32_t i;
                if (v->flags & CL_VEC_FLAG_DISPLACED) {
                    gc_verify_check_ref(parent_off, "displaced", v->data[0]);
                } else {
                    uint32_t n = (v->rank > 1) ? (uint32_t)v->rank + v->length : v->length;
                    for (i = 0; i < n; i++)
                        gc_verify_check_ref(parent_off, "elt", v->data[i]);
                }
                break;
            }
            case TYPE_HASHTABLE: {
                CL_Hashtable *ht = (CL_Hashtable *)ptr;
                uint32_t i;
                for (i = 0; i < ht->bucket_count; i++)
                    gc_verify_check_ref(parent_off, "bucket", ht->buckets[i]);
                break;
            }
            case TYPE_STRUCT: {
                CL_Struct *st = (CL_Struct *)ptr;
                uint32_t i;
                gc_verify_check_ref(parent_off, "type_desc", st->type_desc);
                for (i = 0; i < st->n_slots; i++)
                    gc_verify_check_ref(parent_off, "slot", st->slots[i]);
                break;
            }
            case TYPE_CONDITION: {
                CL_Condition *cond = (CL_Condition *)ptr;
                gc_verify_check_ref(parent_off, "type_name", cond->type_name);
                gc_verify_check_ref(parent_off, "slots", cond->slots);
                gc_verify_check_ref(parent_off, "report", cond->report_string);
                break;
            }
            case TYPE_STREAM: {
                CL_Stream *st = (CL_Stream *)ptr;
                gc_verify_check_ref(parent_off, "string_buf", st->string_buf);
                gc_verify_check_ref(parent_off, "element_type", st->element_type);
                break;
            }
            case TYPE_FUNCTION: {
                CL_Function *f = (CL_Function *)ptr;
                gc_verify_check_ref(parent_off, "name", f->name);
                break;
            }
            case TYPE_RATIO: {
                CL_Ratio *r = (CL_Ratio *)ptr;
                gc_verify_check_ref(parent_off, "num", r->numerator);
                gc_verify_check_ref(parent_off, "den", r->denominator);
                break;
            }
            case TYPE_COMPLEX: {
                CL_Complex *cx = (CL_Complex *)ptr;
                gc_verify_check_ref(parent_off, "real", cx->realpart);
                gc_verify_check_ref(parent_off, "imag", cx->imagpart);
                break;
            }
            case TYPE_PATHNAME: {
                CL_Pathname *pn = (CL_Pathname *)ptr;
                gc_verify_check_ref(parent_off, "host", pn->host);
                gc_verify_check_ref(parent_off, "device", pn->device);
                gc_verify_check_ref(parent_off, "dir", pn->directory);
                gc_verify_check_ref(parent_off, "name", pn->name);
                gc_verify_check_ref(parent_off, "type", pn->type);
                gc_verify_check_ref(parent_off, "version", pn->version);
                break;
            }
            case TYPE_CELL: {
                CL_Cell *cell = (CL_Cell *)ptr;
                gc_verify_check_ref(parent_off, "value", cell->value);
                break;
            }
            case TYPE_PACKAGE: {
                CL_Package *p = (CL_Package *)ptr;
                gc_verify_check_ref(parent_off, "name", p->name);
                gc_verify_check_ref(parent_off, "symbols", p->symbols);
                gc_verify_check_ref(parent_off, "use_list", p->use_list);
                gc_verify_check_ref(parent_off, "nicknames", p->nicknames);
                gc_verify_check_ref(parent_off, "local_nicknames", p->local_nicknames);
                gc_verify_check_ref(parent_off, "shadowing", p->shadowing_symbols);
                break;
            }
            default:
                break;
            }
            if (gc_verify_errors > 20) {
                platform_write_string("GC VERIFY: too many errors, stopping\n");
                return;
            }
        }
        ptr += size;
    }
}

/* Check if an arena offset points to a freed block by testing for poison
 * fill at offset 8 (sizeof(CL_FreeBlock) = 8).  All freed blocks >= 16
 * bytes (CL_MIN_ALLOC_SIZE) have bytes 8-11 poisoned with 0xDE. */
static int gc_is_freed(uint32_t offset)
{
    uint8_t *p = cl_heap.arena + offset;
    /* Freed blocks have poison at offset 8 (after the 8-byte CL_FreeBlock header) */
    return (p[8] == 0xDE && p[9] == 0xDE && p[10] == 0xDE && p[11] == 0xDE);
}

/* Post-sweep verification: check that no live object contains a reference
 * to a freed block (use-after-free detection).  Uses poison fill pattern
 * to identify freed blocks (can't use type==0 since TYPE_CONS==0). */
static void gc_verify_after_sweep(void)
{
    uint8_t *ptr = cl_heap.arena + CL_ALIGN;
    uint8_t *end = cl_heap.arena + cl_heap.bump;
    int errors = 0;

    while (ptr < end) {
        uint32_t obj_size = CL_HDR_SIZE(ptr);
        CL_Obj parent_off = (CL_Obj)(ptr - cl_heap.arena);

        if (obj_size == 0) break;

        /* Skip free blocks */
        if (gc_is_freed(parent_off)) {
            ptr += obj_size;
            continue;
        }

        /* Live object — check CL_Obj fields for poison or dead refs */
        {
            uint8_t type = CL_HDR_TYPE(ptr);

            #define CHECK_FIELD(val, fname) do { \
                CL_Obj _v = (val); \
                if (_v == 0xDEDEDEDEu) { \
                    char buf[256]; \
                    snprintf(buf, sizeof(buf), \
                             "POST-SWEEP: @0x%08x.%s = POISON 0xDEDEDEDE (type %u)\n", \
                             (unsigned)parent_off, fname, (unsigned)type); \
                    platform_write_string(buf); \
                    errors++; \
                } else if (!CL_NULL_P(_v) && !CL_FIXNUM_P(_v) && !CL_CHAR_P(_v) \
                           && _v < cl_heap.arena_size && gc_is_freed(_v)) { \
                    char buf[256]; \
                    snprintf(buf, sizeof(buf), \
                             "POST-SWEEP: @0x%08x.%s -> freed @0x%08x (type %u)\n", \
                             (unsigned)parent_off, fname, (unsigned)_v, (unsigned)type); \
                    platform_write_string(buf); \
                    errors++; \
                } \
            } while(0)

            switch (type) {
            case TYPE_CONS: {
                CL_Cons *c = (CL_Cons *)ptr;
                CHECK_FIELD(c->car, "car");
                CHECK_FIELD(c->cdr, "cdr");
                break;
            }
            case TYPE_SYMBOL: {
                CL_Symbol *s = (CL_Symbol *)ptr;
                CHECK_FIELD(s->name, "name");
                if (s->value != CL_UNBOUND) CHECK_FIELD(s->value, "value");
                if (s->function != CL_UNBOUND) CHECK_FIELD(s->function, "function");
                CHECK_FIELD(s->plist, "plist");
                CHECK_FIELD(s->package, "package");
                break;
            }
            case TYPE_CLOSURE: {
                CL_Closure *cl = (CL_Closure *)ptr;
                uint32_t n = (obj_size - sizeof(CL_Closure)) / sizeof(CL_Obj);
                uint32_t i;
                CHECK_FIELD(cl->bytecode, "bytecode");
                for (i = 0; i < n; i++)
                    CHECK_FIELD(cl->upvalues[i], "upval");
                break;
            }
            case TYPE_BYTECODE: {
                CL_Bytecode *bc = (CL_Bytecode *)ptr;
                CHECK_FIELD(bc->name, "name");
                break;
            }
            case TYPE_VECTOR: {
                CL_Vector *v = (CL_Vector *)ptr;
                uint32_t i;
                if (v->flags & CL_VEC_FLAG_DISPLACED) {
                    CHECK_FIELD(v->data[0], "displaced");
                } else {
                    uint32_t n = (v->rank > 1) ? (uint32_t)v->rank + v->length : v->length;
                    for (i = 0; i < n; i++)
                        CHECK_FIELD(v->data[i], "elt");
                }
                break;
            }
            case TYPE_HASHTABLE: {
                CL_Hashtable *ht = (CL_Hashtable *)ptr;
                uint32_t i;
                for (i = 0; i < ht->bucket_count; i++)
                    CHECK_FIELD(ht->buckets[i], "bucket");
                break;
            }
            default:
                break;
            }
            #undef CHECK_FIELD

            if (errors > 10) {
                platform_write_string("POST-SWEEP: too many errors, stopping\n");
                return;
            }
        }
        ptr += obj_size;
    }

    if (errors > 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "POST-SWEEP: %d use-after-free errors detected\n", errors);
        platform_write_string(buf);
    }
}
#endif

void cl_gc(void)
{
#ifdef DEBUG_GC
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "GC #%lu: marking...\n",
                 (unsigned long)(cl_heap.gc_count + 1));
        platform_write_string(buf);
    }
#endif
    gc_mark();
#ifdef DEBUG_GC
    gc_verify_marked();
    /* After marking, find unmarked objects still referenced from VM stack.
     * Walk each VM stack slot: if it's a heap object that IS marked but is
     * a cons whose car or cdr points to an UNMARKED heap object, that child
     * should have been transitively marked.  This catches cases where a
     * marked cons references an unmarked child — indicating a marking bug. */
    {
        int si;
        char dbuf[256];
        for (si = 0; si < cl_vm.sp; si++) {
            CL_Obj v = cl_vm.stack[si];
            if (CL_NULL_P(v) || CL_FIXNUM_P(v) || CL_CHAR_P(v)) continue;
            if (v >= cl_heap.arena_size) continue;
            if (!CL_HDR_MARKED(CL_OBJ_TO_PTR(v))) {
                snprintf(dbuf, sizeof(dbuf),
                         "GC-DIAG: VM stack[%d]=0x%08x type=%d UNMARKED!\n",
                         si, (unsigned)v, CL_HDR_TYPE(CL_OBJ_TO_PTR(v)));
                platform_write_string(dbuf);
            }
        }
        /* Also check frame bytecodes: if a bytecode's constant references
         * an unmarked object, the constant wasn't transitively marked */
        for (si = 0; si < cl_vm.fp; si++) {
            CL_Obj bc_obj = cl_vm.frames[si].bytecode;
            if (CL_NULL_P(bc_obj) || CL_FIXNUM_P(bc_obj) || CL_CHAR_P(bc_obj)) continue;
            if (bc_obj >= cl_heap.arena_size) continue;
            if (!CL_HDR_MARKED(CL_OBJ_TO_PTR(bc_obj))) {
                snprintf(dbuf, sizeof(dbuf),
                         "GC-DIAG: frame[%d] bytecode=0x%08x UNMARKED!\n",
                         si, (unsigned)bc_obj);
                platform_write_string(dbuf);
            } else {
                void *ptr = CL_OBJ_TO_PTR(bc_obj);
                if (CL_HDR_TYPE(ptr) == TYPE_BYTECODE) {
                    CL_Bytecode *bc = (CL_Bytecode *)ptr;
                    int ci;
                    for (ci = 0; ci < bc->n_constants; ci++) {
                        CL_Obj cval = bc->constants[ci];
                        if (CL_NULL_P(cval) || CL_FIXNUM_P(cval) || CL_CHAR_P(cval)) continue;
                        if (cval >= cl_heap.arena_size) continue;
                        if (!CL_HDR_MARKED(CL_OBJ_TO_PTR(cval))) {
                            snprintf(dbuf, sizeof(dbuf),
                                     "GC-DIAG: frame[%d] bc=0x%08x const[%d]=0x%08x type=%d UNMARKED!\n",
                                     si, (unsigned)bc_obj, ci, (unsigned)cval,
                                     CL_HDR_TYPE(CL_OBJ_TO_PTR(cval)));
                            platform_write_string(dbuf);
                        }
                    }
                }
            }
        }
    }
#endif
    gc_sweep();
#ifdef DEBUG_GC
    gc_verify_after_sweep();
#endif
    cl_heap.gc_count++;
#ifdef DEBUG_GC
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "GC #%lu done: %lu/%lu bytes used\n",
                 (unsigned long)cl_heap.gc_count,
                 (unsigned long)cl_heap.total_allocated,
                 (unsigned long)cl_heap.arena_size);
        platform_write_string(buf);
    }
#endif
}

void cl_mem_stats(void)
{
    char buf[256];
    sprintf(buf, "Heap: %lu/%lu bytes used, %lu free, %lu GC cycles\n",
            (unsigned long)cl_heap.total_allocated,
            (unsigned long)cl_heap.arena_size,
            (unsigned long)(cl_heap.arena_size - cl_heap.total_allocated),
            (unsigned long)cl_heap.gc_count);
    platform_write_string(buf);
}
