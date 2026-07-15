#include "mem.h"
#include "error.h"
#include "float.h"
#include "vm.h"
#include "readtable.h"
#include "package.h"
#include "stream.h"
#include "reader.h"   /* cl_srcloc_table (GC invalidate/forward hooks) */
#include "../platform/platform.h"
#include "../platform/platform_thread.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef DEBUG_GC_STRESS_CBT
#include <execinfo.h>
#endif

/* External roots needed for GC marking */
extern CL_Obj macro_table, setf_table, setf_fn_table, setf_expander_table, type_table, compiler_macro_table;
extern CL_Obj cl_clos_class_table;
extern CL_Obj struct_table;  /* builtins_struct.c: struct type registry */
extern CL_Obj condition_hierarchy;        /* builtins_condition.c */
extern CL_Obj condition_slot_table;       /* builtins_condition.c */
extern CL_Obj condition_default_initargs; /* builtins_condition.c */
extern CL_Obj condition_slot_initforms;   /* builtins_condition.c */

/* Active compiler chain is accessed via cl_active_compiler macro (thread.h) */
typedef struct CL_Compiler_s CL_Compiler;

/* thread.h defines `pending_lambda_name` as a macro for `(CT->pending_lambda_name)`,
 * but the GC walks every thread via an explicit `t->` pointer (not CT), so we
 * must reference the raw struct member.  Drop the macro here — mem.c only ever
 * touches this field through `t->pending_lambda_name`, never the macro form. */
#undef pending_lambda_name

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

/* GC mark stack (iterative marking).
 *
 * Starts on a small static buffer (BSS — always available, cannot fail) and
 * GROWS geometrically on demand up to a heap-proportional cap; see
 * gc_mark_stack_grow.  Growth matters: an object with more unmarked children
 * than the stack has free slots (a >4K-element vector, a large hash table)
 * overflows it, and the overflow fallback below is QUADRATIC — each full-arena
 * re-scan pass recovers at most ~one-stack-full of dropped children, so a
 * large live heap needs live/capacity passes, each pass re-visiting every
 * marked object's children.  Measured on a 210MB live heap: 49 s per GC with
 * a fixed 4096-entry stack vs 35 ms with a large-enough stack.  The re-scan
 * loop in gc_mark is kept as the correctness fallback for when growth fails
 * (platform_alloc OOM or cap reached) — it is slow but never wrong.
 * Growth happens with the world stopped (mark runs under STW), so no other
 * thread can observe the buffer swap. */
static CL_Obj   gc_mark_stack_initial[CL_GC_MARK_STACK_SIZE];
static CL_Obj  *gc_mark_stack = gc_mark_stack_initial;
static uint32_t gc_mark_stack_cap = CL_GC_MARK_STACK_SIZE;
static uint32_t gc_mark_top = 0;
static int gc_mark_overflow = 0;    /* Set when mark stack overflows */
static int gc_mark_grow_failed = 0; /* Per-cycle latch: stop re-attempting a
                                     * failed grow on every subsequent push
                                     * this cycle.  Reset by gc_mark. */
/* Monotonic diagnostics (exposed via ext:%gc-mark-stats and cl_mem_stats):
 * grows = successful capacity doublings; rescan passes = full-arena overflow
 * recovery passes — nonzero means the quadratic fallback ran (a growth
 * failure), which is worth knowing about long before it becomes a 49s GC. */
static uint32_t gc_mark_stack_grows = 0;
static uint32_t gc_mark_rescan_passes = 0;
/* Test hook: when nonzero, caps growth at this many entries so the overflow
 * re-scan fallback can be exercised deterministically (see
 * tests/test_gc_markstack.c).  0 = normal heap-proportional cap. */
static uint32_t gc_mark_stack_test_limit = 0;

/* Forward declarations */
static void gc_mark(void);
static void gc_sweep(void);
static void gc_reset_transient_state(void);
static int root_slot_independently_forwarded(CL_Obj *slot);
static void *alloc_from_bump(uint32_t size);
static void *alloc_from_free_list(uint32_t *sizep, uint32_t max_steps);
void gc_mark_obj(CL_Obj obj);
static void gc_mark_push(CL_Obj obj);
static void gc_mark_stack_release(void);
static void *alloc_from_free_list(uint32_t *sizep, uint32_t max_steps);
static void *alloc_from_bump(uint32_t size);

/* Fast-path bound on the first-fit free-list walk.  alloc_from_free_list is
 * O(list length); under a fragmented heap (many small blocks interspersed with
 * live objects) an allocation-heavy workload would degrade to O(n) per
 * allocation — O(n^2) overall — which manifests as a 100% CPU hang (e.g. the
 * bignum-heavy power-of-ten table built at FASL load time by jzon's
 * eisel-lemire).  cl_alloc probes only this many blocks on the fast path; if no
 * fit turns up it compacts (which resets the bump pointer and clears
 * fragmentation, restoring the O(1) bump path) rather than walking the whole
 * list.  A full unbounded walk is still used as the last resort before
 * declaring the heap exhausted, so a fit deep in the list is never missed.
 * See tests/test_alloc_freelist_perf.c for the regression test. */
#ifndef CL_FREELIST_PROBE_LIMIT
#define CL_FREELIST_PROBE_LIMIT 32
#endif
#ifdef DEBUG_GC
static void gc_verify_marked(void);
static void gc_dump_roots_dbg(void);
static int gc_verify_errors;  /* defined/reset inside gc_verify_marked */
#endif

/* Compaction forwarding table — maps old_offset/CL_ALIGN -> new_offset.
 * Allocated via platform_alloc during compaction, freed afterwards. */
static uint32_t *gc_fwd_table = NULL;
static uint32_t gc_fwd_table_entries = 0;

/* Bump level at which the last forwarding-table platform_alloc failed
 * (0 = none).  While the bump front is at or above this level a
 * sweep-triggered compaction attempt (cl_alloc trigger 2) would fail the
 * same way — full mark work for nothing — so trigger 2 is gated on it.
 * Cleared by a successful compaction (which also resets the bump). */
static uint32_t gc_fwd_fail_bump = 0;

/* Track last GC cycle at which compaction ran — prevents infinite loops
 * when the heap is genuinely full (no fragmentation to reclaim). */
static uint32_t gc_last_compact_cycle = 0xFFFFFFFF;

/* Number of sweep-only GCs (cl_gc) run since the last compaction.  A sweep
 * never resets the bump pointer, so once the bump is exhausted a run of
 * sweeps makes no lasting progress: each just re-coalesces the free list and
 * the next allocation sweeps again.  When this counter crosses
 * GC_SWEEPS_BEFORE_COMPACT (and the heap is not genuinely near-full) cl_alloc
 * forces a compaction, which resets the bump pointer and defragments — escaping
 * the "sweep-forever" regime.  Reset to 0 by cl_gc_compact. */
static uint32_t gc_sweeps_since_compact = 0;


#ifdef DEBUG_GC_STRESS
/* GC-stress debugging: when ready, force a compaction before every single-
 * threaded allocation (see cl_alloc).  Enabled at runtime after boot so the
 * FASL/boot load isn't slowed to a crawl. */
int cl_gc_stress_ready = 0;
static int cl_gc_stress_active = 0;
#endif

/* Pending compaction flag — set when cl_alloc detects fragmentation,
 * cleared when compaction runs at a safe point.
 * Non-static: accessed by VM dispatch loop for safe-point checks. */
int gc_compact_pending = 0;

/* Count of threads currently inside cl_jit_invoke.  Bumped on
 * outermost JIT entry, decremented when the outermost JIT frame
 * returns.  Informational since offset-validated conservative
 * scanning (gc_scan_jit_native_stack) made the compaction inhibit
 * unnecessary; kept for future use (e.g. JIT-aware diagnostics or
 * a faster early-out path).  See specs/native-backend.md
 * §"GC interaction". */
volatile int cl_jit_active_threads = 0;

/* Global root registration table — static CL_Obj variables that must be
 * marked during GC and updated (forwarded) during compaction.
 * Used for cached interned keyword symbols, type symbols, etc. */
static CL_Obj *global_roots[CL_MAX_GLOBAL_ROOTS];
static int n_global_roots = 0;

void cl_gc_register_root(CL_Obj *root_ptr)
{
    int i;
    /* Idempotent: registering the same address twice must be a no-op.
     * gc_forward is NOT idempotent (the forwarding table is keyed by
     * pre-compaction offsets), so a slot forwarded twice through two
     * registry entries would be rewritten to an unrelated object.  The
     * init functions that register handles re-run on every heap
     * re-initialization (each C unit test, embedded restarts) and would
     * otherwise duplicate every entry.  Registration is boot-time-only,
     * so the linear scan costs nothing at runtime. */
    for (i = 0; i < n_global_roots; i++) {
        if (global_roots[i] == root_ptr)
            return;
    }
    if (n_global_roots >= CL_MAX_GLOBAL_ROOTS) {
        /* A silently-dropped root is a guaranteed future memory
         * corruption (the slot goes stale on the next compaction and is
         * never marked).  Fail loudly at boot instead. */
        platform_write_string(
            "FATAL: cl_gc_register_root: CL_MAX_GLOBAL_ROOTS exceeded — "
            "raise the limit in mem.h\n");
        exit(1);
    }
    global_roots[n_global_roots++] = root_ptr;
}

/* Align size up to CL_ALIGN boundary */
static uint32_t align_up(uint32_t size)
{
    return (size + CL_ALIGN - 1) & ~(CL_ALIGN - 1);
}

#ifdef CL_TLAB
/* ================================================================
 * TLAB — per-thread allocation buffers (multi-threaded fast path)
 *
 * With >1 thread registered, every cl_alloc used to serialize on
 * alloc_mutex — at actor-workload allocation rates the mutex (and its
 * cache-line ping-pong) dominates the per-message cost.  Instead, each
 * thread carves a private CHUNK from the shared bump front (or, when the
 * bump is exhausted, the free list) once per ~chunk of allocation, and
 * cuts objects from it with NO locking.
 *
 * Invariants (all load-bearing — see gc_sweep/gc_compute_forwarding):
 *  - The uncut remainder [tlab_cur, tlab_end) is formatted as a
 *    CL_FreeBlock at ALL times (size = remainder, not linked into the
 *    free list), so every linear arena walk parses the chunk.  A cut
 *    rewrites the remainder header BEFORE the object header/memset
 *    touches disjoint bytes below it.
 *  - Cuts never leave a remainder smaller than CL_MIN_ALLOC_SIZE: an
 *    undersized tail is absorbed into the object (same precedent as
 *    alloc_from_free_list's use-entire-block path).
 *  - Every GC cycle clears ALL threads' TLABs during stop-the-world
 *    (gc_tlab_reset_all).  Mandatory, not an optimization: gc_sweep
 *    rebuilds the free list from unmarked regions, so an active tail
 *    would be handed to another thread while the owner keeps cutting.
 *    Threads refill on their next allocation.
 *  - There is no safepoint between a cut and its header write, so a
 *    stop-the-world initiator can never walk a headerless cut (STW
 *    waits for this thread to park; it parks only at safepoints).
 *
 * Single-threaded execution (including the whole DEBUG_GC_STRESS suite)
 * never takes this path: cl_alloc gates it on `multi`.
 * ================================================================ */

/* Chunk size: configured once per heap in cl_mem_init (0 = disabled).
 * CLAMIGA_TLAB_CHUNK=<bytes> overrides; scaled down on small heaps so
 * N threads' chunks can't consume a test-sized arena. */
#ifndef CL_TLAB_CHUNK
#define CL_TLAB_CHUNK (32u * 1024u)
#endif
#define CL_TLAB_MIN_CHUNK 4096u
static uint32_t tlab_chunk_size = 0;
static uint32_t tlab_max_obj    = 0;   /* larger objects go the shared path */

/* Format [off, off+len) as a dead, walkable region (len >= 8, aligned).
 * Not linked into the free list — the next sweep reclaims/coalesces it. */
static void tlab_format_hole(uint32_t off, uint32_t len)
{
    CL_FreeBlock *fb = (CL_FreeBlock *)(cl_heap.arena + off);
    fb->size = len;
    fb->next_offset = 0;
}

/* Owner-only, lock-free: cut `*sizep` bytes from t's chunk.  May grow
 * *sizep by up to CL_MIN_ALLOC_SIZE-CL_ALIGN (absorbed tail).  Returns
 * NULL when no chunk or it doesn't fit (caller refills). */
static void *tlab_cut(CL_Thread *t, uint32_t *sizep)
{
    uint32_t size = *sizep;
    uint32_t rem;
    void *ptr;

    if (!t->tlab_end)
        return NULL;
    rem = t->tlab_end - t->tlab_cur;
    if (size > rem)
        return NULL;
    if (rem - size < CL_MIN_ALLOC_SIZE) {
        size = rem;                 /* absorb undersized tail */
        *sizep = size;
    }
    ptr = cl_heap.arena + t->tlab_cur;
    t->tlab_cur += size;
    if (t->tlab_cur == t->tlab_end)
        t->tlab_cur = t->tlab_end = 0;
    else
        tlab_format_hole(t->tlab_cur, t->tlab_end - t->tlab_cur);
    t->tlab_consed += size;
    return ptr;
}

/* Carve a fresh chunk for t and cut the first object from it.  Caller
 * MUST hold alloc_mutex.  Any abandoned remainder of the previous chunk
 * stays behind as a formatted hole (< tlab_max_obj bytes; the next sweep
 * reclaims it).  Falls back to the bounded free-list probe when the bump
 * front is exhausted, so TLABs stay effective in the sweep-only regime
 * between compactions.  Returns NULL when neither source can supply a
 * chunk — the caller degrades to the shared slow path. */
static void *tlab_refill_cut(CL_Thread *t, uint32_t *sizep)
{
    void    *chunk;
    uint32_t chunk_size = tlab_chunk_size;
    uint32_t chunk_off;

    if (!chunk_size)
        return NULL;
    chunk = alloc_from_bump(chunk_size);
    if (!chunk) {
        /* May return a larger block (use-entire-block path) — honor it. */
        chunk = alloc_from_free_list(&chunk_size, CL_FREELIST_PROBE_LIMIT);
    }
    if (!chunk)
        return NULL;
    chunk_off = (uint32_t)((uint8_t *)chunk - cl_heap.arena);

    cl_heap.total_consed += t->tlab_consed;
    t->tlab_consed = 0;
    /* Chunk-granular: counts the whole chunk as allocated up front; the
     * next sweep recomputes the exact figure.  Errs toward "heap fuller
     * than it is", which only makes the compaction heuristics fire
     * earlier, never later. */
    cl_heap.total_allocated += chunk_size;
    cl_heap.tlab_refills++;

    tlab_format_hole(chunk_off, chunk_size);
    t->tlab_cur = chunk_off;
    t->tlab_end = chunk_off + chunk_size;
    return tlab_cut(t, sizep);
}

/* Clear every registered thread's TLAB.  Called with the world stopped
 * (or single-threaded) at the top of each GC cycle, and on heap re-init.
 * Remainders are already formatted holes — the sweep/compaction that
 * follows reclaims them; owners refill on their next allocation. */
static void gc_tlab_reset_all(void)
{
    CL_Thread *t;
    for (t = cl_thread_list; t; t = t->next) {
        t->tlab_cur = t->tlab_end = 0;
        cl_heap.total_consed += t->tlab_consed;
        t->tlab_consed = 0;
    }
}

/* Thread-exit retirement (see mem.h).  Runs on a live heap, so the
 * counter flush needs alloc_mutex; the remainder hole needs nothing. */
void cl_tlab_retire(CL_Thread *t)
{
    int multi = (cl_thread_count > 1);
    if (!t->tlab_end && !t->tlab_consed)
        return;
    if (multi && alloc_mutex) platform_mutex_lock(alloc_mutex);
    t->tlab_cur = t->tlab_end = 0;
    cl_heap.total_consed += t->tlab_consed;
    t->tlab_consed = 0;
    if (multi && alloc_mutex) platform_mutex_unlock(alloc_mutex);
}
#else /* !CL_TLAB */
/* Feature compiled out (Amiga binary-size gate — see mem.h).  Keep the
 * GC-entry hook as a no-op so call sites stay unconditional. */
static void gc_tlab_reset_all(void) {}
#endif /* CL_TLAB */

void cl_mem_init(uint32_t heap_size)
{
    if (heap_size == 0)
        heap_size = CL_DEFAULT_HEAP_SIZE;

    cl_heap.arena = (uint8_t *)platform_alloc(heap_size);
    if (!cl_heap.arena) {
        char msg[96];
        snprintf(msg, sizeof(msg),
                 "FATAL: Failed to allocate %u-byte heap (try a smaller --heap)\n",
                 (unsigned)heap_size);
        platform_write_string(msg);
        exit(1);
    }
    cl_arena_base = cl_heap.arena;
    cl_heap.arena_size = heap_size;
    cl_heap.bump = CL_ALIGN;  /* Skip offset 0 so it stays NIL */
    cl_heap.free_list = 0;
    cl_heap.total_allocated = 0;
    cl_heap.total_consed = 0;
    cl_heap.gc_count = 0;
    cl_heap.compact_count = 0;
    cl_heap.freelist_steps = 0;
    cl_heap.tlab_refills = 0;
    gc_last_compact_cycle = 0xFFFFFFFF;
    gc_sweeps_since_compact = 0;

#ifdef CL_TLAB
    /* TLAB chunk size for this heap.  CLAMIGA_TLAB_CHUNK=<bytes> overrides
     * (0 disables); otherwise CL_TLAB_CHUNK, scaled down so per-thread
     * chunks can't dominate a small (test-sized) arena, and disabled
     * entirely when even the minimum chunk would be outsized. */
    {
        uint32_t chunk = CL_TLAB_CHUNK;
        const char *e = getenv("CLAMIGA_TLAB_CHUNK");
        if (e)
            chunk = (uint32_t)atol(e);
        if (chunk) {
            uint32_t cap = heap_size / 64u;
            if (chunk > cap) chunk = cap;
            chunk &= ~(uint32_t)(CL_ALIGN - 1);
            if (chunk < CL_TLAB_MIN_CHUNK)
                chunk = 0;
        }
        tlab_chunk_size = chunk;
        tlab_max_obj = chunk / 8u;
    }

    /* Clear stale TLABs on every registered thread: their offsets point
     * into the PREVIOUS arena (unit tests re-init with fresh heaps). */
    {
        CL_Thread *t;
        for (t = cl_thread_list; t; t = t->next) {
            t->tlab_cur = t->tlab_end = 0;
            t->tlab_consed = 0;
        }
    }
#endif /* CL_TLAB */

    /* Reset EVERY registered thread's Lisp-heap references (root stacks,
     * mv_values, pending/handler/restart/reader/printer/TLV state, ...)
     * — all of them are stale offsets into the arena just freed, and
     * gc_mark_thread_roots walks them unconditionally.  Same bug class as
     * the shared-globals reset below; resetting only the CURRENT thread's
     * gc_root_count (the old behavior) left e.g. main's mv_values[] from
     * a pre-re-init cl_error pointing into the fresh arena. */
    {
        CL_Thread *rt;
        for (rt = cl_thread_list; rt; rt = rt->next)
            cl_thread_reset_lisp_state(rt);
    }
    /* Drop any grown mark stack from a previous heap: the growth cap is
     * derived from the (possibly different) new arena size, and unit tests
     * re-init with small heaps.  Also resets gc_mark_top. */
    gc_mark_stack_release();
    gc_mark_stack_grows = 0;
    gc_mark_rescan_passes = 0;
    gc_mark_stack_test_limit = 0;

    /* Reset GC state that survives in static storage across heap
     * re-initialization (each C unit test, embedded restarts): pending
     * flags and JIT scan/pin bookkeeping hold offsets and decisions from
     * the PREVIOUS arena. */
    gc_reset_transient_state();

    /* Reset the global root registry: registered slots are static C
     * globals still holding offsets into the PREVIOUS heap after a
     * shutdown/re-init cycle.  Marking them before their init functions
     * re-assign and re-register would set mark bits at arbitrary offsets
     * in the fresh arena. */
    n_global_roots = 0;

    /* Reset the UNCONDITIONALLY-marked shared Lisp globals for the same
     * reason (the documented "stale static tables on re-init" bug class).
     * gc_mark marks these directly — not through the resettable registry
     * above — so after a heap re-init they still hold offsets from the
     * PREVIOUS arena.  Marking such a stale offset in the fresh arena
     * stamps a mark bit into the middle of whatever live object now spans
     * it (the classic 0x800000-in-a-cdr corruption), as soon as the new
     * bump front grows past the old offset so it passes validation.
     * Observed deterministically from the C unit tests' shutdown/re-init
     * pattern; see shared_tables_reset_on_heap_reinit in
     * tests/test_gc_threaded.c.  Every value here is invalid by
     * definition — the arena they pointed into was just freed. */
    macro_table = CL_NIL;
    setf_table = CL_NIL;
    setf_fn_table = CL_NIL;
    setf_expander_table = CL_NIL;
    type_table = CL_NIL;
    compiler_macro_table = CL_NIL;
    cl_clos_class_table = CL_NIL;
    struct_table = CL_NIL;
    condition_hierarchy = CL_NIL;
    condition_slot_table = CL_NIL;
    condition_default_initargs = CL_NIL;
    condition_slot_initforms = CL_NIL;
    cl_package_registry = CL_NIL;
    {
        extern CL_Obj *cl_main_thread_lisp_obj_ptr(void);
        *cl_main_thread_lisp_obj_ptr() = CL_NIL;
    }
    /* Readtable pool user macro closures (marked per allocated entry) */
    {
        int rt, ch;
        for (rt = 0; rt < CL_RT_POOL_SIZE; rt++) {
            if (!(cl_readtable_alloc_mask & (1u << rt)))
                continue;
            for (ch = 0; ch < CL_RT_CHARS; ch++) {
                cl_readtable_pool[rt].macro_fn[ch] = CL_NIL;
                cl_readtable_pool[rt].dispatch_fn[ch] = CL_NIL;
            }
        }
    }

    /* Initialize allocation mutex */
    platform_mutex_init(&alloc_mutex);
}

/* Persistent JIT-stack scan candidate buffer — defined here so
 * cl_mem_shutdown can free it; helpers and full doc live with
 * gc_scan_jit_native_stack below. */
static uint32_t        *jit_scan_cand_buf = NULL;
static int              jit_scan_cand_cap = 0;

/* Persistent buffer for the deduplicated registered-root forwarding pass
 * (see gc_update_registered_roots below) — defined here so
 * cl_mem_shutdown can free it. */
static CL_Obj         **gc_root_slot_buf = NULL;
static uint32_t         gc_root_slot_cap = 0;

/* Pinned-object set for the moving compactor (option B).  The mark-phase
 * conservative JIT-stack scan records, in ascending arena-offset order,
 * every *validated* real-object offset referenced from the current
 * thread's JIT m68k stack.  gc_compute_forwarding keeps these objects at
 * their current offset (forwarding = identity) so the JIT's spilled
 * operand pointers and any C-stack words that merely *look* like heap
 * offsets stay valid without rewriting the C stack.  This replaces the
 * old gc_forward_jit_native_stack writeback, which could mis-rewrite a
 * coincidental-integer C-stack word that happened to equal a live
 * object's offset (a layout-roulette corruption — see
 * specs/native-backend.md §"GC interaction").  Rebuilt every mark; only
 * non-empty when a JIT frame is live (jit_depth > 0). */
static uint32_t        *jit_pinned       = NULL;
static int              jit_pinned_count = 0;
static int              jit_pinned_cap   = 0;

/* Set when jit_pin_record could not grow jit_pinned[] (platform_alloc
 * OOM): the pinned set is incomplete, so cl_gc_compact MUST NOT move
 * anything this cycle — a live JIT frame's spilled C-stack word would
 * keep the old offset of a relocated object.  Reset by gc_mark. */
static int              gc_jit_pin_oom   = 0;

/* Free-list snapshot for the conservative JIT-stack scan.  A free block's
 * header word is its raw size (<= CL_HDR_SIZE_MASK), which reads as an
 * UNMARKED TYPE_CONS — so a stale spill word holding a freed offset would
 * pass phase-2 header validation, and gc_mark_obj would mark the free
 * block live while gc_mark_children chases its next_offset link and
 * poisoned payload as car/cdr.  The scan must therefore skip offsets
 * that are on the free list.  Collected (sorted ascending) at most once
 * per GC cycle, lazily on the first scan; the free list cannot change
 * during STW marking.  On snapshot OOM, membership falls back to a
 * linear free-list walk per matched candidate. */
static uint32_t        *jit_scan_free_snap  = NULL;
static int              jit_scan_free_count = 0;
static int              jit_scan_free_cap   = 0;
static int              jit_scan_free_valid = 0;

/* Set during a compaction's reference-update pass when at least one
 * CL_Obj immediate baked into JIT native code was rewritten in place.
 * cl_gc_compact flushes the CPU caches once at the end so 68040/060
 * don't execute stale instruction-cache copies of the patched code. */
static int gc_native_code_patched = 0;

/* Reset static GC bookkeeping on heap re-initialization — see the call
 * in cl_mem_init.  (Defined here, below all the statics it touches.) */
static void gc_reset_transient_state(void)
{
    gc_compact_pending = 0;
    gc_mark_overflow = 0;
    gc_mark_grow_failed = 0;
    gc_fwd_fail_bump = 0;
    gc_jit_pin_oom = 0;
    jit_pinned_count = 0;
    jit_scan_free_count = 0;
    jit_scan_free_valid = 0;
}

void cl_mem_shutdown(void)
{
    if (cl_heap.arena) {
        platform_free(cl_heap.arena);
        cl_heap.arena = NULL;
    }
    gc_mark_stack_release();
    if (jit_scan_cand_buf) {
        platform_free(jit_scan_cand_buf);
        jit_scan_cand_buf = NULL;
        jit_scan_cand_cap = 0;
    }
    if (jit_pinned) {
        platform_free(jit_pinned);
        jit_pinned = NULL;
        jit_pinned_cap = 0;
        jit_pinned_count = 0;
    }
    if (jit_scan_free_snap) {
        platform_free(jit_scan_free_snap);
        jit_scan_free_snap = NULL;
        jit_scan_free_cap = 0;
        jit_scan_free_count = 0;
        jit_scan_free_valid = 0;
    }
    if (gc_root_slot_buf) {
        platform_free(gc_root_slot_buf);
        gc_root_slot_buf = NULL;
        gc_root_slot_cap = 0;
    }
    if (alloc_mutex) {
        platform_mutex_destroy(alloc_mutex);
        alloc_mutex = NULL;
    }
}

/* Wrap-safe bump-fit check.  The naive `bump + size <= arena_size` wraps
 * in uint32 when bump sits near the end of a ~4GB arena (size is capped
 * at CL_HDR_SIZE_MASK by cl_alloc, but bump + 8MB can still exceed
 * UINT32_MAX for arena sizes above ~4GB-8MB) — the wrapped sum passes
 * the check and hands out a pointer past the arena.  Non-static so the
 * unit test can exercise the wrap arithmetic without a 4GB heap. */
int cl_bump_fits(uint32_t bump, uint32_t size, uint32_t arena_size)
{
    return size <= arena_size && bump <= arena_size - size;
}

static void *alloc_from_bump(uint32_t size)
{
    if (cl_bump_fits(cl_heap.bump, size, cl_heap.arena_size)) {
        void *ptr = cl_heap.arena + cl_heap.bump;
        cl_heap.bump += size;
        return ptr;
    }
    return NULL;
}

/* First-fit allocation from the free list.  Walks at most max_steps blocks
 * (0 == unbounded); returns NULL if no fit is found within the bound. */
static void *alloc_from_free_list(uint32_t *sizep, uint32_t max_steps)
{
    uint32_t size = *sizep;
    uint32_t *prev_off = &cl_heap.free_list;
    uint32_t cur_off = cl_heap.free_list;
    uint32_t steps = 0;

    while (cur_off) {
        CL_FreeBlock *block = (CL_FreeBlock *)(cl_heap.arena + cur_off);
        cl_heap.freelist_steps++;
        if (max_steps && ++steps > max_steps)
            return NULL;
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
    cl_saved_pending_top = 0;
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

#ifdef DEBUG_GC_STRESS
    /* Force a compacting (moving) GC before every allocation so that any
     * CL_Obj C local held unprotected across an allocating call is reliably
     * corrupted (relocated out from under the stale local).  Single-thread
     * only; gated behind a build flag — compiles to nothing normally.
     *
     * NOTE: the `!multi` gate means the TLAB fast path below (CL_TLAB,
     * multi-threaded only) never runs under gc-stress — the two are
     * mutually exclusive by construction.  TLAB coverage instead comes
     * from tlab_mixed_sizes_vs_concurrent_compaction in
     * tests/test_gc_threaded.c, which hammers concurrent compaction
     * against live TLAB cutting without the per-alloc stress hook. */
    if (!multi && cl_gc_stress_ready && !cl_gc_stress_active) {
        cl_gc_stress_active = 1;
        {
            /* Bisection controls (read once):
             *   CLAMIGA_GC_STRESS_SKIP=N — only compact on alloc index > N
             *     (suffix mode; binary-search the corrupting alloc).
             *   CLAMIGA_GC_STRESS_AT=N   — compact ONLY at alloc index N and
             *     dump a C backtrace there (pin the exact corrupting site). */
            static long s_skip = -2, s_at = -2;
            static long s_count = 0;
            int do_compact;
            if (s_skip == -2) {
                const char *e1 = getenv("CLAMIGA_GC_STRESS_SKIP");
                const char *e2 = getenv("CLAMIGA_GC_STRESS_AT");
                s_skip = e1 ? atol(e1) : -1;
                s_at   = e2 ? atol(e2) : -1;
            }
            s_count++;
            if (s_at >= 0)        do_compact = (s_count == s_at);
            else if (s_skip >= 0) do_compact = (s_count > s_skip);
            else                  do_compact = 1;
            if (do_compact) {
                if (s_at >= 0) {
                    extern void cl_capture_backtrace(void);
                    fprintf(stderr, "[GCSTRESS-AT] compacting at alloc #%ld\n", s_count);
                    cl_capture_backtrace();
                    fprintf(stderr, "%s", cl_backtrace_buf);
#ifdef DEBUG_GC_STRESS_CBT
                    {
                        void *cbt[32];
                        int ncbt = backtrace(cbt, 32);
                        fprintf(stderr, "[GCSTRESS-AT] C backtrace:\n");
                        backtrace_symbols_fd(cbt, ncbt, 2);
                    }
#endif
                    fflush(stderr);
                }
                cl_gc_compact();
            }
        }
        cl_gc_stress_active = 0;
    }
#endif

    size = align_up(size);
    if (size < CL_MIN_ALLOC_SIZE)
        size = CL_MIN_ALLOC_SIZE;

    /* Guard: size must fit the 23-bit header size field.  This MUST run
     * before alloc_from_bump: the old post-allocation guard fired after
     * the bump pointer had already advanced, longjmp'ing out via
     * cl_storage_error and leaving a headerless region inside the walked
     * bump front — every later arena walk (sweep, mark-overflow rescan,
     * forwarding, slide) desynced there, silently corrupting the heap.
     * Reachable from pure Lisp: (make-array 3000000) passes the element-
     * count check but requests ~12MB of bytes. */
    if (size > CL_HDR_SIZE_MASK)
        cl_storage_error("Allocation too large for header: %u bytes (max %u)",
                         (unsigned)size, (unsigned)CL_HDR_SIZE_MASK);

#ifdef CL_TLAB
    /* TLAB fast path (multi-threaded only): cut from this thread's private
     * chunk without touching alloc_mutex; refill under the mutex when the
     * chunk is spent.  Objects above tlab_max_obj take the shared path so
     * a large vector doesn't waste most of a chunk.  When even a refill
     * fails (bump front AND bounded free-list probe exhausted), fall
     * through to the shared slow path below, which GCs/compacts. */
    if (multi && size <= tlab_max_obj) {
        CL_Thread *t = cl_get_current_thread();
        ptr = tlab_cut(t, &size);
        if (!ptr) {
            platform_mutex_lock(alloc_mutex);
            ptr = tlab_refill_cut(t, &size);
            platform_mutex_unlock(alloc_mutex);
        }
        if (ptr) {
            memset(ptr, 0, size);
            ((CL_Header *)ptr)->header = CL_MAKE_HDR(type, size);
            return ptr;
        }
    }
#endif /* CL_TLAB */

    if (multi) platform_mutex_lock(alloc_mutex);

    /* Fast path: bump allocator, then a *bounded* first-fit free-list probe.
     * The bound keeps a single allocation cheap (O(CL_FREELIST_PROBE_LIMIT))
     * even when the free list is long; if no fit turns up quickly we fall
     * through to GC/compaction below rather than walking the whole list. */
    ptr = alloc_from_bump(size);
    if (!ptr) {
        /* Try free list (may update size if using entire oversized block) */
        ptr = alloc_from_free_list(&size, CL_FREELIST_PROBE_LIMIT);
    }
    if (!ptr) {
        /* Bump exhausted and the bounded free-list probe missed.  Sweep first:
         * it is cheaper than compaction, may itself free a fit, AND recomputes
         * the live-set size (cl_heap.total_allocated) so the compaction decision
         * below uses a FRESH measurement — not a stale high-water that an
         * earlier transient peak could pin near the arena size.  Release
         * alloc_mutex first so other threads can reach safepoints during STW. */
        if (multi) platform_mutex_unlock(alloc_mutex);
        cl_gc();
        if (multi) platform_mutex_lock(alloc_mutex);
        gc_sweeps_since_compact++;
        ptr = alloc_from_bump(size);
        if (!ptr) {
            /* Guard: skip the free-list probe when Trigger 2 would compact
             * immediately afterward.  alloc_from_free_list zeroes the block it
             * extracts (memset in the return path); a zeroed block has a zero
             * size field, which stops every compaction pass's arena scan at that
             * slot, leaving live objects above it without forwarding addresses —
             * the next bump allocation then silently overwrites them.  Skipping
             * the probe keeps the arena intact so cl_gc_compact() can walk it
             * cleanly; ptr stays NULL and the compaction block below fires via
             * Trigger 1 (!ptr), satisfying this allocation after compaction. */
            uint32_t probe_live_hwm = cl_heap.arena_size - (cl_heap.arena_size >> 3);
            int will_compact_t2 = (gc_last_compact_cycle != cl_heap.gc_count) &&
                                  (cl_heap.total_allocated < probe_live_hwm) &&
                                  (gc_sweeps_since_compact >= GC_SWEEPS_BEFORE_COMPACT) &&
                                  !(gc_fwd_fail_bump &&
                                    cl_heap.bump >= gc_fwd_fail_bump);
            if (!will_compact_t2)
                ptr = alloc_from_free_list(&size, CL_FREELIST_PROBE_LIMIT);
        }

        /* Now decide whether to ALSO compact (which resets the bump pointer and
         * defragments).  Gated to once per GC cycle.  Two triggers:
         *
         *   1. The sweep + bounded probe STILL found no fit — compaction is the
         *      only remaining way to satisfy this allocation (the long-standing
         *      last-resort path).
         *
         *   2. We have swept GC_SWEEPS_BEFORE_COMPACT times since the last
         *      compaction AND the heap is not near-full.  A sweep-only cl_gc()
         *      never resets the bump pointer, so when a fragmented free list
         *      defeats the bounded probe we can keep "succeeding" via a buried
         *      free block while running a full mark-sweep on nearly every
         *      allocation — the "sweep-forever" pathology (acutely heap-size
         *      sensitive: hundreds to thousands of GCs cold-compiling a large
         *      system, while a nearby heap size needs ~15).  Compaction frees a
         *      large contiguous run of bump space, escaping the loop.
         *      "Not near-full" uses the FRESH live-set size from the sweep just
         *      done, so a heap that is mostly reclaimable garbage (compaction
         *      frees >= 1/8 of the arena — worthwhile) is distinguished from one
         *      genuinely packed with live data (compaction would only thrash, so
         *      we stay on the sweep + free-list-reuse path).
         *
         * NOTE: compaction is a moving GC.  All CL_Obj C locals live across an
         * allocating call MUST be GC-protected, and raw pointers derived from
         * CL_Obj re-derived afterwards.  (This site already compacted before —
         * no new requirement.)  Safe while JIT'd code is on the m68k stack:
         * gc_scan_jit_native_stack offset-validates each conservative root. */
        if (gc_last_compact_cycle != cl_heap.gc_count) {
            uint32_t live_hwm =
                cl_heap.arena_size - (cl_heap.arena_size >> 3);   /* 7/8 arena */
            int compaction_worthwhile = (cl_heap.total_allocated < live_hwm);
            /* Trigger 2 is additionally gated on the forwarding-table OOM
             * latch (gc_fwd_fail_bump): re-attempting at the same bump
             * level would fail the same platform_alloc again after a full
             * (wasted) mark pass.  Trigger 1 (!ptr) is exempt — when
             * compaction is the ONLY way to satisfy the allocation it
             * must be attempted regardless. */
            int fwd_blocked = (gc_fwd_fail_bump != 0 &&
                               cl_heap.bump >= gc_fwd_fail_bump);
            if (!ptr ||
                (compaction_worthwhile && !fwd_blocked &&
                 gc_sweeps_since_compact >= GC_SWEEPS_BEFORE_COMPACT)) {
                gc_compact_pending = 1;
                gc_last_compact_cycle = cl_heap.gc_count;
                if (multi) platform_mutex_unlock(alloc_mutex);
                cl_gc_compact();   /* resets bump + gc_sweeps_since_compact */
                gc_compact_pending = 0;
                if (multi) platform_mutex_lock(alloc_mutex);
                ptr = alloc_from_bump(size);
            }
        }
    }
    if (!ptr) {
        /* Last resort before declaring the heap exhausted: the fit may be
         * deeper in the free list than the bounded probe reached.  Walk the
         * whole list once so a genuine fit is never missed. */
        ptr = alloc_from_free_list(&size, 0 /* unbounded */);
    }
    if (!ptr) {
        if (multi) platform_mutex_unlock(alloc_mutex);
        cl_storage_error("Heap exhausted (requested %u bytes)", (unsigned)size);
    }

    /* (The header-size guard runs at function entry, BEFORE any bump
     * advance — see above.  alloc_from_free_list can only ever shrink-fit
     * or return the exact block, never grow size past the mask.) */

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
    uint32_t alloc_size;
    CL_String *s;
    char stack_buf[256];
    char *safe_str = NULL;

    /* Cap BEFORE computing alloc_size — len near UINT32_MAX wraps the
     * `+ len + 1` arithmetic past cl_alloc's byte-size guard (see the
     * CL_MAX_* block in mem.h). */
    if (len > CL_MAX_STRING_CHARS)
        cl_storage_error("MAKE-STRING: length %u exceeds the maximum "
                         "heap object size (max %u characters)",
                         (unsigned)len, (unsigned)CL_MAX_STRING_CHARS);
    alloc_size = sizeof(CL_String) + len + 1;

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
            if (!safe_str)
                /* MUST NOT fall back to the arena pointer: cl_alloc below
                 * can compact, leaving `str` pointing at moved/freed bytes
                 * — the memcpy would then build a silent garbage string.
                 * Fail loudly instead. */
                cl_storage_error("MAKE-STRING: out of C memory copying a "
                                 "%u-char arena-resident source", (unsigned)len);
            memcpy(safe_str, str, len);
            safe_str[len] = '\0';
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
    uint32_t alloc_size;
    CL_WideString *s;
    uint32_t stack_buf[64];
    uint32_t *safe_chars = NULL;

    /* Cap BEFORE computing alloc_size — len >= 2^30 wraps the `* 4`
     * arithmetic past cl_alloc's byte-size guard (see mem.h). */
    if (len > CL_MAX_WIDE_STRING_CHARS)
        cl_storage_error("MAKE-WIDE-STRING: length %u exceeds the maximum "
                         "heap object size (max %u wide characters)",
                         (unsigned)len, (unsigned)CL_MAX_WIDE_STRING_CHARS);
    alloc_size = sizeof(CL_WideString) + len * sizeof(uint32_t);

    /* If chars points into the arena, copy to a safe buffer first.
     * cl_alloc below may trigger GC compaction which moves arena objects,
     * making the original pointer stale (same guard as cl_make_string). */
    if (chars && (const uint8_t *)chars >= cl_heap.arena &&
        (const uint8_t *)chars < cl_heap.arena + cl_heap.arena_size) {
        if (len <= sizeof(stack_buf) / sizeof(stack_buf[0])) {
            memcpy(stack_buf, chars, len * sizeof(uint32_t));
            safe_chars = stack_buf;
        } else {
            safe_chars = (uint32_t *)platform_alloc(len * sizeof(uint32_t));
            if (!safe_chars)
                /* Same rule as cl_make_string: never keep the arena
                 * pointer across the allocating call below. */
                cl_storage_error("MAKE-WIDE-STRING: out of C memory copying a "
                                 "%u-char arena-resident wide source",
                                 (unsigned)len);
            memcpy(safe_chars, chars, len * sizeof(uint32_t));
        }
        chars = safe_chars ? safe_chars : chars;
    }

    s = (CL_WideString *)cl_alloc(TYPE_WIDE_STRING, alloc_size);
    if (!s) {
        if (safe_chars && safe_chars != stack_buf) platform_free(safe_chars);
        return CL_NIL;
    }
    s->length = len;
    if (chars)
        memcpy(s->data, chars, len * sizeof(uint32_t));
    else
        memset(s->data, 0, len * sizeof(uint32_t));

    if (safe_chars && safe_chars != stack_buf) platform_free(safe_chars);
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
    uint32_t alloc_size;
    CL_Vector *v;
    /* Cap BEFORE computing alloc_size — length >= 2^30 wraps the `* 4`
     * arithmetic past cl_alloc's byte-size guard (see mem.h).  Reachable
     * with corrupted FASL vector lengths (raw u32 from disk). */
    if (length > CL_MAX_VECTOR_ELTS)
        cl_storage_error("MAKE-VECTOR: length %u exceeds the maximum "
                         "heap object size (max %u elements)",
                         (unsigned)length, (unsigned)CL_MAX_VECTOR_ELTS);
    alloc_size = sizeof(CL_Vector) + length * sizeof(CL_Obj);
    v = (CL_Vector *)cl_alloc(TYPE_VECTOR, alloc_size);
    if (!v) return CL_NIL;
    v->length = length;
    v->fill_pointer = CL_NO_FILL_POINTER;
    v->flags = 0;
    v->rank = 0;
    v->elt_type = CL_VEC_ELT_T;
    v->_reserved = 0;
    /* data[] already zeroed (= CL_NIL) by cl_alloc */
    return CL_PTR_TO_OBJ(v);
}

CL_Obj cl_make_array(uint32_t total, uint8_t rank, uint32_t *dims,
                     uint8_t flags, uint32_t fill_ptr)
{
    /* For multi-dim (rank>1): store dimensions in data[0..rank-1], elements at data[rank..] */
    uint32_t n_data;
    uint32_t alloc_size;
    CL_Vector *v;
    /* Cap BEFORE computing n_data/alloc_size — see cl_make_vector. */
    if (total > CL_MAX_VECTOR_ELTS ||
        (rank > 1 && (uint32_t)rank > CL_MAX_VECTOR_ELTS - total))
        cl_storage_error("MAKE-ARRAY: total size %u (rank %u) exceeds the "
                         "maximum heap object size (max %u elements)",
                         (unsigned)total, (unsigned)rank,
                         (unsigned)CL_MAX_VECTOR_ELTS);
    n_data = (rank > 1) ? (uint32_t)rank + total : total;
    /* Adjustable vectors need at least 2 data slots for displacement:
       data[0] = backing vector, data[1] = displaced-index-offset */
    if ((flags & (CL_VEC_FLAG_ADJUSTABLE | CL_VEC_FLAG_FILL_POINTER)) && n_data < 2)
        n_data = 2;
    alloc_size = sizeof(CL_Vector) + n_data * sizeof(CL_Obj);
    v = (CL_Vector *)cl_alloc(TYPE_VECTOR, alloc_size);
    if (!v) return CL_NIL;
    v->length = total;
    v->fill_pointer = fill_ptr;
    v->flags = flags;
    v->rank = rank;
    v->elt_type = CL_VEC_ELT_T;
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
        /* Backing ref at data[base], displaced-index-offset (fixnum) at
         * data[base+1].  base accounts for multi-dim dimension storage. */
        uint32_t base = CL_DISP_BASE_IDX(v);
        if (CL_FIXNUM_P(v->data[base + 1]))
            offset += (uint32_t)CL_FIXNUM_VAL(v->data[base + 1]);
        v = (CL_Vector *)CL_OBJ_TO_PTR(v->data[base]);
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

    /* Cap BEFORE the power-of-two round-up: counts > 2^31 make the
     * round-up loop below spin forever (p <<= 1 wraps to 0), and large
     * counts wrap alloc_size past cl_alloc's byte-size guard (mem.h). */
    if (bucket_count > CL_MAX_HT_BUCKETS)
        cl_storage_error("MAKE-HASH-TABLE: %u buckets exceeds the maximum "
                         "heap object size (max %u)",
                         (unsigned)bucket_count, (unsigned)CL_MAX_HT_BUCKETS);

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
    ht->flags = 0;
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

CL_Obj cl_make_restart(CL_Obj name, CL_Obj function, CL_Obj report,
                       CL_Obj interactive, CL_Obj test, CL_Obj tag)
{
    CL_Restart *r;

    CL_GC_PROTECT(name);
    CL_GC_PROTECT(function);
    CL_GC_PROTECT(report);
    CL_GC_PROTECT(interactive);
    CL_GC_PROTECT(test);
    CL_GC_PROTECT(tag);

    r = (CL_Restart *)cl_alloc(TYPE_RESTART, sizeof(CL_Restart));
    CL_GC_UNPROTECT(6);

    if (!r) return CL_NIL;
    r->name = name;
    r->function = function;
    r->report = report;
    r->interactive = interactive;
    r->test = test;
    r->tag = tag;
    return CL_PTR_TO_OBJ(r);
}

CL_Obj cl_make_struct(CL_Obj type_name, uint32_t n_slots)
{
    uint32_t alloc_size;
    CL_Struct *st;

    /* Cap BEFORE computing alloc_size — see cl_make_vector.  Reachable
     * with corrupted FASL struct slot counts (raw u32 from disk). */
    if (n_slots > CL_MAX_STRUCT_SLOTS)
        cl_storage_error("MAKE-STRUCT: %u slots exceeds the maximum "
                         "heap object size (max %u)",
                         (unsigned)n_slots, (unsigned)CL_MAX_STRUCT_SLOTS);
    alloc_size = sizeof(CL_Struct) + n_slots * sizeof(CL_Obj);

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
    uint32_t alloc_size;
    CL_Bignum *bn;
    /* Cap BEFORE computing alloc_size — see cl_make_vector. */
    if (n_limbs > CL_MAX_BIGNUM_LIMBS)
        cl_storage_error("MAKE-BIGNUM: %u limbs exceeds the maximum "
                         "heap object size (max %u)",
                         (unsigned)n_limbs, (unsigned)CL_MAX_BIGNUM_LIMBS);
    alloc_size = sizeof(CL_Bignum) + n_limbs * sizeof(uint16_t);
    bn = (CL_Bignum *)cl_alloc(TYPE_BIGNUM, alloc_size);
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
    uint32_t nwords;
    uint32_t alloc_size;
    CL_BitVector *bv;
    /* Cap BEFORE CL_BV_WORDS — nbits near UINT32_MAX wraps the `+ 31`
     * arithmetic to a tiny word count (see mem.h). */
    if (nbits > CL_MAX_BV_BITS)
        cl_storage_error("MAKE-BIT-VECTOR: length %u exceeds the maximum "
                         "heap object size (max %u bits)",
                         (unsigned)nbits, (unsigned)CL_MAX_BV_BITS);
    nwords = CL_BV_WORDS(nbits);
    alloc_size = sizeof(CL_BitVector) + nwords * sizeof(uint32_t);
    bv = (CL_BitVector *)cl_alloc(TYPE_BIT_VECTOR, alloc_size);
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

#ifdef DEBUG_GC
void cl_gc_push_root_dbg(CL_Obj *root, const char *file, int line)
{
    if (gc_root_count > CL_GC_ROOT_STACK_SIZE || gc_root_count < 0) {
        fprintf(stderr, "[GC-ROOT-BUG] push_root_dbg (%s:%d): gc_root_count=%d is CORRUPT (max=%d)\n",
                file, line, gc_root_count, CL_GC_ROOT_STACK_SIZE);
        cl_capture_backtrace();
        fprintf(stderr, "%s", cl_backtrace_buf);
        fflush(stderr);
        abort();
    }
    if (gc_root_count < CL_GC_ROOT_STACK_SIZE) {
        CT->gc_root_files[gc_root_count] = file;
        CT->gc_root_lines[gc_root_count] = line;
        gc_root_stack[gc_root_count++] = root;
    } else {
        fprintf(stderr, "FATAL: GC root stack overflow (%d/%d) at %s:%d — increase CL_GC_ROOT_STACK_SIZE\n",
                gc_root_count, CL_GC_ROOT_STACK_SIZE, file, line);
        /* Dump run-length-compressed pusher sites: a root LEAK (push without
         * pop, usually across a longjmp) shows up as one site repeated
         * hundreds of times.  Only sites recorded via CL_GC_PROTECT carry
         * file/line; plain cl_gc_push_root entries show the previous
         * occupant's stale site — treat those with suspicion. */
        {
            int di, dj;
            for (di = 0; di < CL_GC_ROOT_STACK_SIZE; di = dj) {
                const char *df = CT->gc_root_files[di];
                int dl = CT->gc_root_lines[di];
                for (dj = di + 1; dj < CL_GC_ROOT_STACK_SIZE &&
                         CT->gc_root_files[dj] == df &&
                         CT->gc_root_lines[dj] == dl; dj++)
                    ;
                fprintf(stderr, "  roots[%4d..%4d] %s:%d (x%d)\n",
                        di, dj - 1, df ? df : "?", dl, dj - di);
            }
        }
        cl_capture_backtrace();
        fprintf(stderr, "%s", cl_backtrace_buf);
        fflush(stderr);
        abort();
    }
}
#endif

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

#if defined(DEBUG_GC) || defined(DEBUG_GC_STRESS)
/* Highest valid type tag — keep in sync with enum CL_ObjType (TYPE_WIDE_STRING
 * sits AFTER TYPE_RESTART when wide strings are compiled in). */
#ifdef CL_WIDE_STRINGS
#define GC_DBG_MAX_TYPE TYPE_WIDE_STRING
#else
#define GC_DBG_MAX_TYPE TYPE_RESTART
#endif
/* Object whose children are currently being pushed — provenance for the
 * gc_mark_push plausibility guard.  NULL while pushing from the root set. */
static void *gc_dbg_mark_parent;
/* Which root category / index the mark walk is currently processing —
 * provenance for badmarks that come from the root set itself. */
static const char *gc_dbg_root_src = "?";
static int gc_dbg_root_idx = -1;
#define GC_DBG_SRC(tag, idx) \
    (gc_dbg_mark_parent = NULL, gc_dbg_root_src = (tag), gc_dbg_root_idx = (idx))
#else
#define GC_DBG_SRC(tag, idx) ((void)0)
#endif

/* Double the mark stack's capacity (up to a heap-proportional cap).  Runs
 * with the world stopped and the stack contents are plain arena offsets, so
 * an alloc+copy+free swap is safe at any push.  Returns 1 on success, 0 when
 * growth is not possible (cap reached or platform_alloc OOM) — the caller
 * then falls back to the overflow re-scan protocol.  A failure latches for
 * the rest of the cycle so a heap under memory pressure doesn't re-attempt
 * the same failing allocation on every subsequent push. */
static int gc_mark_stack_grow(void)
{
    uint32_t max_cap, new_cap;
    CL_Obj *new_buf;

    if (gc_mark_grow_failed)
        return 0;

    /* Cap the stack's C-heap footprint at ~1/32 of the arena size (entries
     * are 4 bytes, so arena_size/128 entries).  Worst-case marking frontier
     * is bounded by the live object count, but that bound is far larger than
     * any real workload's frontier — the cap exists so a pathological graph
     * degrades to the (correct) re-scan fallback instead of doubling RAM
     * usage.  On the 68020/8MB target a 4MB heap caps at 32K entries (128KB),
     * covering e.g. a full 32K-element vector of fresh children. */
    max_cap = cl_heap.arena_size / 128u;
    if (max_cap < CL_GC_MARK_STACK_SIZE)
        max_cap = CL_GC_MARK_STACK_SIZE;
    if (gc_mark_stack_test_limit && max_cap > gc_mark_stack_test_limit)
        max_cap = gc_mark_stack_test_limit;

    if (gc_mark_stack_cap >= max_cap) {
        gc_mark_grow_failed = 1;
        return 0;
    }
    new_cap = gc_mark_stack_cap * 2u;
    if (new_cap > max_cap)
        new_cap = max_cap;

    new_buf = (CL_Obj *)platform_alloc((unsigned long)new_cap * sizeof(CL_Obj));
    if (!new_buf) {
        gc_mark_grow_failed = 1;
        return 0;
    }
    memcpy(new_buf, gc_mark_stack, (size_t)gc_mark_top * sizeof(CL_Obj));
    if (gc_mark_stack != gc_mark_stack_initial)
        platform_free(gc_mark_stack);
    gc_mark_stack = new_buf;
    gc_mark_stack_cap = new_cap;
    gc_mark_stack_grows++;
    return 1;
}

/* Release a grown mark stack back to the static initial buffer.  Called on
 * heap re-init/shutdown; NOT called between GC cycles — a workload that grew
 * the stack once will grow it again, so keeping the buffer (bounded by the
 * cap above) avoids per-GC alloc churn. */
static void gc_mark_stack_release(void)
{
    if (gc_mark_stack != gc_mark_stack_initial) {
        platform_free(gc_mark_stack);
        gc_mark_stack = gc_mark_stack_initial;
    }
    gc_mark_stack_cap = CL_GC_MARK_STACK_SIZE;
    gc_mark_top = 0;
}

void cl_gc_mark_stack_stats(uint32_t *cap_entries, uint32_t *grows,
                            uint32_t *rescan_passes)
{
    if (cap_entries)   *cap_entries   = gc_mark_stack_cap;
    if (grows)         *grows         = gc_mark_stack_grows;
    if (rescan_passes) *rescan_passes = gc_mark_rescan_passes;
}

void cl_gc_mark_stack_set_test_limit(uint32_t max_entries)
{
    gc_mark_stack_test_limit = max_entries;
}

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

#if defined(DEBUG_GC) || defined(DEBUG_GC_STRESS)
    /* Plausibility guard with provenance — see gc_mark_obj's twin.  Checking
     * at PUSH time (not pop) means gc_dbg_mark_parent still names the object
     * whose slot held the bad reference. */
    {
        void *cptr = CL_OBJ_TO_PTR(obj);
        if ((obj & (CL_ALIGN - 1)) != 0 || CL_HDR_SIZE(cptr) == 0 ||
            obj + CL_HDR_SIZE(cptr) > cl_heap.bump ||
            CL_HDR_TYPE(cptr) > GC_DBG_MAX_TYPE) {
            fprintf(stderr,
                    "[GC-BADMARK] gc_mark_push(0x%08x): implausible object "
                    "start (type=%u size=%u bump=0x%08x)\n",
                    (unsigned)obj, (unsigned)CL_HDR_TYPE(cptr),
                    (unsigned)CL_HDR_SIZE(cptr), (unsigned)cl_heap.bump);
            if (gc_dbg_mark_parent) {
                fprintf(stderr,
                        "  parent @0x%08x type=%u size=%u\n",
                        (unsigned)((uint8_t *)gc_dbg_mark_parent - cl_heap.arena),
                        (unsigned)CL_HDR_TYPE(gc_dbg_mark_parent),
                        (unsigned)CL_HDR_SIZE(gc_dbg_mark_parent));
            } else {
                fprintf(stderr, "  parent: (root set — no parent object)\n");
            }
            cl_capture_backtrace();
            fprintf(stderr, "%s", cl_backtrace_buf);
            fflush(stderr);
            abort();
        }
    }
#endif

    if (gc_mark_top >= gc_mark_stack_cap && !gc_mark_stack_grow()) {
        /* Growth impossible (cap/OOM): drop the child and let gc_mark's
         * full-arena re-scan loop recover it.  Slow (quadratic) but correct.
         * Warn once per process — a silent fallback here is how a 50s GC
         * pause hides from every normal build. */
        static int overflow_warned = 0;
        if (!overflow_warned) {
            overflow_warned = 1;
            platform_write_string("GC: mark stack cannot grow "
                                  "(OOM or cap); falling back to heap "
                                  "re-scan — expect slow GC cycles\n");
        }
        gc_mark_overflow = 1;
        return;
    }
    gc_mark_stack[gc_mark_top++] = obj;
}

/* --- Shared object-slot walker (used by mark and compaction-update) ---------
 * gc_mark_children and gc_update_children walk the identical per-type set of
 * CL_Obj slots; they differ only in the operation applied to each slot
 * (mark-push a value vs forward a slot in place) and in two type-specific
 * tails.  The layout is therefore written ONCE here as an X-macro and each
 * function instantiates it by defining GC_VISIT plus the two _TAIL hooks.
 * Because it is macro expansion the generated code is identical to two
 * hand-written switches — zero per-object overhead on this hot GC path.
 *
 * Callers must define, before invoking:
 *   GC_VISIT(slot)          operate on one CL_Obj lvalue slot
 *   GC_BYTECODE_TAIL(bc)    extra work for TYPE_BYTECODE (native reloc forward)
 *   GC_STREAM_TAIL(st)      extra work for TYPE_STREAM   (outbuf pin)
 * and #undef them afterward. */
#define GC_WALK_OBJ_CHILDREN(ptr, type)                                       \
    switch (type) {                                                           \
    case TYPE_CONS: {                                                         \
        CL_Cons *c = (CL_Cons *)(ptr);                                        \
        GC_VISIT(c->car);                                                     \
        GC_VISIT(c->cdr);                                                     \
        break;                                                               \
    }                                                                        \
    case TYPE_SYMBOL: {                                                       \
        CL_Symbol *s = (CL_Symbol *)(ptr);                                    \
        GC_VISIT(s->name);                                                    \
        if (s->value != CL_UNBOUND) GC_VISIT(s->value);                       \
        if (s->function != CL_UNBOUND) GC_VISIT(s->function);                 \
        GC_VISIT(s->plist);                                                   \
        GC_VISIT(s->package);                                                 \
        break;                                                               \
    }                                                                        \
    case TYPE_FUNCTION: {                                                     \
        CL_Function *f = (CL_Function *)(ptr);                                \
        GC_VISIT(f->name);                                                    \
        break;                                                               \
    }                                                                        \
    case TYPE_CLOSURE: {                                                      \
        CL_Closure *cl = (CL_Closure *)(ptr);                                 \
        uint32_t size = CL_HDR_SIZE(ptr);                                     \
        uint32_t n_upvals = (size - sizeof(CL_Closure)) / sizeof(CL_Obj);     \
        uint32_t i;                                                           \
        GC_VISIT(cl->bytecode);                                               \
        for (i = 0; i < n_upvals; i++)                                        \
            GC_VISIT(cl->upvalues[i]);                                        \
        break;                                                               \
    }                                                                        \
    case TYPE_BYTECODE: {                                                     \
        CL_Bytecode *bc = (CL_Bytecode *)(ptr);                               \
        uint16_t i;                                                           \
        GC_VISIT(bc->name);                                                   \
        GC_VISIT(bc->source_lambda_list);                                     \
        for (i = 0; i < bc->n_constants; i++)                                 \
            GC_VISIT(bc->constants[i]);                                       \
        if (bc->key_syms) {                                                   \
            for (i = 0; i < bc->n_keys; i++)                                  \
                GC_VISIT(bc->key_syms[i]);                                    \
        }                                                                     \
        GC_BYTECODE_TAIL(bc);                                                 \
        break;                                                               \
    }                                                                        \
    case TYPE_VECTOR: {                                                       \
        CL_Vector *v = (CL_Vector *)(ptr);                                    \
        uint32_t i;                                                           \
        if (v->flags & CL_VEC_FLAG_DISPLACED) {                              \
            GC_VISIT(v->data[CL_DISP_BASE_IDX(v)]);                           \
        } else {                                                             \
            uint32_t n_entries = (v->rank > 1) ? (uint32_t)v->rank + v->length \
                                               : v->length;                   \
            for (i = 0; i < n_entries; i++)                                   \
                GC_VISIT(v->data[i]);                                         \
        }                                                                    \
        break;                                                               \
    }                                                                        \
    case TYPE_PACKAGE: {                                                      \
        CL_Package *p = (CL_Package *)(ptr);                                  \
        GC_VISIT(p->name);                                                    \
        GC_VISIT(p->symbols);                                                 \
        GC_VISIT(p->use_list);                                                \
        GC_VISIT(p->nicknames);                                               \
        GC_VISIT(p->local_nicknames);                                         \
        GC_VISIT(p->shadowing_symbols);                                       \
        GC_VISIT(p->exported_symbols);                                        \
        break;                                                               \
    }                                                                        \
    case TYPE_HASHTABLE: {                                                    \
        CL_Hashtable *ht = (CL_Hashtable *)(ptr);                             \
        uint32_t i;                                                           \
        GC_VISIT(ht->bucket_vec);                                             \
        if (!CL_NULL_P(ht->bucket_vec)) {                                     \
            /* Buckets live in the external vector; walking it covers them */ \
        } else {                                                             \
            for (i = 0; i < ht->bucket_count; i++)                            \
                GC_VISIT(ht->buckets[i]);                                     \
        }                                                                    \
        break;                                                               \
    }                                                                        \
    case TYPE_CONDITION: {                                                    \
        CL_Condition *cond = (CL_Condition *)(ptr);                           \
        GC_VISIT(cond->type_name);                                            \
        GC_VISIT(cond->slots);                                                \
        GC_VISIT(cond->report_string);                                       \
        break;                                                               \
    }                                                                        \
    case TYPE_RESTART: {                                                      \
        CL_Restart *r = (CL_Restart *)(ptr);                                  \
        GC_VISIT(r->name);                                                    \
        GC_VISIT(r->function);                                                \
        GC_VISIT(r->report);                                                  \
        GC_VISIT(r->interactive);                                             \
        GC_VISIT(r->test);                                                    \
        GC_VISIT(r->tag);                                                     \
        break;                                                               \
    }                                                                        \
    case TYPE_STRUCT: {                                                       \
        CL_Struct *st = (CL_Struct *)(ptr);                                   \
        uint32_t i;                                                           \
        GC_VISIT(st->type_desc);                                              \
        for (i = 0; i < st->n_slots; i++)                                     \
            GC_VISIT(st->slots[i]);                                           \
        break;                                                               \
    }                                                                        \
    case TYPE_STREAM: {                                                       \
        CL_Stream *st = (CL_Stream *)(ptr);                                   \
        GC_VISIT(st->string_buf);                                             \
        GC_VISIT(st->element_type);                                           \
        GC_STREAM_TAIL(st);                                                   \
        break;                                                               \
    }                                                                        \
    case TYPE_RATIO: {                                                        \
        CL_Ratio *r = (CL_Ratio *)(ptr);                                      \
        GC_VISIT(r->numerator);                                               \
        GC_VISIT(r->denominator);                                             \
        break;                                                               \
    }                                                                        \
    case TYPE_COMPLEX: {                                                      \
        CL_Complex *cx = (CL_Complex *)(ptr);                                 \
        GC_VISIT(cx->realpart);                                               \
        GC_VISIT(cx->imagpart);                                              \
        break;                                                               \
    }                                                                        \
    case TYPE_PATHNAME: {                                                     \
        CL_Pathname *pn = (CL_Pathname *)(ptr);                               \
        GC_VISIT(pn->host);                                                   \
        GC_VISIT(pn->device);                                                 \
        GC_VISIT(pn->directory);                                             \
        GC_VISIT(pn->name);                                                   \
        GC_VISIT(pn->type);                                                   \
        GC_VISIT(pn->version);                                                \
        break;                                                               \
    }                                                                        \
    case TYPE_CELL: {                                                         \
        CL_Cell *cell = (CL_Cell *)(ptr);                                     \
        GC_VISIT(cell->value);                                                \
        break;                                                               \
    }                                                                        \
    case TYPE_THREAD: {                                                       \
        CL_ThreadObj *to = (CL_ThreadObj *)(ptr);                            \
        GC_VISIT(to->name);                                                   \
        GC_VISIT(to->result);                                                 \
        break;                                                               \
    }                                                                        \
    case TYPE_LOCK: {                                                         \
        CL_Lock *lk = (CL_Lock *)(ptr);                                       \
        GC_VISIT(lk->name);                                                   \
        break;                                                               \
    }                                                                        \
    case TYPE_CONDVAR: {                                                      \
        CL_CondVar *cv = (CL_CondVar *)(ptr);                                 \
        GC_VISIT(cv->name);                                                   \
        break;                                                               \
    }                                                                        \
    case TYPE_STRING:                                                         \
    case TYPE_BIGNUM:                                                         \
    case TYPE_SINGLE_FLOAT:                                                    \
    case TYPE_DOUBLE_FLOAT:                                                    \
    case TYPE_RANDOM_STATE:                                                    \
    case TYPE_BIT_VECTOR:                                                      \
    case TYPE_FOREIGN_POINTER:                                                 \
    CL_GC_WIDE_STRING_CASE                                                     \
        /* No CL_Obj children — raw numeric/state/byte data */                \
        break;                                                               \
    default:                                                                  \
        break;                                                               \
    }

#ifdef CL_WIDE_STRINGS
#define CL_GC_WIDE_STRING_CASE case TYPE_WIDE_STRING:
#else
#define CL_GC_WIDE_STRING_CASE
#endif

static void gc_mark_children(void *ptr, uint8_t type)
{
#if defined(DEBUG_GC) || defined(DEBUG_GC_STRESS)
    gc_dbg_mark_parent = ptr;
#endif
#define GC_VISIT(slot) gc_mark_push(slot)
#define GC_BYTECODE_TAIL(bc) ((void)(bc))
    /* Pin this live output stream's outbuf slot so the post-mark reclaim
     * (cl_stream_outbuf_gc_reclaim) doesn't free a buffer still in use. */
#define GC_STREAM_TAIL(st)                                                    \
    do {                                                                      \
        if (((st)->direction & CL_STREAM_OUTPUT) && (st)->out_buf_handle != 0)\
            cl_stream_outbuf_gc_mark_use((st)->out_buf_handle);               \
    } while (0)
    GC_WALK_OBJ_CHILDREN(ptr, type);
#undef GC_VISIT
#undef GC_BYTECODE_TAIL
#undef GC_STREAM_TAIL
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
#if defined(DEBUG_GC) || defined(DEBUG_GC_STRESS)
    /* Plausibility guard: marking an offset that is not a real object start
     * ORs CL_HDR_MARK_BIT into arbitrary object interiors (observed: a
     * hashtable's flags word and bucket slot), which the compactor then
     * treats as live headers — silent corruption far from the culprit.
     * A stale/interior/double-forwarded reference is the only way to get
     * here; abort loudly with the offending offset so the referrer can be
     * hunted while it is still on the stack. */
    if ((obj & (CL_ALIGN - 1)) != 0 || CL_HDR_SIZE(ptr) == 0 ||
        obj + CL_HDR_SIZE(ptr) > cl_heap.bump ||
        CL_HDR_TYPE(ptr) > GC_DBG_MAX_TYPE) {
        fprintf(stderr,
                "[GC-BADMARK] gc_mark_obj(0x%08x): implausible object start "
                "(type=%u size=%u bump=0x%08x) — interior or stale offset\n"
                "  source: %s[%d]%s\n",
                (unsigned)obj, (unsigned)CL_HDR_TYPE(ptr),
                (unsigned)CL_HDR_SIZE(ptr), (unsigned)cl_heap.bump,
                gc_dbg_root_src, gc_dbg_root_idx,
                gc_dbg_mark_parent ? " (via object children)" : "");
#ifdef DEBUG_GC
        if (gc_dbg_root_idx >= 0 && strcmp(gc_dbg_root_src, "gc_roots") == 0 &&
            CT->gc_root_files[gc_dbg_root_idx])
            fprintf(stderr, "  root pushed at %s:%d\n",
                    CT->gc_root_files[gc_dbg_root_idx],
                    CT->gc_root_lines[gc_dbg_root_idx]);
#endif
        cl_capture_backtrace();
        fprintf(stderr, "%s", cl_backtrace_buf);
        fflush(stderr);
        abort();
    }
#endif
    CL_HDR_SET_MARK(ptr);
    gc_mark_children(ptr, CL_HDR_TYPE(ptr));
}

/* Conservatively scan a thread's m68k stack for CL_Obj values
 * spilled by JIT'd code.
 *
 * Two-phase to avoid the moving-GC corruption hazard:
 *
 *   Phase 1 — collect: walk 4-byte-aligned words in [scan_lo,
 *   scan_hi).  A word w is a *candidate* if CL_HEAP_P(w) and
 *   w < arena_size.  Collected into a stack-local bounded buffer.
 *
 *   Phase 2 — validate-and-mark: walk the arena bump-front.  At
 *   each real header offset X, binary-search the sorted candidate
 *   buffer; if X is present, call gc_mark_obj(X).  This guarantees
 *   we only call CL_HDR_SET_MARK on offsets that are actual object
 *   starts — phantom marks on mid-object bytes (which would corrupt
 *   neighbouring data and break the moving compactor's relocations)
 *   are impossible.
 *
 * With validation in place the compaction inhibit (compaction
 * suppressed while cl_jit_active_threads > 0) is no longer
 * required for correctness.
 *
 * Capacity policy: the candidate buffer is a persistent, grow-on-demand
 * allocation held in mem.c static storage.  Pre-sized once per call to
 * the scan window's word count (an exact upper bound on the number of
 * candidates), so overflow is impossible by construction.  Across GC
 * cycles the buffer is reused; it only grows when a larger scan window
 * is encountered, so steady state is zero allocations per GC.
 *
 * Only runs when t->jit_depth > 0 and t is the current thread —
 * non-current threads stop at bytecode-VM safepoints where their
 * jit_depth is 0 by construction (JIT'd code does not yet emit its
 * own safepoints).
 *
 * See specs/native-backend.md §"GC interaction" option A → B-lite. */

static int jit_scan_cand_reserve(int need)
{
    int new_cap;
    uint32_t *buf;
    if (jit_scan_cand_cap >= need) return 1;
    new_cap = jit_scan_cand_cap ? jit_scan_cand_cap : 256;
    while (new_cap < need) new_cap *= 2;
    buf = (uint32_t *)platform_alloc((unsigned long)new_cap * sizeof(uint32_t));
    if (!buf) return 0;
    if (jit_scan_cand_buf) platform_free(jit_scan_cand_buf);
    jit_scan_cand_buf = buf;
    jit_scan_cand_cap = new_cap;
    return 1;
}

static int cand_cmp(const void *a, const void *b)
{
    uint32_t aa = *(const uint32_t *)a;
    uint32_t bb = *(const uint32_t *)b;
    return (aa < bb) ? -1 : (aa > bb) ? 1 : 0;
}

/* Append a validated pinned offset.  Called from gc_scan_jit_native_stack's
 * ascending arena walk, so the array stays sorted per scan; skip
 * consecutive duplicates (multiple stack slots can reference the same
 * object).  On OOM the object is still marked live (gc_mark_obj already
 * ran) but it is NOT safe to simply leave it unpinned: a compaction would
 * relocate it while the JIT frame's spilled offset stays stale — silent
 * corruption on resume.  Set gc_jit_pin_oom so cl_gc_compact degrades to
 * a non-moving mark+sweep for this cycle. */
static void jit_pin_record(uint32_t offset)
{
    if (jit_pinned_count > 0 && jit_pinned[jit_pinned_count - 1] == offset)
        return;
    if (jit_pinned_count >= jit_pinned_cap) {
        int new_cap = jit_pinned_cap ? jit_pinned_cap * 2 : 256;
        uint32_t *buf = (uint32_t *)platform_alloc(
            (unsigned long)new_cap * sizeof(uint32_t));
        if (!buf) {
            static int pin_oom_warned = 0;
            gc_jit_pin_oom = 1;
            if (!pin_oom_warned) {
                pin_oom_warned = 1;
                platform_write_string(
                    "GC: JIT pin-table allocation failed — compaction "
                    "suppressed for this cycle (mark+sweep only)\n");
            }
            return;
        }
        if (jit_pinned) {
            memcpy(buf, jit_pinned, (size_t)jit_pinned_count * sizeof(uint32_t));
            platform_free(jit_pinned);
        }
        jit_pinned = buf;
        jit_pinned_cap = new_cap;
    }
    jit_pinned[jit_pinned_count++] = offset;
}

static int cand_bsearch(const uint32_t *arr, int n, uint32_t key)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (arr[mid] == key) return 1;
        if (arr[mid] < key) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0;
}

/* Snapshot the free list into jit_scan_free_snap, sorted ascending.
 * Leaves jit_scan_free_valid = 0 on OOM (callers fall back to a linear
 * free-list walk).  Called at most once per GC cycle — gc_mark resets
 * the valid flag alongside jit_pinned_count. */
static void jit_scan_collect_free_snapshot(void)
{
    uint32_t cur;
    int n = 0;

    for (cur = cl_heap.free_list; cur;
         cur = ((CL_FreeBlock *)(cl_heap.arena + cur))->next_offset)
        n++;

    if (n > jit_scan_free_cap) {
        int new_cap = jit_scan_free_cap ? jit_scan_free_cap : 256;
        uint32_t *buf;
        while (new_cap < n) new_cap *= 2;
        buf = (uint32_t *)platform_alloc((unsigned long)new_cap * sizeof(uint32_t));
        if (!buf) return;
        if (jit_scan_free_snap) platform_free(jit_scan_free_snap);
        jit_scan_free_snap = buf;
        jit_scan_free_cap = new_cap;
    }

    n = 0;
    for (cur = cl_heap.free_list; cur;
         cur = ((CL_FreeBlock *)(cl_heap.arena + cur))->next_offset)
        jit_scan_free_snap[n++] = cur;

    if (n > 1)
        qsort(jit_scan_free_snap, (size_t)n, sizeof(uint32_t), cand_cmp);
    jit_scan_free_count = n;
    jit_scan_free_valid = 1;
}

/* Is `offset` the start of a free-list block?  Uses the per-cycle
 * snapshot when available, else walks the free list directly. */
static int gc_offset_is_free_block(uint32_t offset)
{
    if (jit_scan_free_valid)
        return cand_bsearch(jit_scan_free_snap, jit_scan_free_count, offset);
    {
        uint32_t cur;
        for (cur = cl_heap.free_list; cur;
             cur = ((CL_FreeBlock *)(cl_heap.arena + cur))->next_offset) {
            if (cur == offset) return 1;
        }
    }
    return 0;
}

static void gc_scan_jit_native_stack(CL_Thread *t)
{
    uint32_t *candidates;
    int n_cand = 0;
    int upper;
    int cand_cap;
    char *scan_lo;
    char *scan_hi;
    uintptr_t addr;
    uint8_t *p;
    uint8_t *end;
    static int oom_warned = 0;

    if (t->jit_depth <= 0 || t->jit_stack_top == NULL) return;

    /* Lower bound of the scan window.
     *  - Current (compacting) thread: a local in THIS frame — strictly below
     *    any JIT'd frame on the C call chain leading here.
     *  - A stopped PEER thread (multi-thread STW): its own frozen stack
     *    pointer, captured when it parked at a safepoint / safe region.  We
     *    cannot use our own SP — the peer runs on a different C stack.  Without
     *    this, a peer parked inside JIT'd code had its spilled operands neither
     *    marked nor pinned, so the compactor relocated the referenced objects
     *    and the peer resumed with dangling offsets (the multi-thread JIT-vs-
     *    moving-GC corruption). */
    if (t == cl_get_current_thread()) {
        scan_lo = (char *)&scan_lo;
    } else {
        if (t->jit_park_sp == NULL) return;
        scan_lo = (char *)t->jit_park_sp;
    }
    scan_hi = (char *)t->jit_stack_top;
    if (scan_hi <= scan_lo) return;

    /* Round up to 4-byte alignment (CL_Obj is 32 bits). */
    addr = (uintptr_t)scan_lo;
    addr = (addr + 3u) & ~(uintptr_t)3u;
    if (addr + 4 > (uintptr_t)scan_hi) return;

    /* Exact upper bound: one slot per 4-byte aligned word in the window. */
    upper = (int)(((uintptr_t)scan_hi - addr) / 4);
    if (jit_scan_cand_reserve(upper)) {
        candidates = jit_scan_cand_buf;
        cand_cap = upper;
    } else {
        /* OOM fallback: a SKIPPED scan would leave JIT-only-reachable
         * objects unmarked — the following sweep frees them under the
         * live JIT frame (real corruption, not merely "less precise").
         * Degrade to chunked scanning through a small static buffer
         * instead: correct, needs no memory, costs one extra arena walk
         * per full chunk. */
        static uint32_t emergency_buf[256];
        if (!oom_warned) {
            oom_warned = 1;
            platform_write_string(
                "GC: JIT native-stack scan buffer allocation failed — "
                "falling back to chunked scanning (slower, still safe)\n");
        }
        candidates = emergency_buf;
        cand_cap = (int)(sizeof(emergency_buf) / sizeof(emergency_buf[0]));
    }

    /* Chunked scan: collect up to cand_cap candidate offsets, resolve
     * them against the arena, repeat until the window is exhausted.  The
     * normal path (cand_cap == upper) runs exactly one iteration. */
    while (addr + 4 <= (uintptr_t)scan_hi) {
        /* Phase 1: collect candidate offsets. */
        n_cand = 0;
        for (; addr + 4 <= (uintptr_t)scan_hi && n_cand < cand_cap; addr += 4) {
            CL_Obj w = *(const CL_Obj *)(uintptr_t)addr;
            if (CL_NULL_P(w) || CL_FIXNUM_P(w) || CL_CHAR_P(w)) continue;
            if (w >= cl_heap.arena_size) continue;
            candidates[n_cand++] = (uint32_t)w;
        }

        if (n_cand == 0) break;   /* window exhausted with no candidates */

        /* Sort candidates so the arena walk can binary-search. */
        qsort(candidates, n_cand, sizeof(candidates[0]), cand_cmp);

        /* Snapshot the free list once per GC cycle so the walk below can
         * reject candidates that point at free blocks (their header word is
         * a raw size that masquerades as an unmarked TYPE_CONS). */
        if (!jit_scan_free_valid)
            jit_scan_collect_free_snapshot();

        /* Phase 2: walk the arena bump-front; for each real header
         * offset that appears in `candidates`, mark it.  Walking from
         * CL_ALIGN (offset 0 is reserved for NIL) by header size,
         * matching gc_sweep's iteration. */
        p   = cl_heap.arena + CL_ALIGN;
        end = cl_heap.arena + cl_heap.bump;
        while (p < end) {
            uint32_t size = CL_HDR_SIZE(p);
            uint32_t offset;
            if (size == 0) break;       /* defensive: malformed header */
            offset = (uint32_t)(p - cl_heap.arena);
            if (cand_bsearch(candidates, n_cand, offset) &&
                !gc_offset_is_free_block(offset)) {
                gc_mark_obj((CL_Obj)offset);
                /* Pin it: the compactor must not move an object reachable
                 * only through a conservative (offset-valued) C-stack
                 * reference, or the JIT's spilled pointer would dangle.
                 * Ascending within one chunk; gc_compute_forwarding sorts
                 * the aggregate (chunks/threads can interleave). */
                jit_pin_record(offset);
            }
            p += size;
        }
    }
}

/* Mark all per-thread roots for a single thread.
 * Called during STW GC — no locking needed, thread is stopped.
 *
 * We must #undef gc_root_count here because thread.h defines it as
 * (CT->gc_root_count), which collides with t->gc_root_count member access. */
#undef gc_root_count

/* Audit the GC root registries for double registration.
 *
 * gc_forward is NOT idempotent (the forwarding table is keyed by
 * pre-compaction offsets), so a CL_Obj slot reachable from two root
 * entries is forwarded twice on compaction: the second lookup maps the
 * already-forwarded offset through whatever object's OLD offset it now
 * coincides with, silently rewriting the root to an unrelated object.
 * Every slot must therefore appear at most once across global_roots and
 * all thread root stacks combined.  Returns the number of violations
 * and prints one line per offender so the source can be found.
 *
 * Walks cl_thread_list and every thread's gc_roots[]/gc_root_count, which
 * are otherwise only safe to read under a stop-the-world pause (peer
 * threads mutate their own root stack via CL_GC_PROTECT/UNPROTECT, and
 * thread create/exit mutates the list's `next` links).  Since this is a
 * diagnostic entry point reachable from Lisp at any time (ext:%gc-audit-roots),
 * bracket the walk with cl_gc_stop_the_world()/cl_gc_resume_the_world()
 * ourselves, matching the pattern in cl_gc_compact(). */
int cl_gc_audit_roots(void)
{
    int violations = 0;
    int i, j;
    CL_Thread *t, *t2;
    int multithread = (cl_thread_count > 1);

    if (multithread)
        cl_gc_stop_the_world();

    for (i = 0; i < n_global_roots; i++) {
        for (j = i + 1; j < n_global_roots; j++) {
            if (global_roots[i] == global_roots[j]) {
                fprintf(stderr, "GC root audit: address registered twice "
                        "in global roots\n");
                violations++;
            }
        }
        for (t = cl_thread_list; t; t = t->next) {
            for (j = 0; j < t->gc_root_count; j++) {
                if (t->gc_roots[j] == global_roots[i]) {
                    fprintf(stderr, "GC root audit: global root also on a "
                            "thread root stack (double-forward hazard)\n");
                    violations++;
                }
            }
        }
    }

    /* Duplicate address registered twice within the same thread's own
     * gc_roots[] stack (e.g. an accidental double CL_GC_PROTECT). */
    for (t = cl_thread_list; t; t = t->next) {
        for (i = 0; i < t->gc_root_count; i++) {
            for (j = i + 1; j < t->gc_root_count; j++) {
                if (t->gc_roots[i] == t->gc_roots[j]) {
                    fprintf(stderr, "GC root audit: address registered "
                            "twice on the same thread's root stack\n");
                    violations++;
                }
            }
        }
    }

    /* Same address registered on two different threads' root stacks. */
    for (t = cl_thread_list; t; t = t->next) {
        for (t2 = t->next; t2; t2 = t2->next) {
            for (i = 0; i < t->gc_root_count; i++) {
                for (j = 0; j < t2->gc_root_count; j++) {
                    if (t->gc_roots[i] == t2->gc_roots[j]) {
                        fprintf(stderr, "GC root audit: address registered "
                                "on two different thread root stacks "
                                "(double-forward hazard)\n");
                        violations++;
                    }
                }
            }
        }
    }

    /* Informational: roots aliasing an independently-forwarded thread
     * region (VM stack, mv_values, pending throw values, dyn/handler/
     * restart stacks, vm_extra_args, compiler chain).  Harmless by
     * construction — gc_update_registered_roots skips them — but each
     * marks a redundant CL_GC_PROTECT worth removing at the source.
     * Not counted as violations. */
    for (t = cl_thread_list; t; t = t->next) {
        for (i = 0; i < t->gc_root_count; i++) {
            if (root_slot_independently_forwarded(t->gc_roots[i])) {
                fprintf(stderr, "GC root audit: note — thread root #%d "
                        "aliases an independently-forwarded thread region "
                        "(redundant CL_GC_PROTECT; skipped by the root "
                        "dedup pass)\n", i);
            }
        }
    }

    if (multithread)
        cl_gc_resume_the_world();

    return violations;
}

static void gc_mark_thread_roots(CL_Thread *t)
{
    int i;

    /* GC root stack */
    for (i = 0; i < t->gc_root_count; i++) {
        GC_DBG_SRC("gc_roots", i);
        gc_mark_obj(*t->gc_roots[i]);
    }

    /* Dynamic binding stack (saved old values) */
    for (i = 0; i < t->dyn_top; i++) {
        GC_DBG_SRC("dyn_stack", i);
        gc_mark_obj(t->dyn_stack[i].symbol);
        gc_mark_obj(t->dyn_stack[i].old_value);
    }

    /* NLX stack (catch tags, results, and saved bytecodes).
     * NOTE: nlx_stack[i].mv_values are deliberately NOT marked: they are
     * written together with mv_count only in the zero-allocation window
     * between a throw's stash and its longjmp landing (immediately
     * consumed into cl_mv_values); outside that window mv_count is 1
     * from the frame push while mv_values[0] holds a stale/garbage word
     * — marking it would set mark bits at non-object-start offsets.
     * Values that live across an ALLOCATING unwind-protect cleanup are
     * parked in pending_mv_values / saved_pending_stack, marked below. */
    for (i = 0; i < t->nlx_top; i++) {
        GC_DBG_SRC("nlx_stack", i);
        gc_mark_obj(t->nlx_stack[i].tag);
        gc_mark_obj(t->nlx_stack[i].result);
        gc_mark_obj(t->nlx_stack[i].bytecode);
    }

    /* Handler stack */
    for (i = 0; i < t->handler_top; i++) {
        GC_DBG_SRC("handler_stack", i);
        gc_mark_obj(t->handler_stack[i].type_name);
        gc_mark_obj(t->handler_stack[i].handler);
    }

    /* Restart stack */
    for (i = 0; i < t->restart_top; i++) {
        GC_DBG_SRC("restart_stack", i);
        gc_mark_obj(t->restart_stack[i].name);
        gc_mark_obj(t->restart_stack[i].handler);
        gc_mark_obj(t->restart_stack[i].tag);
        gc_mark_obj(t->restart_stack[i].restart);
    }

    /* VM execution stack */
    if (t->vm.stack) {
        for (i = 0; i < t->vm.sp; i++) {
            GC_DBG_SRC("vm.stack", i);
            gc_mark_obj(t->vm.stack[i]);
        }
    }

    /* JIT native stack — conservative scan when this thread is
     * currently inside JIT'd code.  No-op otherwise. */
    gc_scan_jit_native_stack(t);

    /* Bytecode objects referenced by active VM frames */
    for (i = 0; i < t->vm.fp; i++) {
        GC_DBG_SRC("vm.frames.bytecode", i);
        gc_mark_obj(t->vm.frames[i].bytecode);
    }

    /* Multiple values and pending throw state */
    for (i = 0; i < CL_MAX_MV; i++) {
        GC_DBG_SRC("mv_values", i);
        gc_mark_obj(t->mv_values[i]);
    }
    /* NOTE: pre_call_mv_values is deliberately NOT marked (nor forwarded
     * in gc_update_thread_roots).  call_builtin (vm.c) snapshots mv_values
     * into it immediately before every builtin invocation, and the only
     * consumers (THROW's NLX capture paths in builtins_io.c) read it back
     * BEFORE any allocating call in that window — verified zero-alloc, so
     * no GC can observe a live-but-unmarked value there.  Outside that
     * window the array holds stale offsets from completed calls; marking
     * those would set mark bits at non-object-start offsets and corrupt
     * the arena walk (same hazard as nlx_stack[i].mv_values above).
     * REGRESSION HAZARD: if an NLX builtin ever allocates before
     * consuming pre_call_mv_values, these slots must instead be marked +
     * forwarded under a validity flag, like pending_mv_values below. */
    GC_DBG_SRC("pending_tag/value", 0);
    gc_mark_obj(t->pending_tag);
    gc_mark_obj(t->pending_value);
    /* Secondary values of an in-flight THROW.  These are live while an
     * unwind-protect cleanup runs (arbitrary allocating Lisp) between
     * the throw and the catch landing; without marking, a sweep during
     * the cleanup collects them and a compaction leaves stale offsets.
     * Bound STRICTLY by pending_mv_count while a throw is in flight:
     * unlike mv_values (continuously maintained), slots beyond the
     * count — or the whole array when no throw is pending — hold stale
     * offsets from completed throws; marking those would set mark bits
     * at non-object-start offsets and corrupt the arena walk. */
    if (t->pending_throw) {
        for (i = 0; i < t->pending_mv_count && i < CL_MAX_MV; i++)
            gc_mark_obj(t->pending_mv_values[i]);
    }
    /* Saved pending-throw snapshots: one per armed unwind-protect whose
     * cleanup is running (nested UWPs park the OUTER throw's tag/values
     * here — see saved_pending_stack in thread.h). */
    for (i = 0; i < t->saved_pending_top; i++) {
        int m;
        /* A snapshot taken while no throw was in flight copies whatever
         * stale tag/value a COMPLETED throw left behind — only entries
         * with an armed pending_throw hold live references. */
        if (!t->saved_pending_stack[i].pending_throw)
            continue;
        gc_mark_obj(t->saved_pending_stack[i].pending_tag);
        gc_mark_obj(t->saved_pending_stack[i].pending_value);
        for (m = 0; m < t->saved_pending_stack[i].pending_mv_count &&
                    m < CL_MAX_MV; m++)
            gc_mark_obj(t->saved_pending_stack[i].pending_mv_values[m]);
    }
    /* Compiler hand-off: holds a CL_Obj symbol across compile_expr's allocations
     * (set in compile_named_lambda/compile_defun, consumed in compile_lambda).
     * Must be marked AND updated or a compaction mid-lambda-compile leaves
     * bc->name a stale offset under GC stress. */
    GC_DBG_SRC("pending_lambda_name", 0);
    gc_mark_obj(t->pending_lambda_name);

    /* Thread metadata */
    GC_DBG_SRC("thread-metadata", 0);
    gc_mark_obj(t->name);
    gc_mark_obj(t->result);
    gc_mark_obj(t->interrupt_func);
    gc_mark_obj(t->thread_obj);

    /* Current lexical env installed for a macro expander — keeps the
     * &environment alist alive while the expander runs. */
    GC_DBG_SRC("current_lex_env", 0);
    gc_mark_obj(t->current_lex_env);

    /* Reader state — in-flight reader stream plus per-read uninterned
     * symbol alist (so #:foo identity survives a GC during a long READ). */
    GC_DBG_SRC("reader-state", 0);
    gc_mark_obj(t->rd_stream);
    gc_mark_obj(t->rd_uninterned);
    gc_mark_obj(t->rd_labels);

    /* Printer in-progress object stack (re-entrancy detection in
     * print-object-hook).  Pinned across hook apply so a GC inside the
     * Lisp print-object method doesn't sweep the object whose recursion
     * we're guarding. */
    for (i = 0; i < t->pr_inprog_top; i++) {
        GC_DBG_SRC("pr_inprog", i);
        gc_mark_obj(t->pr_inprog[i]);
    }

    /* Current printer output target (format/print/write).  Held in a thread
     * field for the whole print, across allocating sub-prints (print-object
     * hooks, string-stream outbuf growth).  A peer thread's compaction while
     * this thread is parked mid-print relocates the stream object; without
     * marking+forwarding this slot, out_char/out_str dereference a stale
     * offset and scribble into freed arena — the multi-thread `(format nil …)`
     * corruption.  Single-writer (owning thread), stopped during STW GC. */
    GC_DBG_SRC("pr_stream", 0);
    gc_mark_obj(t->pr_stream);

    /* *print-circle* detection table keys — live CL_Obj references while a
     * circular/shared print is in progress (pr_circle_active).  Same hazard
     * as pr_stream: relocated out from under the parked printer thread. */
    if (t->pr_circle_active) {
        int ci;
        for (ci = 0; ci < CL_CIRCLE_HT_SIZE; ci++) {
            if (!CL_NULL_P(t->pr_circle_keys[ci])) {
                GC_DBG_SRC("pr_circle_keys", ci);
                gc_mark_obj(t->pr_circle_keys[ci]);
            }
        }
    }

    /* Pending LOAD-TIME-VALUE (cell, thunk) pairs (compile-file only) */
    for (i = 0; i < t->ltv_init_count; i++) {
        GC_DBG_SRC("ltv_init", i);
        gc_mark_obj(t->ltv_init_cells[i]);
        gc_mark_obj(t->ltv_init_thunks[i]);
    }

    /* Compiler constants (active compilers may hold CL_Obj values
     * in platform_alloc'd memory not reachable from the GC arena) */
    GC_DBG_SRC("compiler-constants", 0);
    {
        extern void cl_compiler_gc_mark_thread(CL_Thread *t);
        cl_compiler_gc_mark_thread(t);
    }

    /* VM-internal buffers (e.g. vm_extra_args during &rest processing) */
    GC_DBG_SRC("vm_extra_args", 0);
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
                GC_DBG_SRC("tlv_table", ti);
                gc_mark_obj(sym);
                gc_mark_obj(t->tlv_table[ti].value);
            }
        }
    }
    GC_DBG_SRC("shared-globals", 0);
}
/* Restore gc_root_count macro for the rest of mem.c */
#define gc_root_count (CT->gc_root_count)

static void gc_mark(void)
{
    CL_Thread *t;

    gc_mark_overflow = 0;
    /* Re-arm mark-stack growth: a previous cycle's OOM may have been
     * transient (the failing platform_alloc can succeed now), and the
     * heap-proportional cap re-derives from the current arena anyway. */
    gc_mark_grow_failed = 0;
    /* Reset the pinned-object set; the conservative JIT-stack scan
     * (gc_scan_jit_native_stack, reached via gc_mark_thread_roots) rebuilds
     * it this cycle.  Stays empty unless a JIT frame is live. */
    jit_pinned_count = 0;
    gc_jit_pin_oom = 0;
    /* Invalidate the free-list snapshot — the free list has changed since
     * the last cycle; the first JIT-stack scan this cycle re-collects it. */
    jit_scan_free_valid = 0;

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
    gc_mark_obj(compiler_macro_table);
    gc_mark_obj(cl_clos_class_table);
    gc_mark_obj(struct_table);
    gc_mark_obj(condition_hierarchy);
    gc_mark_obj(condition_slot_table);
    gc_mark_obj(condition_default_initargs);
    gc_mark_obj(condition_slot_initforms);

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

    /* Active FASL readers — their gensym_objs[]/shared_objs[] dedup tables
     * hold CL_Obj references that forward GENSYM_REF/OBJ_REF resolve through
     * but which may not yet be reachable from the graph under construction. */
    {
        extern void cl_fasl_gc_mark_readers(void);
        cl_fasl_gc_mark_readers();
    }

    /* Active FASL WRITERS — bi_load's auto-cache writer holds gensym dedup
     * entries across the interleaved compile/eval of later forms. */
    {
        extern void cl_fasl_gc_mark_writers(void);
        cl_fasl_gc_mark_writers();
    }

    /* Active MAKE-LOAD-FORM writer pre-pass — its walk/result arrays hold
     * CL_Obj offsets that a compaction during a user MAKE-LOAD-FORM method
     * must keep live and forward. */
    {
        extern void cl_fasl_gc_mark_mlf(void);
        cl_fasl_gc_mark_mlf();
    }

    /* Drain mark stack iteratively (children pushed by gc_mark_obj above).
     * Do NOT clear gc_mark_overflow here — it may have been set during
     * root marking above, and the re-scan loop below must handle it. */
    while (gc_mark_top > 0) {
        CL_Obj obj = gc_mark_stack[--gc_mark_top];
        gc_mark_obj(obj);
    }

    /* Handle mark stack overflow: re-scan heap for marked objects whose
     * children may not have been pushed.  Repeat until no overflow.
     * Only reachable when gc_mark_stack_grow failed (cap/OOM) — each pass
     * is O(arena) and recovers at most ~one-stack-full of dropped children,
     * so this is the quadratic last resort, not the normal path.  The pass
     * counter feeds ext:%gc-mark-stats so the fallback is observable. */
    while (gc_mark_overflow) {
        uint8_t *ptr = cl_heap.arena + CL_ALIGN;
        uint8_t *end = cl_heap.arena + cl_heap.bump;

        gc_mark_overflow = 0;
        gc_mark_rescan_passes++;
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

/* Release external resources owned by a dead heap object.
 * Called from gc_sweep with the world stopped, so the per-table mutex used
 * by cl_lock_table_alloc / cl_condvar_table_alloc is not needed: no other
 * thread can mutate the tables here. */
/* R-srcloc: the reader's cons→source-line table is keyed by arena OFFSETS.
 * Two staleness modes corrupt its diagnostics: (a) a key whose cons died —
 * a later object allocated at the same offset false-matches and reports a
 * WRONG file/line; (b) after compaction, a surviving cons moved and its
 * (correct) entry becomes unreachable while its old offset may false-match.
 * Fix: clear dead keys right after marking (mark bits still set), and
 * forward surviving keys while the forwarding table is alive. */
static void gc_srcloc_invalidate_dead(void)
{
    uint32_t i;
    for (i = 0; i < CL_SRCLOC_SIZE; i++) {
        CL_Obj k = cl_srcloc_table[i].cons_obj;
        if (CL_NULL_P(k) || CL_FIXNUM_P(k) || CL_CHAR_P(k)) continue;
        if (k >= cl_heap.bump || !CL_HDR_MARKED(CL_OBJ_TO_PTR(k)))
            cl_srcloc_table[i].cons_obj = CL_NIL;
    }
}

static CL_Obj gc_forward(CL_Obj obj);   /* defined with the compactor below */

static void gc_srcloc_forward(void)
{
    uint32_t i;
    for (i = 0; i < CL_SRCLOC_SIZE; i++) {
        CL_Obj k = cl_srcloc_table[i].cons_obj;
        if (CL_NULL_P(k) || CL_FIXNUM_P(k) || CL_CHAR_P(k)) continue;
        cl_srcloc_table[i].cons_obj = gc_forward(k);
    }
}

static void gc_finalize_dead(uint8_t *ptr)
{
    switch (CL_HDR_TYPE(ptr)) {
    case TYPE_BYTECODE: {
        /* Free the JIT artifacts owned by a dead bytecode: native_code and
         * the reloc table are platform_alloc'd (see cl_jit_compile) and were
         * otherwise leaked for good when the object is swept.  Safe here:
         * the world is stopped and a DEAD (unmarked) bytecode cannot be
         * executing — any running bytecode is reachable from a VM frame or
         * the conservative JIT stack scan and would have been marked.
         * Fields are NULLed so a re-finalize is a no-op. */
        CL_Bytecode *bc = (CL_Bytecode *)ptr;
        if (bc->native_code)   { platform_free(bc->native_code); }
        if (bc->native_relocs) { platform_free(bc->native_relocs); }
        bc->native_code = NULL;
        bc->native_relocs = NULL;
        bc->native_len = 0;
        bc->native_reloc_count = 0;
        break;
    }
    case TYPE_STREAM: {
        /* Output-stream outbuf slots are NOT freed here.  Freeing a dead
         * stream's st->out_buf_handle is unsafe: the slot's handle may already
         * have been recycled by a live string-output-stream (handles are
         * reused as soon as a slot is freed), so re-finalizing a stale corpse
         * would free the LIVE stream's buffer.  Outbufs are reclaimed instead
         * by the mark-driven cl_stream_outbuf_gc_reclaim() after marking.
         *
         * OS handles, however, ARE closed here (ST8): a dead file/socket
         * stream that was never CLOSEd would otherwise leak its fd forever —
         * long-running servers exhaust the descriptor table.  The close uses
         * GC-context variants (platform_file_close is already safe-region-
         * free on both platforms; platform_socket_close_gc skips the
         * write-buffer flush and its safe-region bracket — buffered output
         * on an UNREACHABLE stream is forfeit, and entering a safe region
         * while this thread orchestrates the collection is forbidden).  The
         * OPEN flag gates it: a properly closed stream cleared it (under the
         * iolock/CAS discipline in cl_stream_close), and clearing it here
         * makes a coalesce-path re-finalize a no-op.  Runs under STW — no
         * concurrent closer can race the flag. */
        CL_Stream *st = (CL_Stream *)ptr;
        if ((st->flags & CL_STREAM_FLAG_OPEN) && st->handle_id != 0 &&
            (st->stream_type == CL_STREAM_FILE ||
             st->stream_type == CL_STREAM_SOCKET)) {
            st->flags &= ~CL_STREAM_FLAG_OPEN;
            fprintf(stderr, "[GC] warning: closing a %s stream that was "
                    "dropped without CLOSE (handle %u)\n",
                    st->stream_type == CL_STREAM_FILE ? "file" : "socket",
                    (unsigned)st->handle_id);
            if (st->stream_type == CL_STREAM_FILE)
                platform_file_close((PlatformFile)st->handle_id);
            else
                platform_socket_close_gc((PlatformSocket)st->handle_id);
        }
        break;
    }
    case TYPE_LOCK: {
        /* If a program drops every reference to a lock wrapper while some
         * thread still HOLDS the platform mutex (acquired earlier, wrapper
         * discarded), destroying the mutex is undefined behavior on
         * pthreads.  cl_lock_held[]/cl_lock_depth[] track the holder and its
         * nested-acquire count (set/incremented by acquire, decremented by
         * release, cleared once depth reaches 0; read here during STW =
         * race-free): a held mutex is deliberately LEAKED — the table slot
         * and holder/depth entries are cleared so the id can be reused, but
         * the OS mutex survives for the (now unreachable) holder.  Blocked
         * WAITERS are safe regardless (their args root the wrapper).
         * Closes tier-3 I8. */
        CL_Lock *lk = (CL_Lock *)ptr;
        if (lk->lock_id < CL_MAX_LOCKS) {
            void *h = cl_lock_table[lk->lock_id];
            if (h) {
                cl_lock_table[lk->lock_id] = NULL;
                if (cl_lock_held[lk->lock_id]) {
                    static int lock_leak_warned = 0;
                    cl_lock_held[lk->lock_id] = NULL;
                    cl_lock_depth[lk->lock_id] = 0;
                    if (!lock_leak_warned) {
                        lock_leak_warned = 1;
                        fprintf(stderr, "[MP] warning: a lock was garbage-"
                                "collected while still held - leaking its OS "
                                "mutex (destroying a held mutex is undefined "
                                "behavior). Common cause: MP:DESTROY-THREAD "
                                "of a thread inside a critical section "
                                "(WITH-LOCK-HELD / MP:CONDITION-WAIT) - the "
                                "lock can never be released, so the leak is "
                                "deliberate and benign; further leaks are "
                                "silent\n");
                    }
                } else {
                    cl_lock_depth[lk->lock_id] = 0;
                    platform_mutex_destroy(h);
                }
            }
        }
        break;
    }
    case TYPE_CONDVAR: {
        CL_CondVar *cv = (CL_CondVar *)ptr;
        if (cv->condvar_id < CL_MAX_CONDVARS) {
            void *h = cl_condvar_table[cv->condvar_id];
            if (h) {
                cl_condvar_table[cv->condvar_id] = NULL;
                platform_condvar_destroy(h);
            }
        }
        break;
    }
    case TYPE_FOREIGN_POINTER: {
        /* Reclaim the side-table slot for transient (unowned) foreign
         * pointers — dlsym results, values returned from foreign calls, and
         * results of pointer arithmetic (CFFI inc-pointer / make-pointer).
         * Without this the POSIX side table leaks one slot per such pointer.
         *
         * OWNED allocations are deliberately left alone: per CFFI semantics
         * FFI:ALLOC-FOREIGN memory lives until an explicit FFI:FREE-FOREIGN,
         * and callers routinely keep only the integer address (via
         * pointer-address / make-pointer) while the wrapper object dies — so
         * freeing here would be a use-after-free.  Each register/alloc hands
         * out a unique handle, so this finalize is 1:1 with the slot. */
        CL_ForeignPtr *fp = (CL_ForeignPtr *)ptr;
        if (fp->address != 0 && !(fp->flags & CL_FPTR_FLAG_OWNED))
            platform_ffi_release(fp->address);
        break;
    }
    case TYPE_THREAD: {
        /* Wrapper for an MP thread becoming dead means the OS thread must
         * already be finished: while the worker is registered, gc_mark
         * marks t->thread_obj from gc_mark_thread_roots, so the wrapper
         * is reachable.  thread_entry publishes the terminal status (2
         * finished / 3 aborted) only AFTER it has called
         * cl_thread_unregister(t) — so a wrapper that reaches sweep is
         * guaranteed to have both `t` out of cl_thread_list and
         * status >= 2.
         *
         * Three guards:
         *  - status >= 2: defense-in-depth; never free a still-running
         *    worker that some other path failed to keep the wrapper
         *    reachable for.
         *  - t->thread_obj points back to THIS wrapper: required to
         *    avoid a slot-reuse double-free.  After bi_join_thread or
         *    the bi_make_thread zombie reaper releases the slot, the
         *    next make-thread can reuse it for an unrelated worker.
         *    The old wrapper still holds the now-stale thread_id;
         *    without this check we'd free the new worker on the old
         *    wrapper's finalize. */
        CL_ThreadObj *to = (CL_ThreadObj *)ptr;
        if (to->thread_id != 0 && to->thread_id < CL_MAX_THREADS) {
            CL_Thread *t = cl_thread_table[to->thread_id];
            /* table_gen compare: exact slot-reuse guard — the wrapper's
             * generation must match the slot's current occupancy.  This
             * replaces the old `t->thread_obj == CL_PTR_TO_OBJ(ptr)`
             * back-compare: t->thread_obj stops being forwarded once the
             * worker unregisters, so after any post-unregister compaction
             * it held a stale offset, the compare failed, and the worker
             * leaked until the table-full reaper ran.  The generation pair
             * is immune to relocation (plain integers) and is unique per
             * occupancy, so it is both safe against slot reuse AND exact.
             * Safe to read without the list lock: sweep runs during STW,
             * no peer can reclaim the slot concurrently. */
            if (t && t != cl_main_thread_ptr && t->status >= 2 &&
                cl_thread_table_gen[to->thread_id] == to->table_gen) {
                cl_thread_table[to->thread_id] = NULL;
                if (t->platform_handle) {
                    platform_thread_detach(t->platform_handle);
                    t->platform_handle = NULL;
                }
                cl_thread_free_worker(t);
            }
        }
        break;
    }
    default:
        break;
    }
}

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
            gc_finalize_dead(ptr);

            /* Coalesce adjacent free blocks up to max representable size */
            while (ptr + total < end) {
                CL_Header *next = (CL_Header *)(ptr + total);
                uint32_t next_size = next->header & CL_HDR_SIZE_MASK;
                if (next_size == 0) break;
                if (next->header & CL_HDR_MARK_BIT) break;  /* Next is live */
                if (total + next_size > CL_HDR_SIZE_MASK) break;  /* Would overflow 23-bit size */
                /* Finalize the coalesced dead object too */
                gc_finalize_dead((uint8_t *)next);
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
    uint32_t new_offset = CL_ALIGN;  /* free pointer; skip offset 0 (NIL) */
    int pin_i = 0;                   /* index into the sorted jit_pinned[] */

    /* jit_pinned[] must be ascending for the single-pass merge below.  A single
     * thread's scan already appends in ascending order (monotonic arena walk),
     * but with multi-thread STW each stopped thread's JIT stack is scanned in
     * turn, so pins from different threads concatenate out of order — sort. */
    if (jit_pinned_count > 1)
        qsort(jit_pinned, (size_t)jit_pinned_count, sizeof(jit_pinned[0]),
              cand_cmp);

    while (ptr < end) {
        uint32_t size = CL_HDR_SIZE(ptr);
        uint32_t old_offset;
        if (size == 0) break;

        if (CL_HDR_MARKED(ptr)) {
            old_offset = (uint32_t)(ptr - cl_heap.arena);

            /* Advance past pins below this object (jit_pinned[] is sorted
             * ascending, and we visit objects in ascending offset order). */
            while (pin_i < jit_pinned_count && jit_pinned[pin_i] < old_offset)
                pin_i++;

            if (pin_i < jit_pinned_count && jit_pinned[pin_i] == old_offset) {
                /* Pinned: keep in place.  The free pointer is <= old_offset
                 * here (all live data below this object compacts into
                 * [CL_ALIGN, old_offset)), so leaving [new_offset, old_offset)
                 * as a temporary hole and resuming the free pointer at the
                 * pin's end preserves the monotonic, no-overlap invariant. */
                gc_fwd_table[old_offset / CL_ALIGN] = old_offset;
                new_offset = old_offset + size;
                pin_i++;
            } else {
                gc_fwd_table[old_offset / CL_ALIGN] = new_offset;
                new_offset += size;
            }
        }
        ptr += size;
    }
}

/* (The former gc_forward_jit_native_stack writeback pass was removed when
 * the compactor switched to pinning conservatively-referenced objects —
 * option B in specs/native-backend.md §"GC interaction".  Pinned objects
 * never move, so the JIT's spilled operand offsets on the C stack stay
 * valid with no writeback, and a coincidental-integer C-stack word that
 * merely equals a live object's offset is no longer mis-rewritten.) */

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

/* Pass 3a: Update children of a single heap object.  Shares the per-type slot
 * layout with gc_mark_children via the GC_WALK_OBJ_CHILDREN X-macro above;
 * here each slot is forwarded in place and TYPE_BYTECODE additionally patches
 * CL_Obj immediates baked into JIT'd native code. */
static void gc_update_children(void *ptr, uint8_t type)
{
#define GC_VISIT(slot) gc_update_slot(&(slot))
    /* Forward CL_Obj heap references baked as 32-bit immediates into the JIT'd
     * native code.  native_relocs lists the byte offset of each such 4-byte
     * big-endian field; without this a moving compaction would leave stale
     * arena offsets in executable code (m68k JIT only — native_relocs is NULL
     * on host / non-JIT).  The code buffer is platform_alloc'd (does not move),
     * so the raw pointer stays valid whether we visit the object's old or new
     * copy; only the referenced objects shift.  Stop-the-world GC means no
     * thread is mid-fetch of these bytes. */
#define GC_BYTECODE_TAIL(bc)                                                  \
    do {                                                                      \
        if ((bc)->native_code && (bc)->native_relocs) {                       \
            uint32_t r_;                                                       \
            for (r_ = 0; r_ < (bc)->native_reloc_count; r_++) {               \
                uint8_t *p_ = (bc)->native_code + (bc)->native_relocs[r_];     \
                CL_Obj old = ((CL_Obj)p_[0] << 24) | ((CL_Obj)p_[1] << 16) |   \
                             ((CL_Obj)p_[2] << 8)  |  (CL_Obj)p_[3];           \
                CL_Obj nw = gc_forward(old);                                   \
                if (nw != old) {                                              \
                    p_[0] = (uint8_t)(nw >> 24); p_[1] = (uint8_t)(nw >> 16);  \
                    p_[2] = (uint8_t)(nw >> 8);  p_[3] = (uint8_t)(nw);        \
                    gc_native_code_patched = 1;                               \
                }                                                            \
            }                                                                \
        }                                                                    \
    } while (0)
#define GC_STREAM_TAIL(st) ((void)(st))
    GC_WALK_OBJ_CHILDREN(ptr, type);
#undef GC_VISIT
#undef GC_BYTECODE_TAIL
#undef GC_STREAM_TAIL
}

/* Pass 3b: Update per-thread roots (mirrors gc_mark_thread_roots).
 * Must #undef gc_root_count to avoid macro collision with t->gc_root_count. */
#undef gc_root_count
static void gc_update_thread_roots(CL_Thread *t)
{
    int i;

    /* The gc_roots[] stack (CL_Obj* pointers to C locals) is NOT forwarded
     * here — gc_update_registered_roots forwards all threads' entries from
     * one deduplicated address list, so a slot registered twice (or one
     * aliasing any of the independently-forwarded regions below) is
     * forwarded exactly once. */

    /* Dynamic binding stack */
    for (i = 0; i < t->dyn_top; i++) {
        gc_update_slot(&t->dyn_stack[i].symbol);
        gc_update_slot(&t->dyn_stack[i].old_value);
    }

    /* NLX stack (mv_values deliberately excluded — see the mark phase) */
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
        gc_update_slot(&t->restart_stack[i].restart);
    }

    /* VM execution stack */
    if (t->vm.stack) {
        for (i = 0; i < t->vm.sp; i++)
            gc_update_slot(&t->vm.stack[i]);
    }

    /* VM frame bytecodes */
    for (i = 0; i < t->vm.fp; i++)
        gc_update_slot(&t->vm.frames[i].bytecode);

    /* Multiple values and pending throw state.
     * (pre_call_mv_values deliberately excluded — see the mark phase.) */
    for (i = 0; i < CL_MAX_MV; i++)
        gc_update_slot(&t->mv_values[i]);
    gc_update_slot(&t->pending_tag);
    gc_update_slot(&t->pending_value);
    /* In-flight THROW secondary values + saved pending-throw snapshots —
     * mirror of gc_mark_thread_roots (a snapshot restored after an
     * allocating unwind-protect cleanup must hold FORWARDED offsets, or
     * the restored pending_tag no longer EQ-matches its catch tag:
     * "No catch for tag" / garbage multiple values). */
    if (t->pending_throw) {
        for (i = 0; i < t->pending_mv_count && i < CL_MAX_MV; i++)
            gc_update_slot(&t->pending_mv_values[i]);
    }
    for (i = 0; i < t->saved_pending_top; i++) {
        int m;
        if (!t->saved_pending_stack[i].pending_throw)
            continue;  /* see gc_mark_thread_roots */
        gc_update_slot(&t->saved_pending_stack[i].pending_tag);
        gc_update_slot(&t->saved_pending_stack[i].pending_value);
        for (m = 0; m < t->saved_pending_stack[i].pending_mv_count &&
                    m < CL_MAX_MV; m++)
            gc_update_slot(&t->saved_pending_stack[i].pending_mv_values[m]);
    }
    gc_update_slot(&t->pending_lambda_name);  /* see gc_mark counterpart */

    /* Thread metadata */
    gc_update_slot(&t->name);
    gc_update_slot(&t->result);
    gc_update_slot(&t->interrupt_func);
    gc_update_slot(&t->current_lex_env);
    gc_update_slot(&t->thread_obj);

    /* Reader state — must mirror gc_mark_thread_roots.  These were marked
     * but not updated, leaving stale CL_Obj values pointing to old arena
     * offsets after compaction; the next gc_mark would then dereference
     * the stale offset, read garbage as a heap header, and run off the
     * end of the arena while iterating "n_slots"/"n_constants" — manifesting
     * as a SIGSEGV in gc_mark on sento workloads that compact under load. */
    gc_update_slot(&t->rd_stream);
    gc_update_slot(&t->rd_uninterned);
    gc_update_slot(&t->rd_labels);

    /* Printer in-progress object stack — heap pointers can be relocated
     * by compaction during a Lisp print-object hook (which can allocate). */
    for (i = 0; i < t->pr_inprog_top; i++)
        gc_update_slot(&t->pr_inprog[i]);

    /* Current printer output target — forward the relocated stream offset so
     * a parked printer thread resumes writing to the moved object, not the
     * stale one (mirror of the gc_mark_thread_roots counterpart). */
    gc_update_slot(&t->pr_stream);

    /* *print-circle* detection table keys (only meaningful while active). */
    if (t->pr_circle_active) {
        int ci;
        for (ci = 0; ci < CL_CIRCLE_HT_SIZE; ci++) {
            if (!CL_NULL_P(t->pr_circle_keys[ci]))
                gc_update_slot(&t->pr_circle_keys[ci]);
        }
    }

    /* Pending LOAD-TIME-VALUE (cell, thunk) pairs (compile-file only) */
    for (i = 0; i < t->ltv_init_count; i++) {
        gc_update_slot(&t->ltv_init_cells[i]);
        gc_update_slot(&t->ltv_init_thunks[i]);
    }

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

    /* JIT native stack needs no forward-update pass: the mark-phase scan
     * (gc_scan_jit_native_stack) pins every conservatively-referenced
     * object, so they stay at their current offset and the JIT's spilled
     * C-stack offsets remain valid without rewriting the stack. */
}

/* --- Registered-root forwarding with dedup (double-forward hardening) ---
 *
 * gc_forward is NOT idempotent: the forwarding table is keyed by
 * pre-compaction offsets, so forwarding the same slot twice maps the
 * already-forwarded offset through whatever object's OLD offset it now
 * coincides with — silently rewriting the slot to an unrelated object.
 * Historically each root registry was forwarded by its own loop, so any
 * slot reachable from two entries (an accidental double CL_GC_PROTECT,
 * a global both cl_gc_register_root'ed and protected, or a protect of an
 * already-rooted VM-stack slot like CL_GC_PROTECT(args[i])) corrupted
 * with layout-roulette timing.  This pass forwards all dynamically-
 * registered slots (global_roots[] + every thread's gc_roots[]) from one
 * sorted, deduplicated address list, and skips addresses that alias any
 * independently-forwarded thread region (VM stack, mv_values, pending
 * throw values, dyn/handler/restart stacks, vm_extra_args, compiler
 * chain — see root_slot_independently_forwarded) — making duplicate
 * registration harmless by construction.  cl_gc_audit_roots remains the tool for locating the
 * offending registration site.  (gc_root_slot_buf itself is declared with
 * the other persistent GC buffers near the top of the file, so
 * cl_mem_shutdown can free it.) */
static int root_slot_cmp(const void *a, const void *b)
{
    CL_Obj * const *pa = (CL_Obj * const *)a;
    CL_Obj * const *pb = (CL_Obj * const *)b;
    if (*pa < *pb) return -1;
    if (*pa > *pb) return 1;
    return 0;
}

/* Probe callback for testing whether a slot address is owned by an
 * external per-thread forwarding walker (compiler chain).  Reusing the
 * walker itself as the membership test guarantees this can never drift
 * from what gc_update_thread_roots actually forwards. */
static CL_Obj *gc_probe_slot;
static int     gc_probe_hit;
static void gc_probe_check(CL_Obj *slot)
{
    if (slot == gc_probe_slot) gc_probe_hit = 1;
}

/* Does slot alias a per-thread region that gc_update_thread_roots
 * forwards independently?  Those slots must be SKIPPED by the
 * registered-root pass, or they are forwarded twice (gc_forward is not
 * idempotent — double-forwarding rewrites the slot to an unrelated
 * object).  Historically only the VM value stack was exempted; a root
 * registered against mv_values / pending_mv_values / the dyn/handler/
 * restart stacks / vm_extra_args / a compiler-chain slot still
 * double-forwarded with layout-roulette timing.  Every range below is
 * bounded exactly like its forwarding loop in gc_update_thread_roots —
 * an out-of-bounds alias (e.g. beyond dyn_top) is NOT independently
 * forwarded and must stay in the registered-root pass. */
static int root_slot_independently_forwarded(CL_Obj *slot)
{
    CL_Thread *t;
    for (t = cl_thread_list; t; t = t->next) {
        /* VM value stack [stack, stack+sp) */
        if (t->vm.stack && slot >= t->vm.stack &&
            slot < t->vm.stack + t->vm.sp)
            return 1;
        /* Multiple-values buffer (forwarded unconditionally, all slots) */
        if (slot >= t->mv_values && slot < t->mv_values + CL_MAX_MV)
            return 1;
        /* In-flight THROW secondary values — forwarded only while a
         * throw is pending, and only up to pending_mv_count. */
        if (t->pending_throw &&
            slot >= t->pending_mv_values &&
            slot < t->pending_mv_values +
                   (t->pending_mv_count < CL_MAX_MV ? t->pending_mv_count
                                                    : CL_MAX_MV))
            return 1;
        /* Dynamic-binding / handler / restart stacks: every CL_Obj field
         * of an entry below the top is forwarded by its walker loop. */
        if ((char *)slot >= (char *)t->dyn_stack &&
            (char *)slot <  (char *)(t->dyn_stack + t->dyn_top))
            return 1;
        if ((char *)slot >= (char *)t->handler_stack &&
            (char *)slot <  (char *)(t->handler_stack + t->handler_top))
            return 1;
        if ((char *)slot >= (char *)t->restart_stack &&
            (char *)slot <  (char *)(t->restart_stack + t->restart_top))
            return 1;
        /* VM extra-args buffer (&rest processing) */
        if (slot >= t->vm_extra_args_buf &&
            slot < t->vm_extra_args_buf + t->vm_extra_count)
            return 1;
        /* Saved pending-throw snapshots: pending_tag/pending_value/
         * pending_mv_values[] of each armed (pending_throw) entry below
         * saved_pending_top are forwarded unconditionally by
         * gc_update_thread_roots — mirror those exact bounds here. */
        if (t->saved_pending_stack) {
            int spi;
            for (spi = 0; spi < t->saved_pending_top; spi++) {
                CL_SavedPending *sp = &t->saved_pending_stack[spi];
                if (!sp->pending_throw)
                    continue;
                if (slot == &sp->pending_tag || slot == &sp->pending_value)
                    return 1;
                if (slot >= sp->pending_mv_values &&
                    slot < sp->pending_mv_values +
                           (sp->pending_mv_count < CL_MAX_MV
                                ? sp->pending_mv_count : CL_MAX_MV))
                    return 1;
            }
        }
        /* Compiler-chain constants/blocks/tagbodies/env — walked by
         * cl_compiler_gc_update_thread; probe with the walker itself. */
        if (t->active_compiler) {
            extern void cl_compiler_gc_update_thread(CL_Thread *th,
                                                     void (*update_fn)(CL_Obj *));
            gc_probe_slot = slot;
            gc_probe_hit = 0;
            cl_compiler_gc_update_thread(t, gc_probe_check);
            if (gc_probe_hit) return 1;
        }
    }
    return 0;
}

static void gc_update_registered_roots(void)
{
    uint32_t total = (uint32_t)n_global_roots;
    uint32_t n = 0, i;
    CL_Thread *t;
    int j;
#ifdef DEBUG_GC
    uint32_t dups = 0, aliased = 0;
#endif

    for (t = cl_thread_list; t; t = t->next)
        total += (uint32_t)t->gc_root_count;
    if (total == 0)
        return;

    if (total > gc_root_slot_cap) {
        CL_Obj **buf = (CL_Obj **)platform_alloc(total * sizeof(CL_Obj *));
        if (buf) {
            if (gc_root_slot_buf) platform_free(gc_root_slot_buf);
            gc_root_slot_buf = buf;
            gc_root_slot_cap = total;
        }
    }
    if (total > gc_root_slot_cap) {
        /* Buffer allocation failed — fall back to per-registry loops.
         * Exact-duplicate addresses would still double-forward here, but
         * skipping the update entirely would corrupt EVERY slot; the
         * VM-stack alias check needs no memory, so it still applies. */
        for (j = 0; j < n_global_roots; j++)
            gc_update_slot(global_roots[j]);
        for (t = cl_thread_list; t; t = t->next)
            for (j = 0; j < t->gc_root_count; j++)
                if (!root_slot_independently_forwarded(t->gc_roots[j]))
                    gc_update_slot(t->gc_roots[j]);
        return;
    }

    for (j = 0; j < n_global_roots; j++)
        gc_root_slot_buf[n++] = global_roots[j];
    for (t = cl_thread_list; t; t = t->next)
        for (j = 0; j < t->gc_root_count; j++)
            gc_root_slot_buf[n++] = t->gc_roots[j];

    qsort(gc_root_slot_buf, (size_t)n, sizeof(CL_Obj *), root_slot_cmp);

    for (i = 0; i < n; i++) {
        if (i > 0 && gc_root_slot_buf[i] == gc_root_slot_buf[i - 1]) {
#ifdef DEBUG_GC
            dups++;
#endif
            continue;               /* duplicate registration — forward once */
        }
        if (root_slot_independently_forwarded(gc_root_slot_buf[i])) {
#ifdef DEBUG_GC
            aliased++;
#endif
            continue;               /* owned by a thread-region forwarding loop */
        }
        gc_update_slot(gc_root_slot_buf[i]);
    }

#ifdef DEBUG_GC
    if (dups || aliased)
        fprintf(stderr, "GC: root dedup skipped %u duplicate and %u "
                "thread-region-aliased registration(s) this compaction "
                "(harmless, but run ext:%%gc-audit-roots to find the "
                "redundant CL_GC_PROTECT / cl_gc_register_root)\n",
                (unsigned)dups, (unsigned)aliased);
#endif
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
    gc_update_slot(&compiler_macro_table);
    gc_update_slot(&cl_clos_class_table);
    gc_update_slot(&struct_table);
    gc_update_slot(&condition_hierarchy);
    gc_update_slot(&condition_slot_table);
    gc_update_slot(&condition_default_initargs);
    gc_update_slot(&condition_slot_initforms);

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

    /* Registered global roots (cached keyword/type symbols, etc.) are
     * forwarded by gc_update_registered_roots — one deduplicated pass
     * shared with the thread gc_roots[] stacks. */

    /* Active FASL readers — forward the relocated offsets in their dedup
     * tables (mirrors cl_fasl_gc_mark_readers in the mark phase). */
    {
        extern void cl_fasl_gc_update_readers(void (*update_fn)(CL_Obj *));
        cl_fasl_gc_update_readers(gc_update_slot);
    }

    /* Active FASL writers — forward the gensym dedup entries (mirrors
     * cl_fasl_gc_mark_writers). */
    {
        extern void cl_fasl_gc_update_writers(void (*update_fn)(CL_Obj *));
        cl_fasl_gc_update_writers(gc_update_slot);
    }

    /* MAKE-LOAD-FORM writer pre-pass — forward the relocated offsets in
     * its walk/result arrays (mirrors cl_fasl_gc_mark_mlf). */
    {
        extern void cl_fasl_gc_update_mlf(void (*update_fn)(CL_Obj *));
        cl_fasl_gc_update_mlf(gc_update_slot);
    }

    /* Struct registry hash index — its keys (name symbols) and cached
     * entry values just moved; mark it stale so it rebuilds lazily
     * before the next lookup (builtins_struct.c).  Ditto the condition
     * hierarchy and deftype table indexes (CL_AlistIndex, compiler.h). */
    {
        extern void cl_struct_index_gc_invalidate(void);
        extern void cl_condition_index_gc_invalidate(void);
        extern void cl_type_index_gc_invalidate(void);
        cl_struct_index_gc_invalidate();
        cl_condition_index_gc_invalidate();
        cl_type_index_gc_invalidate();
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

    /* Registered root slots (global_roots[] + all thread gc_roots[]),
     * deduplicated so a doubly-registered slot is forwarded exactly once.
     * Disjoint from the passes above/below: entries aliasing an
     * independently-forwarded thread region are skipped (that region's
     * own forwarding loop owns them). */
    gc_update_registered_roots();

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

/* Chunk size for splitting a free gap of `total` bytes under alignment
 * `align`: at most CL_HDR_SIZE_MASK, align-multiple, and never leaving a
 * remainder too small to hold a CL_FreeBlock header.  With align=4
 * (Amiga) the naive cap CL_HDR_SIZE_MASK & ~3 can leave a 4-byte
 * remainder; the next iteration's CL_FreeBlock write (8 bytes) would
 * then overrun the gap and smash the following — pinned — object's
 * header.  Non-static so the unit test can exercise the align=4
 * arithmetic on the host (whose CL_ALIGN is 8). */
uint32_t gc_free_gap_chunk(uint32_t total, uint32_t align)
{
    uint32_t chunk = total;
    if (chunk > CL_HDR_SIZE_MASK) {
        chunk = CL_HDR_SIZE_MASK & ~(align - 1u);
        if (total - chunk < (uint32_t)sizeof(CL_FreeBlock))
            chunk -= CL_MIN_ALLOC_SIZE;
    }
    return chunk;
}

/* Write valid free-block header(s) covering [offset, offset+total) and
 * thread them onto the free list, so the arena's linear "walk by header
 * size" stays consistent across the gap.  Mirrors gc_sweep's free-block
 * format (size in the header low bits, type/mark = 0 → an unmarked block
 * the walk skips).  Splits gaps larger than the 23-bit size field into
 * multiple blocks (see gc_free_gap_chunk for the remainder invariant).
 * `total` is always a CL_ALIGN multiple and, when non-zero,
 * >= CL_MIN_ALLOC_SIZE (>= sizeof(CL_FreeBlock)), so every piece can
 * hold the 8-byte header. */
static void gc_make_free_gap(uint32_t offset, uint32_t total)
{
    while (total > 0) {
        uint32_t chunk = gc_free_gap_chunk(total, CL_ALIGN);
        if (chunk < (uint32_t)sizeof(CL_FreeBlock)) {
            /* Degenerate sub-header gap (caller invariant broken — total
             * itself smaller than a CL_FreeBlock).  Write a bare size-only
             * header so the linear arena walk stays in sync, but leave the
             * block off the free list: there is no room for the
             * next_offset link, and writing one would smash the following
             * object.  The space is reclaimed by the next compaction. */
            ((CL_Header *)(cl_heap.arena + offset))->header = chunk;
            return;
        }
        {
            CL_FreeBlock *fb = (CL_FreeBlock *)(cl_heap.arena + offset);
            fb->size = chunk;                       /* type=0, mark=0, size=chunk */
            fb->next_offset = cl_heap.free_list;
            cl_heap.free_list = offset;
#ifdef DEBUG_GC
            if (chunk > sizeof(CL_FreeBlock))
                memset((uint8_t *)fb + sizeof(CL_FreeBlock), 0xDE,
                       chunk - sizeof(CL_FreeBlock));
#endif
        }
        offset += chunk;
        total  -= chunk;
    }
}

/* Pass 4: Slide live objects to their forwarding addresses.
 * Objects only move downward (or stay), so forward copy is safe.
 *
 * Pinned objects (option B) keep their offset, which can leave a gap
 * between the compacted run below them and the pin.  Those gaps are
 * filled with free-block headers (gc_make_free_gap) and threaded onto
 * the free list — both so the linear arena walk stays valid and so the
 * allocator can reclaim the space.  With no pins this degenerates to a
 * gap-free compaction (free_list ends empty). */
static void gc_slide(void)
{
    uint8_t *ptr = cl_heap.arena + CL_ALIGN;
    uint8_t *end = cl_heap.arena + cl_heap.bump;
    uint32_t fill = CL_ALIGN;          /* next contiguous position in new layout */
    uint32_t live_total = 0;

    cl_heap.free_list = 0;

    while (ptr < end) {
        uint32_t size = CL_HDR_SIZE(ptr);
        if (size == 0) break;

        if (CL_HDR_MARKED(ptr)) {
            uint32_t old_offset = (uint32_t)(ptr - cl_heap.arena);
            uint32_t new_offset = gc_fwd_table[old_offset / CL_ALIGN];

            /* Clear mark bit before copying */
            CL_HDR_CLR_MARK(ptr);

            /* Gap before this object's new position (only at pins) → free. */
            if (new_offset > fill)
                gc_make_free_gap(fill, new_offset - fill);

            if (new_offset != old_offset)
                memmove(cl_heap.arena + new_offset, ptr, size);
            fill = new_offset + size;
            live_total += size;
        } else {
            /* Dead object — release external resources before overwriting */
            gc_finalize_dead(ptr);
        }
        ptr += size;
    }

    cl_heap.bump = fill;
    /* total_allocated counts live data; gap free-blocks below bump are on
     * the free list and excluded (matches gc_sweep's accounting). */
    cl_heap.total_allocated = live_total;
}

/* Is a key's hash address-sensitive — i.e. does it change when the compacting
 * GC relocates objects? Mirrors hash_obj() in builtins_hashtable.c. If a table
 * has any such key it MUST be rehashed after a compaction, or stale buckets
 * make previously-inserted keys un-findable (gethash returns NIL). */
static int gc_key_addr_sensitive(CL_Obj key, uint32_t test)
{
    uint8_t type;
    if (!CL_HEAP_P(key))
        return 0;                 /* fixnum / char / immediate: stable */
    if (test == CL_HT_TEST_EQ)
        return 1;                 /* every heap object hashed by identity */
    type = CL_HDR_TYPE(CL_OBJ_TO_PTR(key));
    switch (type) {
    case TYPE_BIGNUM:
    case TYPE_RATIO:
    case TYPE_SINGLE_FLOAT:
    case TYPE_DOUBLE_FLOAT:
    case TYPE_COMPLEX:
        return 0;                 /* value-based hash under eql/equal/equalp */
    case TYPE_STRING:
#ifdef CL_WIDE_STRINGS
    case TYPE_WIDE_STRING:
#endif
    case TYPE_BIT_VECTOR:
        /* eql hashes these by identity; equal/equalp by contents (wide
         * strings and bit-vectors gained content hashes in tier-4 AH5). */
        return (test == CL_HT_TEST_EQL) ? 1 : 0;
    case TYPE_CONS:
        if (test == CL_HT_TEST_EQL)
            return 1;             /* cons hashed by identity under eql */
        /* equal/equalp: hash_obj() folds in only the car */
        return gc_key_addr_sensitive(((CL_Cons *)CL_OBJ_TO_PTR(key))->car, test);
    default:
        return 1;                 /* symbol, struct, instance, array, … */
    }
}

#if defined(DEBUG_GC) || defined(DEBUG_GC_STRESS)
/* Chain-integrity check: every bucket chain must terminate within ht->count
 * steps.  A longer walk means a chain CYCLE (double-forwarded bucket slot or
 * a put through a stale table pointer) — without this check the next
 * gc_rehash_table spins forever, which is undebuggable in the field. */
static void gc_verify_ht_chains(CL_Hashtable *ht, const char *when)
{
    CL_Obj *bkts;
    uint32_t i, walked = 0;
    if (!CL_NULL_P(ht->bucket_vec))
        bkts = ((CL_Vector *)CL_OBJ_TO_PTR(ht->bucket_vec))->data;
    else
        bkts = ht->buckets;
    for (i = 0; i < ht->bucket_count; i++) {
        CL_Obj chain = bkts[i];
        while (!CL_NULL_P(chain)) {
            if (++walked > ht->count + 8) {
                CL_Cons *e0 = (CL_Cons *)CL_OBJ_TO_PTR(bkts[i]);
                fprintf(stderr,
                        "[GC-HT-BUG] %s: hashtable @0x%08x (test=%u count=%u "
                        "buckets=%u flags=0x%x) chain walk exceeded count at "
                        "bucket %u — cycle/corruption\n"
                        "  chain=0x%08x entry->car(pair)=0x%08x "
                        "entry->cdr=0x%08x\n",
                        when,
                        (unsigned)((uint8_t *)ht - cl_heap.arena),
                        (unsigned)ht->test, (unsigned)ht->count,
                        (unsigned)ht->bucket_count, (unsigned)ht->flags,
                        (unsigned)i,
                        (unsigned)bkts[i], (unsigned)e0->car,
                        (unsigned)e0->cdr);
                if (CL_CONS_P(e0->car)) {
                    CL_Cons *p0 = (CL_Cons *)CL_OBJ_TO_PTR(e0->car);
                    fprintf(stderr, "  pair key=0x%08x val=0x%08x\n",
                            (unsigned)p0->car, (unsigned)p0->cdr);
                }
                /* Re-walk the arena and print the last objects leading up to
                 * this table — a desynced walk (bad header size upstream)
                 * shows up as implausible type/size runs here. */
                {
                    uint8_t *wp = cl_heap.arena + CL_ALIGN;
                    uint8_t *wend = cl_heap.arena + cl_heap.bump;
                    uint32_t woff[8]; uint32_t wtype[8]; uint32_t wsize[8];
                    int wn = 0;
                    while (wp < wend && wp <= (uint8_t *)ht) {
                        uint32_t wsz = CL_HDR_SIZE(wp);
                        if (wsz == 0) break;
                        woff[wn & 7] = (uint32_t)(wp - cl_heap.arena);
                        wtype[wn & 7] = CL_HDR_TYPE(wp);
                        wsize[wn & 7] = wsz;
                        wn++;
                        wp += wsz;
                    }
                    {
                        int k, first = wn > 8 ? wn - 8 : 0;
                        for (k = first; k < wn; k++)
                            fprintf(stderr,
                                    "  walk[%d] @0x%08x type=%u size=%u\n",
                                    k, (unsigned)woff[k & 7],
                                    (unsigned)wtype[k & 7],
                                    (unsigned)wsize[k & 7]);
                        fprintf(stderr, "  walk ended at 0x%08x (ht at 0x%08x, bump 0x%08x)\n",
                                (unsigned)(wp - cl_heap.arena),
                                (unsigned)((uint8_t *)ht - cl_heap.arena),
                                (unsigned)cl_heap.bump);
                    }
                }
                /* Who references this table?  A symbol-value referrer names
                 * the variable holding it. */
                {
                    CL_Obj ht_obj = CL_PTR_TO_OBJ((uint8_t *)ht);
                    uint8_t *wp = cl_heap.arena + CL_ALIGN;
                    uint8_t *wend = cl_heap.arena + cl_heap.bump;
                    int found = 0;
                    while (wp < wend && found < 8) {
                        uint32_t wsz = CL_HDR_SIZE(wp);
                        uint32_t nw, w;
                        if (wsz == 0) break;
                        nw = wsz / 4;
                        for (w = 1; w < nw; w++) {
                            if (((uint32_t *)wp)[w] == ht_obj) {
                                fprintf(stderr,
                                        "  referrer @0x%08x type=%u word=%u",
                                        (unsigned)(wp - cl_heap.arena),
                                        (unsigned)CL_HDR_TYPE(wp), (unsigned)w);
                                if (CL_HDR_TYPE(wp) == TYPE_SYMBOL) {
                                    CL_Symbol *rs = (CL_Symbol *)wp;
                                    if (CL_HEAP_P(rs->name)) {
                                        CL_String *rn = (CL_String *)CL_OBJ_TO_PTR(rs->name);
                                        fprintf(stderr, " symbol=%.*s",
                                                (int)rn->length, rn->data);
                                    }
                                }
                                fprintf(stderr, "\n");
                                found++;
                            }
                        }
                        wp += wsz;
                    }
                    if (!found)
                        fprintf(stderr, "  no arena referrer (C-global/root/TLV?)\n");
                }
                cl_capture_backtrace();
                fprintf(stderr, "%s", cl_backtrace_buf);
                fflush(stderr);
                abort();
            }
            if (!CL_CONS_P(chain)) {
                fprintf(stderr,
                        "[GC-HT-BUG] %s: hashtable %p bucket %u chain entry "
                        "is not a cons (0x%08x)\n",
                        when, (void *)ht, (unsigned)i, (unsigned)chain);
                fflush(stderr);
                abort();
            }
            chain = ((CL_Cons *)CL_OBJ_TO_PTR(chain))->cdr;
        }
    }
}
/* Sweep the arena and verify every populated table (see gc_verify_ht_chains). */
static void gc_verify_all_ht_chains(const char *when)
{
    uint8_t *ptr = cl_heap.arena + CL_ALIGN;
    uint8_t *end = cl_heap.arena + cl_heap.bump;
    while (ptr < end) {
        uint32_t size = CL_HDR_SIZE(ptr);
        if (size == 0) break;
        if (CL_HDR_TYPE(ptr) == TYPE_HASHTABLE) {
            CL_Hashtable *ht = (CL_Hashtable *)ptr;
            if (ht->count > 0)
                gc_verify_ht_chains(ht, when);
        }
        ptr += size;
    }
}
#endif

/* Rehash a single hash table after compaction (no allocation). Recomputes each
 * key's bucket with the table's own test, since relocated objects now hash to
 * different values. Skips tables whose every key is hash-stable. */
static void gc_rehash_table(CL_Hashtable *ht)
{
    CL_Obj *bkts;
    uint32_t bucket_count = ht->bucket_count;
    uint32_t test = ht->test;
    uint32_t i;
    int needs = 0;
    CL_Obj all_entries = CL_NIL;

    /* Get bucket array */
    if (!CL_NULL_P(ht->bucket_vec)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(ht->bucket_vec);
        bkts = v->data;
    } else {
        bkts = ht->buckets;
    }

#if defined(DEBUG_GC) || defined(DEBUG_GC_STRESS)
    gc_verify_ht_chains(ht, "pre-rehash");
#endif

    /* Do any keys have address-sensitive hashes? If not, the buckets are still
     * valid after the move and we can leave the table untouched. */
    for (i = 0; i < bucket_count && !needs; i++) {
        CL_Obj chain = bkts[i];
        while (!CL_NULL_P(chain)) {
            CL_Cons *entry = (CL_Cons *)CL_OBJ_TO_PTR(chain);
            CL_Obj pair = entry->car;
            CL_Obj key = ((CL_Cons *)CL_OBJ_TO_PTR(pair))->car;
            if (gc_key_addr_sensitive(key, test)) { needs = 1; break; }
            chain = entry->cdr;
        }
    }
    if (!needs)
        return;

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

    /* Redistribute using freshly computed hashes (post-move offsets) */
    while (!CL_NULL_P(all_entries)) {
        CL_Cons *entry = (CL_Cons *)CL_OBJ_TO_PTR(all_entries);
        CL_Obj next = entry->cdr;
        CL_Obj pair = entry->car;
        CL_Obj key = ((CL_Cons *)CL_OBJ_TO_PTR(pair))->car;
        uint32_t h = cl_hashtable_hash_key(key, test);
        uint32_t idx = h & (bucket_count - 1);
        entry->cdr = bkts[idx];
        bkts[idx] = all_entries;
        all_entries = next;
    }
}

/* Rehash ALL hash tables in the arena after compaction. Covers eq/eql/equal/
 * equalp: any of them can hash a key by object identity (objects fall through
 * to an identity hash), and identity changes when the GC moves the object. */
static void gc_rehash_tables(void)
{
    uint8_t *ptr = cl_heap.arena + CL_ALIGN;
    uint8_t *end = cl_heap.arena + cl_heap.bump;

    while (ptr < end) {
        uint32_t size = CL_HDR_SIZE(ptr);
        if (size == 0) break;
        if (CL_HDR_TYPE(ptr) == TYPE_HASHTABLE) {
            CL_Hashtable *ht = (CL_Hashtable *)ptr;
            if (ht->count > 0)
                gc_rehash_table(ht);
        }
        ptr += size;
    }
}

/* Rebuild every thread's TLV (dynamic-binding) table after a compaction.
 * Like the user hash tables, the TLV table is keyed by symbol arena-offset and
 * must be re-hashed once symbols have moved, or active dynamic bindings (e.g.
 * a `let` of a special var) silently revert to their global value. */
static void gc_rehash_tlv_tables(void)
{
    CL_Thread *t;
    /* During STW GC all threads are stopped; cl_thread_list is stable. */
    for (t = cl_thread_list; t; t = t->next)
        cl_tlv_rehash(t);
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

    /* Same invariant as cl_gc: no thread may keep cutting from a chunk the
     * passes below reclaim or slide.  Remainders are formatted holes, so
     * forwarding/slide parse them as dead regions. */
    gc_tlab_reset_all();

#ifdef DEBUG_GC
    platform_write_string("GC: compaction starting...\n");
#endif

    gc_native_code_patched = 0;

    /* Pass 1: Mark (standard) */
    cl_stream_outbuf_gc_mark_begin();
    gc_mark();
    /* Reclaim outbuf slots of streams that didn't survive marking, BEFORE the
     * slide overwrites their (now-dead) stream objects. */
    cl_stream_outbuf_gc_reclaim();
    gc_srcloc_invalidate_dead();

#ifdef DEBUG_GC
    /* Verify parent→child mark invariant immediately after marking so that
     * 'marked @X.field -> unmarked @Y' diagnostics fire at the exact
     * compaction that would corrupt the heap (not on the next sweep-only GC). */
    gc_verify_marked();
    if (gc_verify_errors > 0)
        gc_dump_roots_dbg();
#endif

    /* JIT pin-table OOM during marking: the pinned set is incomplete, so
     * moving anything would strand a live JIT frame's spilled offsets.
     * Degrade to a non-moving mark+sweep for this cycle (the mark phase
     * above is complete — only relocation is unsafe). */
    if (gc_jit_pin_oom) {
#ifdef DEBUG_GC
        platform_write_string("GC: compact suppressed (JIT pin-table OOM), "
                              "falling back to sweep\n");
#endif
        gc_sweep();
        cl_heap.gc_count++;
        /* Don't let the sweep-forever escape immediately re-attempt a
         * doomed compaction on every following allocation. */
        gc_sweeps_since_compact = 0;
        if (multithread) cl_gc_resume_the_world();
        return;
    }

    /* Allocate forwarding table */
    if (!gc_fwd_alloc()) {
#ifdef DEBUG_GC
        platform_write_string("GC: compact failed (no memory for fwd table), "
                              "falling back to sweep\n");
#endif
        gc_sweep();
        cl_heap.gc_count++;
        /* Reset the sweep-forever escape counter and remember the bump
         * level that failed: without this, gc_sweeps_since_compact stays
         * >= GC_SWEEPS_BEFORE_COMPACT, so EVERY following bump-exhausted
         * allocation re-runs a doomed full mark (+ failed table alloc +
         * sweep) — a permanent 100%-CPU GC-thrash regime.  cl_alloc's
         * trigger 2 skips re-attempts until the bump front is below the
         * recorded failure level (the table size is proportional to it);
         * trigger 1 (compaction as last resort) still always tries. */
        gc_sweeps_since_compact = 0;
        gc_fwd_fail_bump = cl_heap.bump;
        if (multithread) cl_gc_resume_the_world();
        return;
    }

    /* Pass 2: Compute forwarding addresses */
    gc_compute_forwarding();

    /* Pass 3: Update all references */
    gc_update_all_references();
    gc_srcloc_forward();

    /* Pass 4: Slide objects */
    gc_slide();


    /* Clean up forwarding table */
    gc_fwd_free();

    /* If we rewrote any CL_Obj immediate inside JIT'd native code, flush
     * the CPU caches so the patched instruction bytes are re-fetched.
     * REQUIRED on every 68020+ — the 020/030 also have an I-cache (256 B)
     * that can serve stale pre-patch lines; only the 68000/010 lack one.
     * Do NOT "optimize" this to 040/060-only.  No-op on host. */
    if (gc_native_code_patched)
        platform_cache_clear(NULL, 0);

    /* Rehash hash tables whose keys hash by object identity (offsets changed) */
    gc_rehash_tables();
#if defined(DEBUG_GC) || defined(DEBUG_GC_STRESS)
    /* Every object in the arena is a live survivor here — a dirty chain now
     * means THIS compact (or the mutator window before it) corrupted it. */
    gc_verify_all_ht_chains("post-rehash");
#endif

    /* Rehash per-thread TLV tables (keyed by symbol offset, which changed) */
    gc_rehash_tlv_tables();

    cl_heap.gc_count++;
    cl_heap.compact_count++;

    /* The bump pointer was just reset to the end of the surviving objects, so
     * the sweep-forever escape counter starts fresh — see gc_sweeps_since_compact. */
    gc_sweeps_since_compact = 0;
    gc_fwd_fail_bump = 0;   /* table allocation succeeded at this size */

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
                uint32_t nelt = (v->flags & CL_VEC_FLAG_DISPLACED) ? CL_DISP_BASE_IDX(v) + 2 :
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

/* Dump the per-thread GC root stack with file:line tags (DEBUG_GC only).
 * Call this after gc_verify_marked() finds errors to name the C frames that
 * hold the suspicious roots — the same technique used to finger cl_cons's
 * arg at mem.c:378 in the PROGN-cursor bug.
 *
 * Must #undef gc_root_count because thread.h defines it as (CT->gc_root_count),
 * which collides with t->gc_root_count member access. */
#undef gc_root_count
static void gc_dump_roots_dbg(void)
{
    /* During STW GC all threads are stopped; cl_thread_list is stable — no lock needed. */
    CL_Thread *t;
    char buf[256];

    for (t = cl_thread_list; t; t = t->next) {
        int i;
        snprintf(buf, sizeof(buf),
                 "GC-ROOTS: thread %u — %d protected roots:\n",
                 (unsigned)t->id, t->gc_root_count);
        platform_write_string(buf);

        for (i = 0; i < t->gc_root_count; i++) {
            CL_Obj val = *t->gc_roots[i];
            int is_heap = (!CL_NULL_P(val) && !CL_FIXNUM_P(val) && !CL_CHAR_P(val)
                           && val != CL_UNBOUND && val < cl_heap.arena_size);
            int marked  = (is_heap && CL_HDR_MARKED(CL_OBJ_TO_PTR(val)));

            snprintf(buf, sizeof(buf),
                     "  root[%d] val=0x%08x %s  @ %s:%d\n",
                     i, (unsigned)val,
                     is_heap ? (marked ? "(heap,marked)" : "(heap,UNMARKED)") : "(imm)",
                     t->gc_root_files[i] ? t->gc_root_files[i] : "?",
                     t->gc_root_lines[i]);
            platform_write_string(buf);
        }
    }
}
#define gc_root_count (CT->gc_root_count)

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
                gc_verify_check_ref(parent_off, "lambda-list", bc->source_lambda_list);
                for (i = 0; i < bc->n_constants; i++)
                    gc_verify_check_ref(parent_off, "const", bc->constants[i]);
                break;
            }
            case TYPE_VECTOR: {
                CL_Vector *v = (CL_Vector *)ptr;
                uint32_t i;
                if (v->flags & CL_VEC_FLAG_DISPLACED) {
                    gc_verify_check_ref(parent_off, "displaced",
                                        v->data[CL_DISP_BASE_IDX(v)]);
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
            case TYPE_RESTART: {
                CL_Restart *r = (CL_Restart *)ptr;
                gc_verify_check_ref(parent_off, "name", r->name);
                gc_verify_check_ref(parent_off, "function", r->function);
                gc_verify_check_ref(parent_off, "report", r->report);
                gc_verify_check_ref(parent_off, "interactive", r->interactive);
                gc_verify_check_ref(parent_off, "test", r->test);
                gc_verify_check_ref(parent_off, "tag", r->tag);
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
                gc_verify_check_ref(parent_off, "exported", p->exported_symbols);
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
    /* The poison word lives at bytes 8..11 — it must stay inside the
     * walked arena (callers pass arbitrary CL_Obj values here). */
    if (offset + 12 > cl_heap.bump)
        return 0;
    /* Blocks of exactly sizeof(CL_FreeBlock) — the 8-byte pin-gap chunks
     * gc_make_free_gap can emit — have no room for poison: their bytes
     * 8..11 belong to the NEXT block, so testing them would misreport
     * based on a neighbor's data.  Treat as not-freed (a false negative
     * only; this is a DEBUG_GC diagnostic helper). */
    if (CL_HDR_SIZE(p) <= sizeof(CL_FreeBlock))
        return 0;
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
                CHECK_FIELD(bc->source_lambda_list, "lambda-list");
                break;
            }
            case TYPE_VECTOR: {
                CL_Vector *v = (CL_Vector *)ptr;
                uint32_t i;
                if (v->flags & CL_VEC_FLAG_DISPLACED) {
                    CHECK_FIELD(v->data[CL_DISP_BASE_IDX(v)], "displaced");
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

    /* Drop every thread's TLAB before any pass runs: gc_sweep rebuilds the
     * free list from unmarked regions, so an active chunk tail (already a
     * formatted hole) would otherwise be handed out twice — once by the
     * free list, once by the owning thread's next cut.  Runs even when
     * single-threaded to reclaim leftovers after thread-count dropped. */
    gc_tlab_reset_all();

#ifdef DEBUG_GC
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "GC #%lu: marking...\n",
                 (unsigned long)(cl_heap.gc_count + 1));
        platform_write_string(buf);
    }
#endif
    cl_stream_outbuf_gc_mark_begin();
    gc_mark();
    /* Reclaim outbuf slots no live stream pinned, before gc_sweep coalesces
     * the dead stream objects into free blocks. */
    cl_stream_outbuf_gc_reclaim();
    gc_srcloc_invalidate_dead();
#ifdef DEBUG_GC
    gc_verify_marked();
    if (gc_verify_errors > 0)
        gc_dump_roots_dbg();
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
    sprintf(buf, "GC mark stack: %lu entries (%lu grows, "
            "%lu overflow re-scan passes)\n",
            (unsigned long)gc_mark_stack_cap,
            (unsigned long)gc_mark_stack_grows,
            (unsigned long)gc_mark_rescan_passes);
    platform_write_string(buf);
#ifdef CL_TLAB
    sprintf(buf, "TLAB: %lu-byte chunks, %lu refills\n",
            (unsigned long)tlab_chunk_size,
            (unsigned long)cl_heap.tlab_refills);
    platform_write_string(buf);
#endif
}
