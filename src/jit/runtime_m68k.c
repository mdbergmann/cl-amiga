/* runtime_m68k.c -- the m68k walker's own runtime helpers (jit_m68k.c).
 *
 * Each helper has a stable C ABI (args on stack, result in D0) so that
 * JIT'd native code can call it via plain JSR.  The operand stack of an
 * m68k native frame is the m68k stack itself, so these helpers take
 * `operand_top` pointers into it (last argument lowest) and build the NLX
 * frames the walker sandwiches an inline `JSR setjmp` into.  The
 * CPU-neutral slow paths both walkers share are in runtime.c.
 *
 * GC: allocating helpers (jit_dispatch into a Lisp callee that allocates,
 * the condition objects of the NLX paths, ...) are safe because the
 * conservative m68k-stack scan with offset validation in
 * mem.c::gc_scan_jit_native_stack roots -- and the compactor forwards --
 * the operand-stack values the walker flushed before the JSR.
 */

#ifdef JIT_M68K

#include "jit/runtime.h"
#include "jit/runtime_m68k.h"
#include "core/types.h"
#include "core/float.h"      /* CL_NUMBER_P, CL_REALP */
#include "core/error.h"
#include "core/symbol.h"     /* SYM_STAR_PACKAGE, SYM_TYPE_ERROR, KW_DATUM, KW_EXPECTED_TYPE */
#include "core/printer.h"    /* cl_prin1_to_string (OP_ASSERT_TYPE diagnostic) */
#include "core/package.h"    /* cl_sync_current_package_from_dynamic */
#include "core/thread.h"     /* cl_symbol_value / cl_set_symbol_value */
#include "core/opcodes.h"    /* CL_CMP_BR_*, CL_AREF_KIND_* */
#include "core/vm.h"         /* cl_dynbind_restore_to, CL_MAX_DYN_BINDINGS, CL_NLXFrame */
#include "core/mem.h"        /* cl_heap.arena_size */
#include "core/compiler.h"   /* cl_compiler_mark / cl_compiler_unwind_to,
                              * cl_amiga_ffi_call_dispatch */
#include "core/fasl.h"       /* cl_fasl_{reader,writer}_unwind_to */
#include "core/string_utils.h" /* cl_string_length, cl_string_set_char_at */
#include "core/builtins.h"   /* cl_ffi_stub_call (jit_dispatch) */
#include "jit/jit.h"         /* cl_jit_invoke (jit_dispatch) */
#include "platform/platform.h" /* CL_NOINLINE */
#include "platform/platform_thread.h" /* platform_atomic_cas (call-site fill) */
#include <setjmp.h>
#include <string.h>          /* memcpy for mv_values preservation */

extern CL_Obj cl_car(CL_Obj obj);
extern CL_Obj cl_cdr(CL_Obj obj);
extern CL_Obj cl_cons(CL_Obj car, CL_Obj cdr);

/* From src/core/vm.c — the universal call entry point.  Handles C
 * builtins directly, sets up a stub frame for bytecode/closures, and
 * dispatches through cl_vm_run (which itself routes to native_code if
 * the callee carries one). */
extern CL_Obj cl_vm_apply(CL_Obj func, CL_Obj *args, int nargs);

/* Address of libc `setjmp`, baked into the JSR.abs.l emitted for
 * OP_BLOCK_PUSH.  The setjmp call must originate from the JIT'd
 * function's own stack frame (so a later longjmp restores SP to the
 * correct point) — that means a JSR direct to setjmp, *not* a JSR
 * into a wrapper helper that calls setjmp and returns: such a wrapper
 * would let its frame disappear before longjmp could rewind to it,
 * which is undefined behaviour per C99 §7.13.1.1.  Captured at
 * init-time rather than recomputed on every emit. */
uint32_t cl_jit_setjmp_addr;

void cl_jit_runtime_init(void)
{
    cl_jit_setjmp_addr = (uint32_t)(uintptr_t)&setjmp;
}

/* Backing for OP_CALL.  See runtime.h for the operand-stack layout
 * the JIT delivers.  Reverse-copies args into a stack-local CL_Obj[]
 * because cl_vm_apply expects args[0..N-1] = arg0..argN-1 in natural
 * order, while the m68k operand stack has argN-1 at the lowest
 * address.  The copy is bounded by OP_CALL's u8 nargs limit (256),
 * so a fixed-size buffer is sufficient.
 *
 * GC caveat — same as every allocating slow path in this file:
 * operand-stack slots and LINK-frame locals on the m68k stack
 * aren't reached by the current collector's root scan.  cl_vm_apply
 * may allocate (cons frames, format strings, the callee's own
 * arena objects), which may trigger GC.  Workloads whose live
 * unscanned m68k-stack values never overlap an allocation window
 * stay safe; the test suite's 2456 passes show this is the common
 * case in practice, but `(let ((x (alloc))) (other-call x))` has
 * a real exposure window between OP_STORE x and the next OP_LOAD x.
 * Conservative m68k-stack scanning at safepoints is the spec'd
 * fix, tracked under §"Open design choices" in
 * specs/native-backend.md. */
/* --- The call path ----------------------------------------------------
 *
 * Every call a JIT'd function makes lands in one of the two helpers
 * below with the arguments on the m68k operand stack (operand_top[0] =
 * last argument ... [nargs-1] = first; OP_CALL's function value is at
 * [nargs]).  They used to hand the callee to cl_vm_apply unconditionally,
 * so every native call site paid the generic trampoline: two
 * funcallable-instance probes, a C-stack probe (FindTask + bounds), the
 * arguments copied into a C array and then onto the VM stack, and for a
 * Lisp callee a stub OP_CALL frame plus a nested cl_vm_run activation
 * (its own C-stack probe, thread lookup and dispatch) before
 * cl_jit_invoke was reached.  trunk/bench-jit-call.lisp on FS-UAE 68040:
 * a JIT'd caller paid 13.4 us per call to a native leaf against the
 * interpreter's 11.8, and 15.2 vs 13.7 to a bytecode leaf -- the JIT lost
 * on exactly the call-heavy generic code an editor is made of
 * (clamacs/specs/clamacs-lisp.md, phase 0).
 *
 * jit_dispatch now handles the callee kinds that need no interpreter
 * frame the way cl_vm_run's OP_CALL does:
 *   - a C builtin or an FFI stub: the arguments are copied once, onto the
 *     GC-rooted VM stack (builtins assume rooted arguments), then the C
 *     function is called.  No C-stack probe, as in the VM: a builtin does
 *     not grow the C stack by itself, and the ones that call back into
 *     Lisp go through cl_vm_apply, which is guarded;
 *   - a bytecode/closure callee that carries native code and whose lambda
 *     list the argument count fits (the VM's own rule): a safepoint poll,
 *     one C-stack probe (native frames nest on the m68k stack, so runaway
 *     recursion must still reach the guard) and cl_jit_invoke;
 *   - any other bytecode/closure callee (interpreted, or an arity
 *     mismatch): the stub OP_CALL frame, but entered directly
 *     (cl_vm_call_bytecode) from the operand stack.
 * Everything else -- a generic function (the reader/writer inline caches
 * live in cl_vm_apply), a traced function, a corrupted object -- takes
 * cl_vm_apply as before, so every diagnostic is unchanged.  Both helpers allocate (the callee may),
 * so the walker cache_flushes before the JSR. */

/* The trampoline arm: the arguments go onto the VM stack, in natural order,
 * right below sp, and cl_vm_apply takes them from there (it pushes its own
 * copies above sp, so the two never overlap; the slice is a GC root
 * meanwhile).  They used to go into a CL_Obj[256] in this frame: 1 KB of C
 * stack per native call into a generic function, on top of cl_vm_run's own
 * frame -- with &rest functions native, ASDF's FIND-SYSTEM on the 128K suite
 * stack nested deep enough to hit the C-stack guard.  Never inlined, so
 * jit_dispatch's frame (under the callee for the whole call) stays small. */
#ifdef JIT_M68K
static CL_Obj jit_dispatch_apply(CL_Thread *thr, CL_Obj func,
                                 CL_Obj *operand_top, uint32_t nargs) CL_NOINLINE;
static CL_Obj jit_dispatch_apply(CL_Thread *thr, CL_Obj func,
                                 CL_Obj *operand_top, uint32_t nargs)
{
    int base = thr->vm.sp;
    uint32_t i;
    CL_Obj result;
    if (base + (int)nargs >= (int)thr->vm.stack_size - 16)
        cl_error(CL_ERR_OVERFLOW, "VM stack overflow");
    for (i = 0; i < nargs; i++)
        thr->vm.stack[base + (int)i] = operand_top[nargs - 1 - i];
    thr->vm.sp = base + (int)nargs;
    result = cl_vm_apply(func, &thr->vm.stack[base], (int)nargs);
    thr->vm.sp = base;
    return result;
}

#endif /* JIT_M68K */

#ifdef JIT_M68K
/* --- Direct native-to-native calls (specs/jit-direct-calls.md) ----------
 *
 * Every call site in native code owns a CL_JitCallSite cell.  The emitted
 * hit path (jit_m68k.c, emit_call_site) reads gen/func/entry, then compares the
 * gen it read against cl_call_gen, and JSRs straight into `entry` with
 * `func` pushed over the arguments -- no helper, no argument copy, no
 * FindTask.  Anything else lands here, in the miss path: the dispatch the
 * site used to call unconditionally, after an attempt to fill the cell.
 *
 * Filling races with readers on other tasks (the m68k JIT is single-CPU,
 * but preemptive).  The rules that keep a reader from ever pairing one
 * fill's func with another's entry:
 *   - the hit path reads all three words BEFORE comparing gen, so a reader
 *     preempted mid-read compares a gen whose site has since changed;
 *   - a site's func/entry change only while site->gen != cl_call_gen, and
 *     cl_call_gen only grows: a reader holding the old gen then misses;
 *   - fillers serialize through a try-lock (a contended fill is skipped;
 *     the next miss retries), and write func, entry, then gen;
 *   - G, the gen recorded, is read BEFORE any input of the fill rule.
 *     Every input -- the function cell, native_code, TRACE, shadow frames,
 *     the kill switch -- changes before its own cl_call_gen bump, so a
 *     fill that saw an old input records an old gen and simply misses.
 *
 * The counters are process-wide statics written on the miss path only;
 * the m68k JIT runs on one CPU, so there is no cache line to bounce. */
static int jit_direct_calls = 1;
static uint32_t jit_ds[CL_JIT_DS_COUNT];
static volatile uint32_t jit_site_fill_lock = 0;

void cl_jit_set_direct_calls(int on)
{
    jit_direct_calls = on ? 1 : 0;
    cl_call_gen_bump("direct calls");   /* the fill rule changed */
}

int cl_jit_direct_calls_enabled(void) { return jit_direct_calls; }

void cl_jit_direct_call_stats(uint32_t *out)
{
    int i;
    for (i = 0; i < CL_JIT_DS_COUNT; i++) out[i] = jit_ds[i];
}

/* Does a call with NARGS arguments fit BC's lambda list?  The interpreter's
 * OP_CALL bounds: arity <= nargs <= arity + n_optional, any count above
 * arity with &rest or &key.  A call outside them takes the interpreter,
 * which signals the arity error. */
static int jit_fits(const CL_Bytecode *bc, uint32_t nargs)
{
    uint32_t arity = bc->arity & 0x7FFFu;
    if (nargs < arity) return 0;
    return (bc->flags & 1) || (bc->arity & 0x8000) ||
           nargs <= arity + bc->n_optional;
}

/* Fill SITE with a native callee jit_dispatch has classified (BC carries
 * native code and the call fits its lambda list).  G was read before any
 * input of the fill rule; see above. */
static void jit_site_try_fill(CL_Thread *thr, CL_JitCallSite *site,
                              uint32_t g, CL_Obj func, CL_Bytecode *bc,
                              uint32_t nargs)
{
    if (!jit_direct_calls) return;
    if (!cl_jit_shadow_frames_enabled()) {
        jit_ds[CL_JIT_DS_REFUSED_SHADOW]++;     /* the hit path pushes a frame */
        return;
    }
    if (cl_traced_function_count != 0 || thr->trace_count != 0) {
        jit_ds[CL_JIT_DS_REFUSED_TRACE]++;      /* traced calls take cl_vm_apply */
        return;
    }
    /* The positional native ABI (jit_dispatch has checked jit_fits): the
     * hit path pushes the arguments and passes their count in D1, which
     * covers &optional, &rest and &key -- every shape the walker takes. */
    if (nargs > CL_JIT_MAX_POSITIONAL) {
        jit_ds[CL_JIT_DS_REFUSED_ABI]++;
        return;
    }
    if (!platform_atomic_cas(&jit_site_fill_lock, 0, 1))
        return;                                 /* a peer is filling; retry later */
    if (site->gen != cl_call_gen) {             /* never rewrite a live site */
        site->gen   = 0;
        site->func  = func;
        site->entry = bc->native_code;
        site->code  = bc->code;
        site->gen   = g;
        jit_ds[CL_JIT_DS_FILLS]++;
    }
    jit_site_fill_lock = 0;
}

/* SITE is the call-site cell of a miss (NULL from anywhere else), G the
 * cl_call_gen the miss read.  The fill attempt sits in the native arm, so
 * the calls that miss on every call -- into builtins and interpreted
 * functions -- pay no classification of their own (measured: classifying
 * up front cost those calls ~0.2 us each on a 68040). */
static CL_Obj jit_dispatch(CL_Thread *thr, CL_Obj func, CL_Obj *operand_top,
                           uint32_t nargs, CL_JitCallSite *site, uint32_t g)
{
    uint32_t i;
    uint32_t ftype = 0xFFu;

    if (CL_HEAP_P(func) && func < cl_heap.arena_size)
        ftype = CL_HDR_TYPE(CL_OBJ_TO_PTR(func));

    if (thr->trace_count == 0 &&
        thr->vm.sp + (int)nargs < (int)thr->vm.stack_size - 16) {
        if (ftype == TYPE_FUNCTION || ftype == TYPE_FFI_STUB) {
            int base = thr->vm.sp;
            CL_Obj result;
            if (site != NULL) jit_ds[CL_JIT_DS_REFUSED_NOT_NATIVE]++;
            for (i = 0; i < nargs; i++)
                thr->vm.stack[base + (int)i] = operand_top[nargs - 1 - i];
            thr->vm.sp = base + (int)nargs;
            if (ftype == TYPE_FUNCTION) {
                result = cl_vm_call_builtin(thr, (CL_Function *)CL_OBJ_TO_PTR(func),
                                            &thr->vm.stack[base], (int)nargs);
            } else {
                result = cl_ffi_stub_call(func, &thr->vm.stack[base], (int)nargs);
                thr->mv_count = 1;
                thr->mv_values[0] = result;
            }
            thr->vm.sp = base;
            return result;
        }
        if (ftype == TYPE_BYTECODE || ftype == TYPE_CLOSURE) {
            CL_Bytecode *bc = cl_jit_bytecode_of(func, ftype);
            /* No hot-call count here: an interpreted callee goes on to the
             * stub frame below, whose OP_CALL counts it (jit.h).  Counting
             * here too made a native caller's callee hot after half the
             * threshold. */
            if (bc != NULL && bc->native_code != NULL) {
                if (jit_fits(bc, nargs)) {
                    int base;
                    CL_Obj result;
                    if (site != NULL)
                        jit_site_try_fill(thr, site, g, func, bc, nargs);
                    if (thr->gc_requested) cl_gc_safepoint();
                    if (thr->interrupt_pending) cl_thread_handle_interrupt(thr);
                    /* The safepoint above can run a peer thread's stop-the-
                     * world (compacting) collection, which relocates the
                     * CL_Bytecode/CL_Closure bc was resolved from -- bc is
                     * a raw pointer, not a CL_Obj, so compaction does not
                     * fix it up in place.  Re-resolve it from func before
                     * dereferencing it again. */
                    bc = cl_jit_bytecode_of(func, ftype);
                    cl_check_c_stack("a native call");
                    base = thr->vm.sp;
                    for (i = 0; i < nargs; i++)
                        thr->vm.stack[base + (int)i] = operand_top[nargs - 1 - i];
                    thr->vm.sp = base + (int)nargs;
                    result = cl_jit_invoke(func, bc, (int)nargs);
                    thr->vm.sp = base;
                    return result;
                }
            }
            /* An interpreted callee (or one whose lambda list the call does
             * not fit: the arity diagnostic is OP_CALL's): the stub-frame
             * path, entered with the operand-stack layout as it is. */
            if (site != NULL) jit_ds[CL_JIT_DS_REFUSED_NOT_NATIVE]++;
            if (bc != NULL)
                return cl_vm_call_bytecode(thr, func, operand_top, (int)nargs, 1);
        }
    }

    /* While anything is traced (trace_count counts this thread's traced
     * symbols) every call takes the trampoline above, so the native arm's
     * fill never runs: the refusal is TRACE's. */
    if (site != NULL)
        jit_ds[thr->trace_count != 0 || cl_traced_function_count != 0
               ? CL_JIT_DS_REFUSED_TRACE : CL_JIT_DS_REFUSED_NOT_NATIVE]++;
    return jit_dispatch_apply(thr, func, operand_top, nargs);
}

/* Miss path of an OP_CALL / OP_TAILCALL site: the callee is the function
 * value under the arguments. */
CL_Obj cl_jit_runtime_call_site(CL_Obj *operand_top, uint32_t nargs,
                                CL_JitCallSite *site)
{
    CL_Thread *thr = cl_get_current_thread();
    uint32_t g;
    if (nargs > 255) nargs = 255;   /* defensive -- OP_CALL is u8 */
    /* A hit never polls: a requester sets the flag, then bumps
     * cl_call_gen, so this thread's next call misses and lands here. */
    if (thr->gc_requested) cl_gc_safepoint();
    if (thr->interrupt_pending) cl_thread_handle_interrupt(thr);
    jit_ds[CL_JIT_DS_MISSES]++;
    g = cl_call_gen;
    return jit_dispatch(thr, operand_top[nargs], operand_top, nargs, site, g);
}

/* Miss path of an OP_CALL_GLOBAL / OP_TAILCALL_GLOBAL site (and the fused
 * LOAD_/GLOAD_CALL_GLOBAL heads): the callee is SYM's function. */
CL_Obj cl_jit_runtime_call_global_site(CL_Obj *operand_top, uint32_t nargs,
                                       CL_Obj sym, CL_JitCallSite *site)
{
    CL_Thread *thr = cl_get_current_thread();
    uint32_t g;
    CL_Obj func;
    if (nargs > 255) nargs = 255;   /* defensive -- the operand is a u8 */
    if (thr->gc_requested) cl_gc_safepoint();
    if (thr->interrupt_pending) cl_thread_handle_interrupt(thr);
    jit_ds[CL_JIT_DS_MISSES]++;
    g = cl_call_gen;
    func = cl_jit_runtime_fload(sym);   /* UNDEFINED-FUNCTION as before */
    return jit_dispatch(thr, func, operand_top, nargs, site, g);
}

#endif /* JIT_M68K */

#ifdef JIT_M68K

/* The NLX frames, the multiple-value save stack and the handler/restart
 * bindings are shared with the AArch64 walker: runtime_nlx.c. */

/* See runtime_m68k.h for the contract.  Implementation tracks vm.c's
 * normal-call frame setup (the "Normal call: push new frame" branch in
 * OP_CALL) closely so behaviour stays in lock-step.
 *
 * Argument i sits at LAST[nargs-1-i] (operand-stack order above A6, as
 * in cl_jit_runtime_rest_prologue); ARG(i) reads it.
 *
 * The only allocation is the &rest list.  Everything it can disturb is
 * safe by then: the arguments and the frame slots are on the m68k stack
 * (the conservative scan pins what they reference), and the bytecode --
 * a raw pointer, which a compaction does not fix up -- is re-derived
 * from FUNC (8(a6), pinned the same way) after the consing. */
void cl_jit_runtime_kw_prologue(CL_Obj func, uint32_t nargs,
                                CL_Obj *last, CL_Obj *frame)
{
#define ARG(i) (last[nargs - 1 - (i)])
    uint32_t i;
    CL_Bytecode *bc = cl_jit_bytecode_of(func, CL_HDR_TYPE(CL_OBJ_TO_PTR(func)));
    uint32_t arity        = (uint32_t)(bc->arity & 0x7FFFu);
    uint32_t n_opt        = bc->n_optional;
    int      allow        = (bc->flags & 2) != 0;
    uint32_t n_locals     = bc->n_locals;
    uint32_t n_positional = arity + n_opt;
    uint32_t n_extra;

    /* nargs <= 255: OP_CALL's byte, the bound of every entry. */

    /* NIL-initialize every frame slot.  Mirrors the VM's
     * `while (sp < bp + n_locals) push(NIL)` so any slot the body
     * reads before writing observes NIL, and so the suppliedp slots
     * default to NIL for keys whose argument is omitted. */
    for (i = 0; i < n_locals; i++) frame[i] = CL_NIL;

    /* Required + optional positional args copy across directly. */
    {
        uint32_t copy_n = (nargs < n_positional) ? nargs : n_positional;
        for (i = 0; i < copy_n; i++) frame[i] = ARG(i);
    }

    n_extra = (nargs > n_positional) ? (nargs - n_positional) : 0;

    /* &rest: the arguments past the positional ones, in the slot right
     * after them (the compiler's layout, before the keyword slots). */
    if (bc->arity & 0x8000u) {
        CL_Obj rest = CL_NIL;
        int32_t j;
        CL_GC_PROTECT(rest);
        for (j = (int32_t)n_extra - 1; j >= 0; j--)
            rest = cl_cons_rooted(&ARG(n_positional + (uint32_t)j), &rest);
        CL_GC_UNPROTECT(1);
        frame[n_positional] = rest;
        bc = cl_jit_bytecode_of(func, CL_HDR_TYPE(CL_OBJ_TO_PTR(func)));
    }

    if ((bc->flags & 1) == 0) return;   /* not reached: &key shapes only */

    /* Odd-arg-count check per CLHS 3.4.1.4. */
    if (n_extra & 1u)
        cl_error(CL_ERR_ARGS, "odd number of keyword arguments");

    /* Scan for an explicit `:allow-other-keys t` from the caller. */
    if (!allow) {
        uint32_t k;
        for (k = 0; k + 1 < n_extra; k += 2) {
            if (ARG(n_positional + k) == KW_ALLOW_OTHER_KEYS &&
                !CL_NULL_P(ARG(n_positional + k + 1))) {
                allow = 1;
                break;
            }
        }
    }

    /* Match pairs right-to-left so the leftmost duplicate keyword
     * wins (CLHS 3.4.1.4.1). */
    if (n_extra >= 2) {
        int32_t last_ki = (int32_t)n_extra - 2;
        int32_t ki;
        if (last_ki & 1) last_ki--;
        for (ki = last_ki; ki >= 0; ki -= 2) {
            CL_Obj key = ARG(n_positional + (uint32_t)ki);
            CL_Obj val = ARG(n_positional + (uint32_t)ki + 1);
            int j;
            int found = 0;
            for (j = 0; j < bc->n_keys; j++) {
                if (key == bc->key_syms[j]) {
                    frame[bc->key_slots[j]] = val;
                    if (bc->key_suppliedp_slots)
                        frame[bc->key_suppliedp_slots[j]] = CL_T;
                    found = 1;
                    break;
                }
            }
            if (!found && key != KW_ALLOW_OTHER_KEYS && !allow)
                cl_error(CL_ERR_ARGS, "Unknown keyword argument: %s",
                         cl_symbol_name(key));
        }
    }
#undef ARG
}

/* See runtime_m68k.h.  Argument i sits at LAST[nargs-1-i] (operand-stack
 * order above A6), so walking LAST upwards from the last argument conses
 * the &rest list back to front.  The consing may collect: the arguments
 * and the frame are on the m68k stack, which the conservative scan pins,
 * and the bytecode is read before it. */
void cl_jit_runtime_rest_prologue(CL_Obj func, uint32_t nargs,
                                  CL_Obj *last, CL_Obj *frame)
{
    CL_Bytecode *bc = cl_jit_bytecode_of(func, CL_HDR_TYPE(CL_OBJ_TO_PTR(func)));
    uint32_t n_pos    = (uint32_t)(bc->arity & 0x7FFFu) + bc->n_optional;
    uint32_t n_locals = bc->n_locals;
    uint32_t i;
    CL_Obj rest = CL_NIL;

    if (nargs > 255) nargs = 255;
    for (i = 0; i < n_locals; i++) frame[i] = CL_NIL;
    for (i = 0; i < nargs && i < n_pos; i++) frame[i] = last[nargs - 1 - i];
    if (nargs > n_pos) {
        CL_GC_PROTECT(rest);
        for (i = 0; i < nargs - n_pos; i++)
            rest = cl_cons_rooted(&last[i], &rest);
        CL_GC_UNPROTECT(1);
    }
    frame[n_pos] = rest;
}

/* Backing for OP_AMIGA_CALL — mirrors the VM's dispatch in vm.c::
 * OP_AMIGA_CALL one-for-one so behaviour and error messages stay
 * identical between the bytecode and JIT paths.
 *
 *   base_sym   — the library-base symbol (baked into the JIT'd call
 *                site as a CL_Obj literal from the bytecode's
 *                constants[] table).
 *   offset     — LVO offset from the library base (i16 widened to i32
 *                by the caller; passed as int32_t for a clean 4-byte
 *                push slot).
 *   regspec    — packed register spec, bits 28-29 = result kind
 *                (CL_AMIGA_RES_*, builtins.h).
 *   n_args     — number of register args (0..7, validated by dispatch).
 *   operand_top — points at the most-recently-pushed arg on the m68k
 *                 operand stack.  Args lie at operand_top[0..n_args-1]
 *                 with operand_top[0] = argN-1 (the last pushed) and
 *                 operand_top[n_args-1] = arg0 (the first pushed).
 *
 * Reverse-copy into a stack-local CL_Obj[8] buffer so the dispatch
 * helper sees args in the same order the bytecode VM would
 * (`&cl_vm.stack[cl_vm.sp - n_args]` is bottom-to-top: buf[0] = arg0,
 * buf[n_args-1] = argN-1).  The conservative m68k-stack scan still
 * reaches the original args at operand_top, so even if dispatch
 * allocates (cl_make_bignum when the result exceeds CL_FIXNUM_MAX) the
 * caller's operand-stack values stay rooted across the call. */
CL_Obj cl_jit_runtime_amiga_call(CL_Obj base_sym, int32_t offset,
                                 uint32_t regspec, uint32_t n_args,
                                 CL_Obj *operand_top)
{
    CL_Obj args_buf[8];
    uint32_t base;
    uint32_t i;

    /* The VM path's checks (builtins_amiga.c), a NULL base included. */
    base = cl_amiga_library_base_address(base_sym);

    /* dispatch caps at 7; the buffer is sized to 8 anyway. */
    if (n_args > 7)
        cl_error(CL_ERR_ARGS,
                 "OP_AMIGA_CALL: too many register args (max 7), got %u",
                 (unsigned)n_args);

    for (i = 0; i < n_args; i++)
        args_buf[i] = operand_top[n_args - 1 - i];

    return cl_amiga_ffi_call_dispatch(base, (int16_t)offset,
                                      regspec, (int)n_args, args_buf);
}

/* OP_RESTART_PUSH (the other binding-stack opcodes, which do not depend on
 * the frame design, are in runtime_nlx.c). */
void cl_jit_runtime_restart_push(CL_Obj name_sym, CL_Obj handler, CL_Obj report,
                                 CL_Obj interactive, CL_Obj test, CL_Obj tag)
{
    CL_Obj restart;
    if (cl_restart_top >= CL_MAX_RESTART_BINDINGS)
        cl_error(CL_ERR_OVERFLOW, "Restart stack overflow");
    /* The five operands live on the JIT'd frame's m68k stack (the walker
     * flushed the cache before the JSR), so they're reachable through the
     * conservative native-stack scan; cl_make_restart also protects them. */
    restart = cl_make_restart(name_sym, handler, report, interactive, test, tag);
    cl_restart_stack[cl_restart_top].name = name_sym;
    cl_restart_stack[cl_restart_top].handler = handler;
    cl_restart_stack[cl_restart_top].tag = tag;
    cl_restart_stack[cl_restart_top].restart = restart;
    cl_restart_top++;
}

#endif /* JIT_M68K (the &key/&rest prologues, AMIGA_CALL, RESTART_PUSH) */

#endif /* JIT_M68K */
