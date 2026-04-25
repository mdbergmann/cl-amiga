#include "mem.h"
#include "error.h"
#include "float.h"
#include "vm.h"
#include "readtable.h"
#include "package.h"
#include "stream.h"
#include "../platform/platform.h"
#include "../platform/platform_thread.h"
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

/* Active compiler chain is accessed via cl_active_compiler macro (thread.h) */
typedef struct CL_Compiler_s CL_Compiler;

/* STW GC coordination (defined in thread.c) */
extern void cl_gc_stop_the_world(void);
extern void cl_gc_resume_the_world(void);

/* Allocation mutex — protects bump pointer, free list, and heap metadata */
static void *alloc_mutex = NULL;

CL_Heap cl_heap;
uint8_t *cl_arena_base = NULL;  /* Global arena base for offset↔pointer conversion */

/* GC root stack now lives in CL_Thread.
 * gc_root_count is a macro from thread.h.
 * gc_root_stack is a local macro below. */
#define gc_root_stack (CT->gc_roots)

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

/* Compaction forwarding table — maps old_offset/CL_ALIGN -> new_offset.
 * Allocated via platform_alloc during compaction, freed afterwards. */
static uint32_t *gc_fwd_table = NULL;
static uint32_t gc_fwd_table_entries = 0;

/* Track last GC cycle at which compaction ran — prevents infinite loops
 * when the heap is genuinely full (no fragmentation to reclaim). */
static uint32_t gc_last_compact_cycle = 0xFFFFFFFF;

/* Pending compaction flag — set when cl_alloc detects fragmentation,
 * cleared when compaction runs at a safe point.
 * Non-static: accessed by VM dispatch loop for safe-point checks. */
int gc_compact_pending = 0;

/* Global root registration table — static CL_Obj variables that must be
 * marked during GC and updated (forwarded) during compaction.
 * Used for cached interned keyword symbols, type symbols, etc. */
static CL_Obj *global_roots[CL_MAX_GLOBAL_ROOTS];
static int n_global_roots = 0;

void cl_gc_register_root(CL_Obj *root_ptr)
{
    if (n_global_roots < CL_MAX_GLOBAL_ROOTS)
        global_roots[n_global_roots++] = root_ptr;
}

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
    cl_heap.compact_count = 0;

    gc_root_count = 0;
    gc_mark_top = 0;

    /* Initialize allocation mutex */
    platform_mutex_init(&alloc_mutex);
}

void cl_mem_shutdown(void)
{
    if (cl_heap.arena) {
        platform_free(cl_heap.arena);
        cl_heap.arena = NULL;
    }
    if (alloc_mutex) {
        platform_mutex_destroy(alloc_mutex);
        alloc_mutex = NULL;
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
        /* Don't decrement here — CL_UNCATCH at the catch site pops */
        longjmp(cl_error_frames[cl_error_frame_top - 1].buf, CL_ERR_STORAGE);
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
    int multi = (cl_thread_count > 1);

    /* GC safepoint before allocation — if another thread initiated GC,
     * we must stop here before touching the heap */
    if (multi) CL_SAFEPOINT();

    size = align_up(size);
    if (size < CL_MIN_ALLOC_SIZE)
        size = CL_MIN_ALLOC_SIZE;

    if (multi) platform_mutex_lock(alloc_mutex);

    /* Try bump allocator first */
    ptr = alloc_from_bump(size);
    if (!ptr) {
        /* Try free list (may update size if using entire oversized block) */
        ptr = alloc_from_free_list(&size);
    }
    if (!ptr) {
        /* Need GC — release alloc_mutex so other threads can reach safepoints,
         * then run STW GC, then re-acquire and retry */
        if (multi) platform_mutex_unlock(alloc_mutex);
        cl_gc();
        if (multi) platform_mutex_lock(alloc_mutex);
        ptr = alloc_from_free_list(&size);
        if (!ptr) {
            ptr = alloc_from_bump(size);
        }
    }
    if (!ptr && gc_last_compact_cycle != cl_heap.gc_count) {
        /* Normal GC didn't help — try compaction to eliminate fragmentation.
         * First attempt: set pending flag for VM-level safe-point compaction.
         * If the VM dispatch loop runs compaction before we retry, great.
         * If not, we fall through to heap-exhausted error.
         *
         * NOTE: compaction is a moving GC.  All CL_Obj C locals that survive
         * across allocating calls MUST be GC-protected so compaction can
         * update them.  Raw C pointers derived from CL_Obj (e.g. via
         * CL_OBJ_TO_PTR) must be re-derived after any allocating call. */
        gc_compact_pending = 1;
        gc_last_compact_cycle = cl_heap.gc_count;
        if (multi) platform_mutex_unlock(alloc_mutex);
        cl_gc_compact();
        gc_compact_pending = 0;
        if (multi) platform_mutex_lock(alloc_mutex);
        ptr = alloc_from_bump(size);
    }
    if (!ptr) {
        if (multi) platform_mutex_unlock(alloc_mutex);
        cl_storage_error("Heap exhausted (requested %u bytes)", (unsigned)size);
    }

    /* Guard: size must fit in the 23-bit header size field.
     * If this fires, an object exceeds ~8MB — architecturally unsupported. */
    if (size > CL_HDR_SIZE_MASK) {
        if (multi) platform_mutex_unlock(alloc_mutex);
        cl_storage_error("Allocation too large for header: %u bytes (max %u)",
                         (unsigned)size, (unsigned)CL_HDR_SIZE_MASK);
    }

    /* Initialize: zero entire block, then set header.
     * Zeroing prevents stale data in padding/trailing bytes from being
     * misinterpreted by GC (e.g. closure padding read as upvalue slots)
     * or FASL serializer (traverses object graph by following CL_Obj fields). */
    memset(ptr, 0, size);
    ((CL_Header *)ptr)->header = CL_MAKE_HDR(type, size);
    cl_heap.total_allocated += size;
    cl_heap.total_consed += size;

    if (multi) platform_mutex_unlock(alloc_mutex);

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
    CL_String *s;
    char stack_buf[256];
    char *safe_str = NULL;

    /* If str points into the arena, copy to a safe buffer first.
     * cl_alloc below may trigger GC compaction which moves arena objects,
     * making the original pointer stale. */
    if (str && (const uint8_t *)str >= cl_heap.arena &&
        (const uint8_t *)str < cl_heap.arena + cl_heap.arena_size) {
        if (len < sizeof(stack_buf)) {
            memcpy(stack_buf, str, len);
            stack_buf[len] = '\0';
            safe_str = stack_buf;
        } else {
            safe_str = (char *)platform_alloc(len + 1);
            if (safe_str) {
                memcpy(safe_str, str, len);
                safe_str[len] = '\0';
            }
        }
        str = safe_str ? safe_str : str;
    }

    s = (CL_String *)cl_alloc(TYPE_STRING, alloc_size);
    if (!s) {
        if (safe_str && safe_str != stack_buf) platform_free(safe_str);
        return CL_NIL;
    }
    s->length = len;
    if (str)
        memcpy(s->data, str, len);
    else
        memset(s->data, 0, len);
    s->data[len] = '\0';

    if (safe_str && safe_str != stack_buf) platform_free(safe_str);
    return CL_PTR_TO_OBJ(s);
}

#ifdef CL_WIDE_STRINGS
CL_Obj cl_make_wide_string(const uint32_t *chars, uint32_t len)
{
    uint32_t alloc_size = sizeof(CL_WideString) + len * sizeof(uint32_t);
    CL_WideString *s = (CL_WideString *)cl_alloc(TYPE_WIDE_STRING, alloc_size);
    if (!s) return CL_NIL;
    s->length = len;
    if (chars)
        memcpy(s->data, chars, len * sizeof(uint32_t));
    else
        memset(s->data, 0, len * sizeof(uint32_t));
    return CL_PTR_TO_OBJ(s);
}
#endif

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
    /* Adjustable vectors need at least 2 data slots for displacement:
       data[0] = backing vector, data[1] = displaced-index-offset */
    if ((flags & CL_VEC_FLAG_ADJUSTABLE) && n_data < 2)
        n_data = 2;
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

/* Follow displacement chain to get the actual data pointer.
   Accumulates displaced-index-offset stored in data[1] at each level. */
CL_Obj *cl_vector_data_fn(CL_Vector *v)
{
    uint32_t offset = 0;
    while (v->flags & CL_VEC_FLAG_DISPLACED) {
        /* data[1] stores displaced-index-offset as a fixnum (0 for internal displacement) */
        if (CL_FIXNUM_P(v->data[1]))
            offset += (uint32_t)CL_FIXNUM_VAL(v->data[1]);
        v = (CL_Vector *)CL_OBJ_TO_PTR(v->data[0]);
    }
    {
        CL_Obj *base = v->rank > 1 ? &v->data[v->rank] : v->data;
        return base + offset;
    }
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
    ht->bucket_vec = CL_NIL;
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

CL_Obj cl_make_foreign_pointer(uint32_t address, uint32_t size, uint8_t flags)
{
    CL_ForeignPtr *fp = (CL_ForeignPtr *)cl_alloc(TYPE_FOREIGN_POINTER,
                                                    sizeof(CL_ForeignPtr));
    if (!fp) return CL_NIL;
    fp->address = address;
    fp->size = size;
    fp->flags = flags;
    fp->_pad[0] = fp->_pad[1] = fp->_pad[2] = 0;
    return CL_PTR_TO_OBJ(fp);
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
    if (gc_root_count > CL_GC_ROOT_STACK_SIZE || gc_root_count < 0) {
        fprintf(stderr, "[GC-ROOT-BUG] push_root: gc_root_count=%d is CORRUPT (max=%d)\n",
                gc_root_count, CL_GC_ROOT_STACK_SIZE);
        cl_capture_backtrace();
        fprintf(stderr, "%s", cl_backtrace_buf);
        fflush(stderr);
        abort();
    }
    if (gc_root_count < CL_GC_ROOT_STACK_SIZE) {
        gc_root_stack[gc_root_count++] = root;
    } else {
        fprintf(stderr, "FATAL: GC root stack overflow (%d/%d) — increase CL_GC_ROOT_STACK_SIZE\n",
                gc_root_count, CL_GC_ROOT_STACK_SIZE);
        cl_capture_backtrace();
        fprintf(stderr, "%s", cl_backtrace_buf);
        fflush(stderr);
        abort();
    }
}

void cl_gc_pop_roots(int n)
{
    if (gc_root_count > CL_GC_ROOT_STACK_SIZE || gc_root_count < 0) {
        fprintf(stderr, "[GC-ROOT-BUG] pop_roots(%d): gc_root_count=%d is CORRUPT (max=%d)\n",
                n, gc_root_count, CL_GC_ROOT_STACK_SIZE);
        cl_capture_backtrace();
        fprintf(stderr, "%s", cl_backtrace_buf);
        fflush(stderr);
        abort();
    }
    gc_root_count -= n;
    if (gc_root_count < 0) {
        fprintf(stderr, "[GC-ROOT-BUG] pop_roots(%d): gc_root_count went negative -> %d\n",
                n, gc_root_count);
        cl_capture_backtrace();
        fprintf(stderr, "%s", cl_backtrace_buf);
        fflush(stderr);
        abort();
    }
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
        gc_mark_push(ht->bucket_vec);
        if (!CL_NULL_P(ht->bucket_vec)) {
            /* Buckets in external vector — marking the vector marks its contents */
        } else {
            for (i = 0; i < ht->bucket_count; i++)
                gc_mark_push(ht->buckets[i]);
        }
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
    case TYPE_THREAD: {
        CL_ThreadObj *to = (CL_ThreadObj *)ptr;
        gc_mark_push(to->name);
        break;
    }
    case TYPE_LOCK: {
        CL_Lock *lk = (CL_Lock *)ptr;
        gc_mark_push(lk->name);
        break;
    }
    case TYPE_CONDVAR: {
        CL_CondVar *cv = (CL_CondVar *)ptr;
        gc_mark_push(cv->name);
        break;
    }
    case TYPE_FOREIGN_POINTER:
        /* No CL_Obj children */
        break;
    case TYPE_BIGNUM:
    case TYPE_SINGLE_FLOAT:
    case TYPE_DOUBLE_FLOAT:
    case TYPE_RANDOM_STATE:
    case TYPE_BIT_VECTOR:
#ifdef CL_WIDE_STRINGS
    case TYPE_WIDE_STRING:
#endif
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

/* Mark all per-thread roots for a single thread.
 * Called during STW GC — no locking needed, thread is stopped.
 *
 * We must #undef gc_root_count here because thread.h defines it as
 * (CT->gc_root_count), which collides with t->gc_root_count member access. */
#undef gc_root_count
static void gc_mark_thread_roots(CL_Thread *t)
{
    int i;

    /* GC root stack */
    for (i = 0; i < t->gc_root_count; i++) {
        gc_mark_obj(*t->gc_roots[i]);
    }

    /* Dynamic binding stack (saved old values) */
    for (i = 0; i < t->dyn_top; i++) {
        gc_mark_obj(t->dyn_stack[i].symbol);
        gc_mark_obj(t->dyn_stack[i].old_value);
    }

    /* NLX stack (catch tags, results, and saved bytecodes) */
    for (i = 0; i < t->nlx_top; i++) {
        gc_mark_obj(t->nlx_stack[i].tag);
        gc_mark_obj(t->nlx_stack[i].result);
        gc_mark_obj(t->nlx_stack[i].bytecode);
    }

    /* Handler stack */
    for (i = 0; i < t->handler_top; i++) {
        gc_mark_obj(t->handler_stack[i].type_name);
        gc_mark_obj(t->handler_stack[i].handler);
    }

    /* Restart stack */
    for (i = 0; i < t->restart_top; i++) {
        gc_mark_obj(t->restart_stack[i].name);
        gc_mark_obj(t->restart_stack[i].handler);
        gc_mark_obj(t->restart_stack[i].tag);
    }

    /* VM execution stack */
    if (t->vm.stack) {
        for (i = 0; i < t->vm.sp; i++) {
            gc_mark_obj(t->vm.stack[i]);
        }
    }

    /* Bytecode objects referenced by active VM frames */
    for (i = 0; i < t->vm.fp; i++) {
        gc_mark_obj(t->vm.frames[i].bytecode);
    }

    /* Multiple values and pending throw state */
    for (i = 0; i < CL_MAX_MV; i++) {
        gc_mark_obj(t->mv_values[i]);
    }
    gc_mark_obj(t->pending_tag);
    gc_mark_obj(t->pending_value);

    /* Thread metadata */
    gc_mark_obj(t->name);
    gc_mark_obj(t->result);
    gc_mark_obj(t->interrupt_func);

    /* Current lexical env installed for a macro expander — keeps the
     * &environment alist alive while the expander runs. */
    gc_mark_obj(t->current_lex_env);

    /* Reader state — in-flight reader stream plus per-read uninterned
     * symbol alist (so #:foo identity survives a GC during a long READ). */
    gc_mark_obj(t->rd_stream);
    gc_mark_obj(t->rd_uninterned);

    /* Compiler constants (active compilers may hold CL_Obj values
     * in platform_alloc'd memory not reachable from the GC arena) */
    {
        extern void cl_compiler_gc_mark_thread(CL_Thread *t);
        cl_compiler_gc_mark_thread(t);
    }

    /* VM-internal buffers (e.g. vm_extra_args during &rest processing) */
    {
        extern void cl_vm_gc_mark_extra_thread(CL_Thread *t);
        cl_vm_gc_mark_extra_thread(t);
    }

    /* Thread-Local Value (TLV) table — mark both symbol and value
     * for non-empty, non-tombstone entries */
    {
        int ti;
        for (ti = 0; ti < CL_TLV_TABLE_SIZE; ti++) {
            CL_Obj sym = t->tlv_table[ti].symbol;
            if (sym != CL_NIL && sym != CL_UNBOUND) {
                gc_mark_obj(sym);
                gc_mark_obj(t->tlv_table[ti].value);
            }
        }
    }
}
/* Restore gc_root_count macro for the rest of mem.c */
#define gc_root_count (CT->gc_root_count)

static void gc_mark(void)
{
    CL_Thread *t;

    gc_mark_overflow = 0;

    /* Mark all roots directly (not via gc_mark_push).
     * gc_mark_obj immediately sets the mark bit then pushes children.
     * This is critical: if we used gc_mark_push for roots and the mark
     * stack overflowed, dropped roots would never be marked and the
     * heap re-scan couldn't recover them (it only processes children of
     * already-marked objects).  With gc_mark_obj, even if children
     * overflow, the root itself IS marked and recoverable by re-scan. */

    /* Mark per-thread roots for ALL registered threads.
     * During STW GC, all other threads are stopped, so the thread list
     * is stable — no lock needed for iteration. */
    for (t = cl_thread_list; t; t = t->next) {
        gc_mark_thread_roots(t);
    }

    /* Mark shared globals (not per-thread) */

    /* Package registry — transitively marks all packages, all symbols,
     * and all their values/functions/plists */
    gc_mark_obj(cl_package_registry);

    /* Compiler tables (alists not reachable through packages) */
    gc_mark_obj(macro_table);
    gc_mark_obj(setf_table);
    gc_mark_obj(setf_fn_table);
    gc_mark_obj(setf_expander_table);
    gc_mark_obj(type_table);
    gc_mark_obj(cl_clos_class_table);
    gc_mark_obj(struct_table);
    gc_mark_obj(condition_hierarchy);
    gc_mark_obj(condition_slot_table);

    /* Thread system: main thread's Lisp object */
    {
        extern CL_Obj cl_main_thread_lisp_obj(void);
        CL_Obj mto = cl_main_thread_lisp_obj();
        if (!CL_NULL_P(mto)) gc_mark_obj(mto);
    }

    /* Readtable user macro closures */
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

    /* Registered global roots (cached keyword/type symbols, etc.) */
    {
        int gi;
        for (gi = 0; gi < n_global_roots; gi++)
            gc_mark_obj(*global_roots[gi]);
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

/* ================================================================
 * Compacting GC (Lisp-2 style sliding compaction)
 *
 * 4-pass algorithm:
 *   Pass 1: Mark (reuse existing gc_mark)
 *   Pass 2: Compute forwarding addresses
 *   Pass 3: Update all references (roots + heap objects)
 *   Pass 4: Slide live objects to their new positions
 *
 * Triggered when normal GC + free-list can't satisfy an allocation
 * (fragmentation is the bottleneck), or explicitly via cl_gc_compact().
 * ================================================================ */

/* Allocate / free forwarding table */
static int gc_fwd_alloc(void)
{
    gc_fwd_table_entries = cl_heap.bump / CL_ALIGN;
    gc_fwd_table = (uint32_t *)platform_alloc(
        gc_fwd_table_entries * sizeof(uint32_t));
    if (!gc_fwd_table) return 0;
    memset(gc_fwd_table, 0, gc_fwd_table_entries * sizeof(uint32_t));
    return 1;
}

static void gc_fwd_free(void)
{
    if (gc_fwd_table) {
        platform_free(gc_fwd_table);
        gc_fwd_table = NULL;
        gc_fwd_table_entries = 0;
    }
}

/* Pass 2: Walk arena linearly, assign forwarding addresses to marked objects */
static void gc_compute_forwarding(void)
{
    uint8_t *ptr = cl_heap.arena + CL_ALIGN;
    uint8_t *end = cl_heap.arena + cl_heap.bump;
    uint32_t new_offset = CL_ALIGN;  /* Skip offset 0 (NIL sentinel) */

    while (ptr < end) {
        uint32_t size = CL_HDR_SIZE(ptr);
        if (size == 0) break;

        if (CL_HDR_MARKED(ptr)) {
            uint32_t old_offset = (uint32_t)(ptr - cl_heap.arena);
            gc_fwd_table[old_offset / CL_ALIGN] = new_offset;
            new_offset += size;
        }
        ptr += size;
    }
}

/* Translate a CL_Obj through the forwarding table.
 * Returns obj unchanged if it's not a movable heap pointer. */
static CL_Obj gc_forward(CL_Obj obj)
{
    uint32_t idx, fwd;
    if (CL_NULL_P(obj) || CL_FIXNUM_P(obj) || CL_CHAR_P(obj))
        return obj;
    if (obj == CL_UNBOUND)
        return obj;
    if (obj >= cl_heap.bump)
        return obj;
    idx = obj / CL_ALIGN;
    if (idx >= gc_fwd_table_entries)
        return obj;
    fwd = gc_fwd_table[idx];
    return fwd ? fwd : obj;
}

/* Update a CL_Obj slot in place via forwarding table */
static void gc_update_slot(CL_Obj *slot)
{
    *slot = gc_forward(*slot);
}

/* Pass 3a: Update children of a single heap object (mirrors gc_mark_children) */
static void gc_update_children(void *ptr, uint8_t type)
{
    switch (type) {
    case TYPE_CONS: {
        CL_Cons *c = (CL_Cons *)ptr;
        gc_update_slot(&c->car);
        gc_update_slot(&c->cdr);
        break;
    }
    case TYPE_SYMBOL: {
        CL_Symbol *s = (CL_Symbol *)ptr;
        gc_update_slot(&s->name);
        if (s->value != CL_UNBOUND) gc_update_slot(&s->value);
        if (s->function != CL_UNBOUND) gc_update_slot(&s->function);
        gc_update_slot(&s->plist);
        gc_update_slot(&s->package);
        break;
    }
    case TYPE_FUNCTION: {
        CL_Function *f = (CL_Function *)ptr;
        gc_update_slot(&f->name);
        break;
    }
    case TYPE_CLOSURE: {
        CL_Closure *cl = (CL_Closure *)ptr;
        uint32_t size = CL_HDR_SIZE(ptr);
        uint32_t n_upvals = (size - sizeof(CL_Closure)) / sizeof(CL_Obj);
        uint32_t i;
        gc_update_slot(&cl->bytecode);
        for (i = 0; i < n_upvals; i++)
            gc_update_slot(&cl->upvalues[i]);
        break;
    }
    case TYPE_BYTECODE: {
        CL_Bytecode *bc = (CL_Bytecode *)ptr;
        uint16_t i;
        gc_update_slot(&bc->name);
        for (i = 0; i < bc->n_constants; i++)
            gc_update_slot(&bc->constants[i]);
        if (bc->key_syms) {
            for (i = 0; i < bc->n_keys; i++)
                gc_update_slot(&bc->key_syms[i]);
        }
        break;
    }
    case TYPE_VECTOR: {
        CL_Vector *v = (CL_Vector *)ptr;
        uint32_t i;
        if (v->flags & CL_VEC_FLAG_DISPLACED) {
            gc_update_slot(&v->data[0]);
        } else {
            uint32_t n_entries = (v->rank > 1) ? (uint32_t)v->rank + v->length : v->length;
            for (i = 0; i < n_entries; i++)
                gc_update_slot(&v->data[i]);
        }
        break;
    }
    case TYPE_PACKAGE: {
        CL_Package *p = (CL_Package *)ptr;
        gc_update_slot(&p->name);
        gc_update_slot(&p->symbols);
        gc_update_slot(&p->use_list);
        gc_update_slot(&p->nicknames);
        gc_update_slot(&p->local_nicknames);
        gc_update_slot(&p->shadowing_symbols);
        break;
    }
    case TYPE_HASHTABLE: {
        CL_Hashtable *ht = (CL_Hashtable *)ptr;
        uint32_t i;
        gc_update_slot(&ht->bucket_vec);
        if (!CL_NULL_P(ht->bucket_vec)) {
            /* External bucket vector — its contents updated when we walk that object */
        } else {
            for (i = 0; i < ht->bucket_count; i++)
                gc_update_slot(&ht->buckets[i]);
        }
        break;
    }
    case TYPE_CONDITION: {
        CL_Condition *cond = (CL_Condition *)ptr;
        gc_update_slot(&cond->type_name);
        gc_update_slot(&cond->slots);
        gc_update_slot(&cond->report_string);
        break;
    }
    case TYPE_STRUCT: {
        CL_Struct *st = (CL_Struct *)ptr;
        uint32_t i;
        gc_update_slot(&st->type_desc);
        for (i = 0; i < st->n_slots; i++)
            gc_update_slot(&st->slots[i]);
        break;
    }
    case TYPE_STREAM: {
        CL_Stream *st = (CL_Stream *)ptr;
        gc_update_slot(&st->string_buf);
        gc_update_slot(&st->element_type);
        break;
    }
    case TYPE_RATIO: {
        CL_Ratio *r = (CL_Ratio *)ptr;
        gc_update_slot(&r->numerator);
        gc_update_slot(&r->denominator);
        break;
    }
    case TYPE_COMPLEX: {
        CL_Complex *cx = (CL_Complex *)ptr;
        gc_update_slot(&cx->realpart);
        gc_update_slot(&cx->imagpart);
        break;
    }
    case TYPE_PATHNAME: {
        CL_Pathname *pn = (CL_Pathname *)ptr;
        gc_update_slot(&pn->host);
        gc_update_slot(&pn->device);
        gc_update_slot(&pn->directory);
        gc_update_slot(&pn->name);
        gc_update_slot(&pn->type);
        gc_update_slot(&pn->version);
        break;
    }
    case TYPE_CELL: {
        CL_Cell *cell = (CL_Cell *)ptr;
        gc_update_slot(&cell->value);
        break;
    }
    case TYPE_THREAD: {
        CL_ThreadObj *to = (CL_ThreadObj *)ptr;
        gc_update_slot(&to->name);
        break;
    }
    case TYPE_LOCK: {
        CL_Lock *lk = (CL_Lock *)ptr;
        gc_update_slot(&lk->name);
        break;
    }
    case TYPE_CONDVAR: {
        CL_CondVar *cv = (CL_CondVar *)ptr;
        gc_update_slot(&cv->name);
        break;
    }
    case TYPE_STRING:
    case TYPE_BIGNUM:
    case TYPE_SINGLE_FLOAT:
    case TYPE_DOUBLE_FLOAT:
    case TYPE_RANDOM_STATE:
    case TYPE_BIT_VECTOR:
    case TYPE_FOREIGN_POINTER:
#ifdef CL_WIDE_STRINGS
    case TYPE_WIDE_STRING:
#endif
        break;
    default:
        break;
    }
}

/* Pass 3b: Update per-thread roots (mirrors gc_mark_thread_roots).
 * Must #undef gc_root_count to avoid macro collision with t->gc_root_count. */
#undef gc_root_count
static void gc_update_thread_roots(CL_Thread *t)
{
    int i;

    /* GC root stack — CL_Obj* pointers to C stack variables */
    for (i = 0; i < t->gc_root_count; i++)
        gc_update_slot(t->gc_roots[i]);

    /* Dynamic binding stack */
    for (i = 0; i < t->dyn_top; i++) {
        gc_update_slot(&t->dyn_stack[i].symbol);
        gc_update_slot(&t->dyn_stack[i].old_value);
    }

    /* NLX stack */
    for (i = 0; i < t->nlx_top; i++) {
        gc_update_slot(&t->nlx_stack[i].tag);
        gc_update_slot(&t->nlx_stack[i].result);
        gc_update_slot(&t->nlx_stack[i].bytecode);
    }

    /* Handler stack */
    for (i = 0; i < t->handler_top; i++) {
        gc_update_slot(&t->handler_stack[i].type_name);
        gc_update_slot(&t->handler_stack[i].handler);
    }

    /* Restart stack */
    for (i = 0; i < t->restart_top; i++) {
        gc_update_slot(&t->restart_stack[i].name);
        gc_update_slot(&t->restart_stack[i].handler);
        gc_update_slot(&t->restart_stack[i].tag);
    }

    /* VM execution stack */
    if (t->vm.stack) {
        for (i = 0; i < t->vm.sp; i++)
            gc_update_slot(&t->vm.stack[i]);
    }

    /* VM frame bytecodes */
    for (i = 0; i < t->vm.fp; i++)
        gc_update_slot(&t->vm.frames[i].bytecode);

    /* Multiple values and pending throw state */
    for (i = 0; i < CL_MAX_MV; i++)
        gc_update_slot(&t->mv_values[i]);
    gc_update_slot(&t->pending_tag);
    gc_update_slot(&t->pending_value);

    /* Thread metadata */
    gc_update_slot(&t->name);
    gc_update_slot(&t->result);
    gc_update_slot(&t->interrupt_func);
    gc_update_slot(&t->current_lex_env);

    /* Compiler constants (platform_alloc'd, hold CL_Obj refs) */
    {
        extern void cl_compiler_gc_update_thread(CL_Thread *t,
                                                  void (*update_fn)(CL_Obj *));
        cl_compiler_gc_update_thread(t, gc_update_slot);
    }

    /* VM extra args buffer */
    {
        extern void cl_vm_gc_update_extra_thread(CL_Thread *t,
                                                  void (*update_fn)(CL_Obj *));
        cl_vm_gc_update_extra_thread(t, gc_update_slot);
    }

    /* TLV table */
    {
        int ti;
        for (ti = 0; ti < CL_TLV_TABLE_SIZE; ti++) {
            CL_Obj sym = t->tlv_table[ti].symbol;
            if (sym != CL_NIL && sym != CL_UNBOUND) {
                gc_update_slot(&t->tlv_table[ti].symbol);
                gc_update_slot(&t->tlv_table[ti].value);
            }
        }
    }
}
#define gc_root_count (CT->gc_root_count)

/* Pass 3c: Update shared (non-per-thread) roots (mirrors gc_mark shared section) */
static void gc_update_shared_roots(void)
{
    /* Package registry */
    gc_update_slot(&cl_package_registry);

    /* Compiler tables */
    gc_update_slot(&macro_table);
    gc_update_slot(&setf_table);
    gc_update_slot(&setf_fn_table);
    gc_update_slot(&setf_expander_table);
    gc_update_slot(&type_table);
    gc_update_slot(&cl_clos_class_table);
    gc_update_slot(&struct_table);
    gc_update_slot(&condition_hierarchy);
    gc_update_slot(&condition_slot_table);

    /* Main thread Lisp object */
    {
        extern CL_Obj *cl_main_thread_lisp_obj_ptr(void);
        CL_Obj *ptr = cl_main_thread_lisp_obj_ptr();
        if (ptr) gc_update_slot(ptr);
    }

    /* Readtable user macro closures */
    {
        int rt, ch;
        for (rt = 0; rt < CL_RT_POOL_SIZE; rt++) {
            if (!(cl_readtable_alloc_mask & (1u << rt)))
                continue;
            for (ch = 0; ch < CL_RT_CHARS; ch++) {
                gc_update_slot(&cl_readtable_pool[rt].macro_fn[ch]);
                gc_update_slot(&cl_readtable_pool[rt].dispatch_fn[ch]);
            }
        }
    }

    /* Registered global roots (cached keyword/type symbols, etc.) */
    {
        int gi;
        for (gi = 0; gi < n_global_roots; gi++)
            gc_update_slot(global_roots[gi]);
    }
}

/* Pass 3: Walk all live heap objects + all roots and update references */
static void gc_update_all_references(void)
{
    uint8_t *ptr, *end;
    CL_Thread *t;

    /* Update per-thread roots */
    for (t = cl_thread_list; t; t = t->next)
        gc_update_thread_roots(t);

    /* Update shared globals */
    gc_update_shared_roots();

    /* Walk all live heap objects and update their children */
    ptr = cl_heap.arena + CL_ALIGN;
    end = cl_heap.arena + cl_heap.bump;
    while (ptr < end) {
        uint32_t size = CL_HDR_SIZE(ptr);
        if (size == 0) break;
        if (CL_HDR_MARKED(ptr))
            gc_update_children(ptr, CL_HDR_TYPE(ptr));
        ptr += size;
    }
}

/* Pass 4: Slide live objects to their forwarding addresses.
 * Objects only move downward (or stay), so forward copy is safe. */
static void gc_slide(void)
{
    uint8_t *ptr = cl_heap.arena + CL_ALIGN;
    uint8_t *end = cl_heap.arena + cl_heap.bump;
    uint32_t new_bump = CL_ALIGN;

    while (ptr < end) {
        uint32_t size = CL_HDR_SIZE(ptr);
        if (size == 0) break;

        if (CL_HDR_MARKED(ptr)) {
            uint32_t old_offset = (uint32_t)(ptr - cl_heap.arena);
            uint32_t new_offset = gc_fwd_table[old_offset / CL_ALIGN];

            /* Clear mark bit before copying */
            CL_HDR_CLR_MARK(ptr);

            if (new_offset != old_offset)
                memmove(cl_heap.arena + new_offset, ptr, size);
            new_bump = new_offset + size;
        } else {
            /* Dead object — finalize streams before overwriting */
            if (CL_HDR_TYPE(ptr) == TYPE_STREAM) {
                CL_Stream *st = (CL_Stream *)ptr;
                if ((st->direction & CL_STREAM_OUTPUT) && st->out_buf_handle != 0)
                    cl_stream_free_outbuf(st->out_buf_handle);
            }
        }
        ptr += size;
    }

    cl_heap.bump = new_bump;
    cl_heap.free_list = 0;  /* No fragmentation after compaction */
    cl_heap.total_allocated = new_bump - CL_ALIGN;
}

/* Rehash a single eq hash table after compaction (no allocation) */
static void gc_rehash_eq_table(CL_Hashtable *ht)
{
    CL_Obj *bkts;
    uint32_t bucket_count = ht->bucket_count;
    uint32_t i;
    CL_Obj all_entries = CL_NIL;

    /* Get bucket array */
    if (!CL_NULL_P(ht->bucket_vec)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(ht->bucket_vec);
        bkts = v->data;
    } else {
        bkts = ht->buckets;
    }

    /* Collect all entries into a single linked list, clear buckets */
    for (i = 0; i < bucket_count; i++) {
        CL_Obj chain = bkts[i];
        while (!CL_NULL_P(chain)) {
            CL_Cons *entry = (CL_Cons *)CL_OBJ_TO_PTR(chain);
            CL_Obj next = entry->cdr;
            entry->cdr = all_entries;
            all_entries = chain;
            chain = next;
        }
        bkts[i] = CL_NIL;
    }

    /* Redistribute using new identity hashes */
    while (!CL_NULL_P(all_entries)) {
        CL_Cons *entry = (CL_Cons *)CL_OBJ_TO_PTR(all_entries);
        CL_Obj next = entry->cdr;
        CL_Obj pair = entry->car;
        CL_Obj key = ((CL_Cons *)CL_OBJ_TO_PTR(pair))->car;
        /* hash_mix for eq: identity hash on the CL_Obj value */
        uint32_t h = key;
        h ^= h >> 16;
        h *= 0x45d9f3bU;
        h ^= h >> 16;
        {
            uint32_t idx = h & (bucket_count - 1);
            entry->cdr = bkts[idx];
            bkts[idx] = all_entries;
        }
        all_entries = next;
    }
}

/* Rehash ALL eq hash tables in the arena after compaction */
static void gc_rehash_eq_tables(void)
{
    uint8_t *ptr = cl_heap.arena + CL_ALIGN;
    uint8_t *end = cl_heap.arena + cl_heap.bump;

    while (ptr < end) {
        uint32_t size = CL_HDR_SIZE(ptr);
        if (size == 0) break;
        if (CL_HDR_TYPE(ptr) == TYPE_HASHTABLE) {
            CL_Hashtable *ht = (CL_Hashtable *)ptr;
            if (ht->test == CL_HT_TEST_EQ && ht->count > 0)
                gc_rehash_eq_table(ht);
        }
        ptr += size;
    }
}

/* Run compaction if pending (called from safe points). */
void cl_gc_compact_if_pending(void)
{
    if (gc_compact_pending) {
        gc_compact_pending = 0;
        gc_last_compact_cycle = cl_heap.gc_count;
        cl_gc_compact();
    }
}

/* Main compaction entry point */
void cl_gc_compact(void)
{
    int multithread = (cl_thread_count > 1);

    if (multithread)
        cl_gc_stop_the_world();

#ifdef DEBUG_GC
    platform_write_string("GC: compaction starting...\n");
#endif

    /* Pass 1: Mark (standard) */
    gc_mark();

    /* Allocate forwarding table */
    if (!gc_fwd_alloc()) {
#ifdef DEBUG_GC
        platform_write_string("GC: compact failed (no memory for fwd table), "
                              "falling back to sweep\n");
#endif
        gc_sweep();
        cl_heap.gc_count++;
        if (multithread) cl_gc_resume_the_world();
        return;
    }

    /* Pass 2: Compute forwarding addresses */
    gc_compute_forwarding();

    /* Pass 3: Update all references */
    gc_update_all_references();

    /* Pass 4: Slide objects */
    gc_slide();

    /* Clean up forwarding table */
    gc_fwd_free();

    /* Rehash eq hash tables (object identity changed) */
    gc_rehash_eq_tables();

    cl_heap.gc_count++;
    cl_heap.compact_count++;

#ifdef DEBUG_GC
    /* Post-compaction verification: check all live objects have valid refs */
    {
        uint8_t *vptr = cl_heap.arena + CL_ALIGN;
        uint8_t *vend = cl_heap.arena + cl_heap.bump;
        int vc_errs = 0;
        char vbuf[256];
        while (vptr < vend && vc_errs < 5) {
            uint32_t vsize = CL_HDR_SIZE(vptr);
            uint8_t vtype = CL_HDR_TYPE(vptr);
            uint32_t voff = (uint32_t)(vptr - cl_heap.arena);
            if (vsize == 0) break;
            if (vtype == TYPE_CONS) {
                CL_Cons *c = (CL_Cons *)vptr;
                if (!CL_NULL_P(c->car) && !CL_FIXNUM_P(c->car) && !CL_CHAR_P(c->car)
                    && c->car != CL_UNBOUND && c->car >= cl_heap.bump) {
                    snprintf(vbuf, sizeof(vbuf),
                        "COMPACT-VERIFY: @0x%08x.car -> 0x%08x (OOB, bump=0x%08x)\n",
                        (unsigned)voff, (unsigned)c->car, (unsigned)cl_heap.bump);
                    platform_write_string(vbuf);
                    vc_errs++;
                }
                if (!CL_NULL_P(c->cdr) && !CL_FIXNUM_P(c->cdr) && !CL_CHAR_P(c->cdr)
                    && c->cdr != CL_UNBOUND && c->cdr >= cl_heap.bump) {
                    snprintf(vbuf, sizeof(vbuf),
                        "COMPACT-VERIFY: @0x%08x.cdr -> 0x%08x (OOB, bump=0x%08x)\n",
                        (unsigned)voff, (unsigned)c->cdr, (unsigned)cl_heap.bump);
                    platform_write_string(vbuf);
                    vc_errs++;
                }
            } else if (vtype == TYPE_VECTOR) {
                CL_Vector *v = (CL_Vector *)vptr;
                uint32_t vi;
                uint32_t nelt = (v->flags & CL_VEC_FLAG_DISPLACED) ? 1 :
                                ((v->rank > 1) ? (uint32_t)v->rank + v->length : v->length);
                for (vi = 0; vi < nelt && vc_errs < 5; vi++) {
                    CL_Obj elt = v->data[vi];
                    if (!CL_NULL_P(elt) && !CL_FIXNUM_P(elt) && !CL_CHAR_P(elt)
                        && elt != CL_UNBOUND && (elt & CL_TAG_MASK_LO2) == 0
                        && elt >= cl_heap.bump) {
                        snprintf(vbuf, sizeof(vbuf),
                            "COMPACT-VERIFY: @0x%08x vec[%u] -> 0x%08x (OOB, bump=0x%08x)\n",
                            (unsigned)voff, (unsigned)vi, (unsigned)elt,
                            (unsigned)cl_heap.bump);
                        platform_write_string(vbuf);
                        vc_errs++;
                    }
                }
            }
            vptr += vsize;
        }
    }
#endif

#ifdef DEBUG_GC
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "GC: compact done: %lu/%lu bytes used\n",
                 (unsigned long)cl_heap.total_allocated,
                 (unsigned long)cl_heap.arena_size);
        platform_write_string(buf);
    }
#endif

    if (multithread)
        cl_gc_resume_the_world();
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
                gc_verify_check_ref(parent_off, "bucket_vec", ht->bucket_vec);
                if (CL_NULL_P(ht->bucket_vec)) {
                    for (i = 0; i < ht->bucket_count; i++)
                        gc_verify_check_ref(parent_off, "bucket", ht->buckets[i]);
                }
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
                CHECK_FIELD(ht->bucket_vec, "bucket_vec");
                if (CL_NULL_P(ht->bucket_vec)) {
                    for (i = 0; i < ht->bucket_count; i++)
                        CHECK_FIELD(ht->buckets[i], "bucket");
                }
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
    int multithread = (cl_thread_count > 1);

    /* Stop all other threads if multi-threaded */
    if (multithread)
        cl_gc_stop_the_world();

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

    /* Resume all stopped threads */
    if (multithread)
        cl_gc_resume_the_world();
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
