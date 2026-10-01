/* jit_common.c — the CPU-independent half of the native-code JIT.
 *
 * When to compile (the hot-call policy, specs/lazy-jit.md), the on/off
 * switches and the counters are the same for every backend; what to emit
 * and how to enter it are not (jit_backend.h).  Built whenever a backend
 * is: JIT_M68K (jit.c) or JIT_A64 (jit_a64.c).
 */

#include "jit/jit.h"

#ifdef CL_JIT_NATIVE

#include "jit/jit_backend.h"
#include "core/mem.h"        /* cl_call_gen_bump */
#include "core/peephole.h"   /* cl_bytecode_has_backward_jump */
#ifdef DEBUG_JIT_HOT
#include <stdio.h>
#include "core/symbol.h"
#include "platform/platform.h"
#endif

int      cl_jitc_active = 0;

/* Opt-in: when set, cl_jit_invoke pushes a shadow CL_Frame per call so
 * EXT:BACKTRACE / EXT:FRAME-LOCALS and the error-time backtrace can see
 * JIT'd functions.  Off by default — the push costs a few % on call-heavy
 * code, and only matters when something actually reads a backtrace (an
 * error, the SLDB debugger, an explicit (ext:backtrace)).  A debug session
 * (Sly/SLDB) or a test that needs JIT frame introspection turns it on. */
int      cl_jitc_shadow_frames = 0;

/* Bumped on every cl_jit_invoke entry.  Lets Lisp-side tests prove
 * the native dispatch path was actually taken (since a correctly
 * compiled native fn returns the same value the bytecode would). */
uint32_t cl_jitc_invoke_count = 0;

/* Native code installed since boot, in bytes -- cumulative (code freed with
 * a dead function is not subtracted), for measuring what a load compiles. */
uint32_t cl_jitc_native_bytes = 0;

void cl_jit_set_shadow_frames(int on)
{
    cl_jitc_shadow_frames = on ? 1 : 0;
    cl_call_gen_bump("shadow frames");   /* the site fill rule changed */
}
int  cl_jit_shadow_frames_enabled(void) { return cl_jitc_shadow_frames; }

uint32_t cl_jit_invoke_count_get(void) { return cl_jitc_invoke_count; }

uint32_t cl_jit_native_bytes(void) { return cl_jitc_native_bytes; }

void cl_jit_init(void)
{
    cl_jit_backend_init();
    cl_jitc_active = 1;
}

void cl_jit_shutdown(void)
{
    cl_jit_backend_shutdown();
}

void cl_jit_compile(CL_Bytecode *bc)
{
    cl_jit_backend_compile(bc, 1);
}

/* --- Hot compilation ----------------------------------------------------
 *
 * Compiling every function at definition cost about 35% of load time and
 * over a megabyte of native code for the editor on a 68040, most of it for
 * code that runs once or never; and a heap image, whose native code is
 * dropped on restore, used to run as bytecode for the rest of the session.
 * Now a function is compiled when it turns hot (jit.h). */

static int      jit_hot_threshold = CL_JIT_HOT_DEFAULT;
static uint32_t jit_hot_compiles = 0;
/* --no-jit (cl_jit_disable_for_session), as opposed to a %JIT-SET-ACTIVE
 * NIL window: a call settles its callee, so a function restored from an
 * image pays the slow arm once, not on every call of the session. */
static int      jit_session_off = 0;

void cl_jit_set_hot_threshold(int calls)
{
    if (calls < 0) calls = 0;
    if (calls > CL_JIT_HOT_MAX) calls = CL_JIT_HOT_MAX;
    jit_hot_threshold = calls;
}

int cl_jit_hot_threshold(void) { return jit_hot_threshold; }

uint32_t cl_jit_hot_compile_count(void) { return jit_hot_compiles; }

void cl_jit_note_definition(CL_Bytecode *bc)
{
    if (bc == NULL) return;
    if (!cl_jitc_active) {
        /* Defined with the JIT off (--no-jit, or %JIT-SET-ACTIVE NIL
         * around a DEFUN for an A/B benchmark): it stays bytecode. */
        bc->jit_hot |= CL_BC_JIT_SETTLED;
        return;
    }
    if (jit_hot_threshold == 0 || (bc->jit_hot & CL_BC_JIT_SPEED)) {
        bc->jit_hot |= CL_BC_JIT_SETTLED;
        cl_jit_compile(bc);
        return;
    }
    bc->jit_hot &= CL_BC_JIT_SPEED;   /* start counting */
}

void cl_jit_note_call(CL_Bytecode *bc)
{
    uint32_t n;

    if (!cl_jitc_active) {
        /* Nothing counts while the JIT is off.  Only the definition-time
         * state pins a function to bytecode: a benchmark that times its
         * bytecode variant inside a %JIT-SET-ACTIVE NIL window must not
         * pin the callees its JIT variant runs afterwards. */
        if (jit_session_off) bc->jit_hot |= CL_BC_JIT_SETTLED;
        return;
    }
    n = bc->jit_hot & CL_BC_JIT_COUNT_MASK;
    /* A loop pays for its compile inside its first call, so it does not
     * wait for the threshold; nor does a (speed 3) function, which lands
     * here only after an image restore dropped its native code. */
    if (n == 0 && ((bc->jit_hot & CL_BC_JIT_SPEED) ||
                   cl_bytecode_has_backward_jump(bc->code, bc->code_len,
                                                 bc->constants, bc->n_constants)))
        n = CL_JIT_HOT_MAX;
    else
        n++;
    if (n < (uint32_t)jit_hot_threshold) {
        /* This store is a plain overwrite, not the settle arm's OR, so it
         * can revert CL_BC_JIT_SETTLED (== CL_BC_JIT_COUNT_MASK) back to a
         * small count if another thread settled bc using a fresher read
         * while this n went stale.  Re-check right before writing so a
         * settle already landed is never undone -- once declined, a
         * function must stay declined instead of becoming eligible to
         * re-enter the backend's compile. */
        if (CL_BC_JIT_COUNTING_P(bc))
            bc->jit_hot = (uint8_t)((bc->jit_hot & CL_BC_JIT_SPEED) | n);
        return;
    }
    /* Settle first: whatever the JIT decides, it decides once. */
    bc->jit_hot |= CL_BC_JIT_SETTLED;
    jit_hot_compiles++;
    cl_jit_backend_compile(bc, 0);
#ifdef DEBUG_JIT_HOT
    {
        char line[160];
        snprintf(line, sizeof(line), "; [jit-hot] %s: %s (%lu bytes)\n",
                 CL_SYMBOL_P(bc->name) ? cl_symbol_name(bc->name) : "<anon>",
                 bc->native_code ? "compiled" : "declined",
                 (unsigned long)bc->native_len);
        platform_write_string(line);
    }
#endif
}

void cl_jit_compile_if_counting(CL_Bytecode *bc)
{
    if (bc == NULL || !cl_jitc_active || bc->native_code || !CL_BC_JIT_COUNTING_P(bc))
        return;
    bc->jit_hot |= CL_BC_JIT_SETTLED;
    cl_jit_backend_compile(bc, 0);
}

int cl_jit_enabled(void) { return cl_jitc_active; }

void cl_jit_set_active(int active)
{
    cl_jitc_active = active ? 1 : 0;
    if (active) jit_session_off = 0;
    cl_call_gen_bump("jit active");
}

void cl_jit_disable_for_session(void)
{
    cl_jitc_active = 0;
    jit_session_off = 1;
}

#endif /* CL_JIT_NATIVE */
