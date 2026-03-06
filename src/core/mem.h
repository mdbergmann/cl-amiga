#ifndef CL_MEM_H
#define CL_MEM_H

#include "types.h"

/*
 * Arena allocator with mark-and-sweep GC.
 *
 * Single contiguous arena, bump allocation with free-list fallback.
 * Mark phase uses explicit stack (no C recursion).
 * Sweep phase coalesces adjacent free blocks.
 */

#define CL_DEFAULT_HEAP_SIZE  (4 * 1024 * 1024)  /* 4MB */
#define CL_GC_MARK_STACK_SIZE 4096
#define CL_GC_ROOT_STACK_SIZE 256
#define CL_MIN_ALLOC_SIZE     16  /* Minimum allocation (aligned) */
#define CL_ALIGN              4   /* 4-byte alignment */

/* Free block header (overlays CL_Header in freed objects) */
typedef struct CL_FreeBlock {
    uint32_t size;              /* Total size including this header */
    struct CL_FreeBlock *next;  /* Next free block */
} CL_FreeBlock;

/* Heap state */
typedef struct {
    uint8_t *arena;             /* Base of arena */
    uint32_t arena_size;        /* Total arena size */
    uint32_t bump;              /* Bump pointer offset from arena */
    CL_FreeBlock *free_list;    /* Free list head */
    uint32_t total_allocated;   /* Bytes currently allocated */
    uint32_t total_consed;      /* Bytes ever allocated (monotonic, never reset) */
    uint32_t gc_count;          /* Number of GC cycles */
} CL_Heap;

extern CL_Heap cl_heap;

/* Initialize/shutdown heap */
void cl_mem_init(uint32_t heap_size);
void cl_mem_shutdown(void);

/* Signal a storage error without allocating (safe when heap is exhausted) */
void cl_storage_error(const char *fmt, ...);

/* Allocate a heap object (triggers GC if needed) */
void *cl_alloc(uint8_t type, uint32_t size);

/* Convenience allocators */
CL_Obj cl_cons(CL_Obj car, CL_Obj cdr);
CL_Obj cl_make_string(const char *str, uint32_t len);
CL_Obj cl_make_symbol(CL_Obj name);
CL_Obj cl_make_function(CL_CFunc func, CL_Obj name, int min_args, int max_args);
CL_Obj cl_make_vector(uint32_t length);
CL_Obj cl_make_array(uint32_t total, uint8_t rank, uint32_t *dims,
                     uint8_t flags, uint32_t fill_ptr);
CL_Obj cl_make_hashtable(uint32_t bucket_count, uint32_t test);
CL_Obj cl_make_condition(CL_Obj type_name, CL_Obj slots, CL_Obj report_string);
CL_Obj cl_make_struct(CL_Obj type_name, uint32_t n_slots);
CL_Obj cl_make_bignum(uint32_t n_limbs, uint32_t sign);
CL_Obj cl_make_ratio(CL_Obj numerator, CL_Obj denominator);
CL_Obj cl_make_single_float(float value);
CL_Obj cl_make_double_float(double value);
CL_Obj cl_make_random_state(uint32_t seed);
CL_Obj cl_make_bit_vector(uint32_t nbits);
CL_Obj cl_make_pathname(CL_Obj host, CL_Obj device, CL_Obj directory,
                        CL_Obj name, CL_Obj type, CL_Obj version);
CL_Obj cl_make_cell(CL_Obj value);

/* GC root protection */
#define CL_GC_PROTECT(var) cl_gc_push_root(&(var))
#define CL_GC_UNPROTECT(n) cl_gc_pop_roots(n)

void cl_gc_push_root(CL_Obj *root);
void cl_gc_pop_roots(int n);
void cl_gc_reset_roots(void);

/* Manually trigger GC */
void cl_gc(void);

/* Debug/stats */
void cl_mem_stats(void);

#endif /* CL_MEM_H */
