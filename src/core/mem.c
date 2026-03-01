#include "mem.h"
#include "vm.h"
#include "../platform/platform.h"
#include <string.h>
#include <stdio.h>

CL_Heap cl_heap;
uint8_t *cl_arena_base = NULL;  /* Global arena base for offset↔pointer conversion */

/* GC root stack — stores pointers to CL_Obj variables */
static CL_Obj *gc_root_stack[CL_GC_ROOT_STACK_SIZE];
static int gc_root_count = 0;

/* GC mark stack (iterative marking) */
static CL_Obj gc_mark_stack[CL_GC_MARK_STACK_SIZE];
static int gc_mark_top = 0;

/* Forward declarations */
static void gc_mark(void);
static void gc_sweep(void);
static void gc_mark_obj(CL_Obj obj);
static void gc_mark_push(CL_Obj obj);
static void *alloc_from_free_list(uint32_t size);
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
    cl_heap.free_list = NULL;
    cl_heap.total_allocated = 0;
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

static void *alloc_from_free_list(uint32_t size)
{
    CL_FreeBlock **prev = &cl_heap.free_list;
    CL_FreeBlock *block = cl_heap.free_list;

    while (block) {
        if (block->size >= size) {
            uint32_t remainder = block->size - size;
            if (remainder >= CL_MIN_ALLOC_SIZE) {
                /* Split block */
                CL_FreeBlock *new_free = (CL_FreeBlock *)((uint8_t *)block + size);
                new_free->size = remainder;
                new_free->next = block->next;
                *prev = new_free;
            } else {
                /* Use entire block */
                size = block->size;
                *prev = block->next;
            }
            memset(block, 0, size);
            return block;
        }
        prev = &block->next;
        block = block->next;
    }
    return NULL;
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
        /* Try free list */
        ptr = alloc_from_free_list(size);
    }
    if (!ptr) {
        /* Run GC and retry */
        cl_gc();
        ptr = alloc_from_free_list(size);
        if (!ptr) {
            ptr = alloc_from_bump(size);
        }
    }
    if (!ptr) {
        platform_write_string("ERROR: Heap exhausted\n");
        return NULL;
    }

    /* Initialize header */
    ((CL_Header *)ptr)->header = CL_MAKE_HDR(type, size);
    cl_heap.total_allocated += size;

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
    memcpy(s->data, str, len);
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
    /* data[] already zeroed (= CL_NIL) by cl_alloc */
    return CL_PTR_TO_OBJ(v);
}

/* --- GC Root Stack --- */

void cl_gc_push_root(CL_Obj *root)
{
    if (gc_root_count < CL_GC_ROOT_STACK_SIZE) {
        gc_root_stack[gc_root_count++] = root;
    }
}

void cl_gc_pop_roots(int n)
{
    gc_root_count -= n;
    if (gc_root_count < 0) gc_root_count = 0;
}

/* --- Mark Phase --- */

static void gc_mark_push(CL_Obj obj)
{
    if (gc_mark_top < CL_GC_MARK_STACK_SIZE) {
        gc_mark_stack[gc_mark_top++] = obj;
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
        for (i = 0; i < v->length; i++)
            gc_mark_push(v->data[i]);
        break;
    }
    case TYPE_PACKAGE: {
        CL_Package *p = (CL_Package *)ptr;
        gc_mark_push(p->name);
        gc_mark_push(p->symbols);
        gc_mark_push(p->use_list);
        break;
    }
    default:
        break;
    }
}

static void gc_mark_obj(CL_Obj obj)
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

    /* Push all roots */
    for (i = 0; i < gc_root_count; i++) {
        gc_mark_push(*gc_root_stack[i]);
    }

    /* Mark dynamic binding stack (saved old values) */
    for (i = 0; i < cl_dyn_top; i++) {
        gc_mark_push(cl_dyn_stack[i].symbol);
        gc_mark_push(cl_dyn_stack[i].old_value);
    }

    /* Drain mark stack iteratively */
    while (gc_mark_top > 0) {
        CL_Obj obj = gc_mark_stack[--gc_mark_top];
        gc_mark_obj(obj);
    }
}

/* --- Sweep Phase --- */

static void gc_sweep(void)
{
    uint8_t *ptr = cl_heap.arena;
    uint8_t *end = cl_heap.arena + cl_heap.bump;

    cl_heap.free_list = NULL;
    cl_heap.total_allocated = 0;

    while (ptr < end) {
        uint32_t size = CL_HDR_SIZE(ptr);

        if (size == 0) break;  /* Safety: shouldn't happen */

        if (CL_HDR_MARKED(ptr)) {
            /* Live object — clear mark for next cycle */
            CL_HDR_CLR_MARK(ptr);
            cl_heap.total_allocated += size;
        } else {
            /* Dead object — add to free list, coalesce with next if possible */
            CL_FreeBlock *fb = (CL_FreeBlock *)ptr;
            uint32_t total = size;

            /* Coalesce adjacent free blocks */
            while (ptr + total < end) {
                CL_Header *next = (CL_Header *)(ptr + total);
                uint32_t next_size = next->header & CL_HDR_SIZE_MASK;
                if (next_size == 0) break;
                if (next->header & CL_HDR_MARK_BIT) break;  /* Next is live */
                total += next_size;
            }

            fb->size = total;
            fb->next = cl_heap.free_list;
            cl_heap.free_list = fb;
        }
        ptr += size;
    }
}

void cl_gc(void)
{
    gc_mark();
    gc_sweep();
    cl_heap.gc_count++;
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
