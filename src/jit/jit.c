/* jit.c — orchestration for the m68k template JIT.
 *
 * Skeleton stage: the wiring is in place but no opcode has an emitter
 * yet, so cl_jit_compile always leaves native_code == NULL and every
 * call falls through to the bytecode interpreter.
 *
 * Filling in real codegen happens in codegen_m68k.c (one template per
 * opcode) and asm_m68k.c (instruction encoders).  See
 * specs/native-backend.md §"Implementation Sketch".
 */

#ifdef JIT_M68K

#include "jit/jit.h"
#include "jit/codebuf.h"
#include "jit/asm_m68k.h"
#include "jit/codegen_m68k.h"
#include "jit/runtime.h"
#include "platform/platform.h"

extern int cl_quiet_boot;

static int jit_active = 0;

void cl_jit_init(void)
{
    /* Future: allocate code-buffer pool, prime per-opcode emitter
     * table, install GC hooks for native_code reclamation. */
    cl_jit_runtime_init();
    cl_jit_codegen_init();
    jit_active = 1;

    if (!cl_quiet_boot) {
        platform_write_string(
            "; [jit] m68k template backend: skeleton (no codegen yet)\n");
    }
}

void cl_jit_compile(CL_Bytecode *bc)
{
    if (bc == NULL) return;

    /* Skeleton: declare ineligible.  When templates land, this is
     * where the call-count threshold + size cap + code-buffer alloc
     * lives, followed by the per-opcode dispatch loop. */
    bc->native_code = NULL;
    bc->native_len  = 0;

    (void)jit_active;
}

CL_Obj cl_jit_invoke(CL_Bytecode *bc, int nargs)
{
    (void)bc; (void)nargs;
    /* Unreachable in the skeleton (no bytecode ever has native_code
     * set), but kept so the call-site dispatch in vm.c can be wired
     * up independently of when emitters land. */
    return CL_NIL;
}

int cl_jit_enabled(void) { return jit_active; }

int cl_jit_emit_stub(CL_Bytecode *bc)
{
    CodeBuf cb;
    uint8_t *code;
    uint32_t len;

    if (bc == NULL) return 0;

    cb_init(&cb, 8);
    m68k_emit_nop(&cb);
    m68k_emit_rts(&cb);
    code = cb_finish(&cb, &len);
    if (code == NULL) return 0;

    /* Replace any prior native code attached to this bytecode. */
    if (bc->native_code) platform_free(bc->native_code);
    bc->native_code = code;
    bc->native_len  = len;

    /* NOTE: when execution gets wired up, an Amiga CacheClearU() call
     * must happen here — the 040 I-cache may hold stale lines from a
     * prior code-buffer reuse, leading to a Guru if executed. */
    return 1;
}

#endif /* JIT_M68K */
