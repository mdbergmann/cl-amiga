/* jit.c — orchestration for the m68k template JIT.
 *
 * Stage 1 (spec §"Up next" item 2): the first real codegen path
 * recognizes the bytecode shape the compiler emits for `(defun f () nil)`
 * and replaces it with a 4-byte `moveq #0,d0; rts`.  Every other
 * function still leaves `bc->native_code == NULL` and runs through
 * the bytecode interpreter unchanged.
 *
 * Filling in the rest of the staging-1 opcode set lands additional
 * patterns + operand-bearing encoders here.  See
 * specs/native-backend.md §"Suggested staging".
 */

#ifdef JIT_M68K

#include "jit/jit.h"
#include "jit/codebuf.h"
#include "jit/asm_m68k.h"
#include "jit/codegen_m68k.h"
#include "jit/runtime.h"
#include "core/opcodes.h"
#include "platform/platform.h"

extern int cl_quiet_boot;

static int jit_active = 0;

/* Bumped on every cl_jit_invoke entry.  Lets Lisp-side tests prove
 * the native dispatch path was actually taken (since a correctly
 * compiled native fn returns the same value the bytecode would). */
static uint32_t jit_invoke_count = 0;

uint32_t cl_jit_invoke_count_get(void) { return jit_invoke_count; }

void cl_jit_init(void)
{
    cl_jit_runtime_init();
    cl_jit_codegen_init();
    jit_active = 1;

    if (!cl_quiet_boot) {
        platform_write_string(
            "; [jit] m68k template backend: trivial-nil pattern only\n");
    }
}

/* Match the canonical 7-byte body the compiler emits for a zero-arg
 * lambda whose only form is NIL:
 *
 *   00: OP_NIL
 *   01: OP_STORE 0
 *   03: OP_POP
 *   04: OP_LOAD  0
 *   06: OP_RET
 *
 * Local 0 is the implicit block return-value slot.  Behavior:
 * no side effects, returns NIL.  The same shape with OP_T or
 * OP_CONST in place of OP_NIL is a future extension — keep this
 * matcher strict so a near-match never silently lands wrong code.
 */
static int matches_trivial_nil(const CL_Bytecode *bc)
{
    static const uint8_t pattern[7] = {
        OP_NIL,
        OP_STORE, 0x00,
        OP_POP,
        OP_LOAD,  0x00,
        OP_RET
    };
    uint32_t i;

    if (bc->code_len != 7) return 0;
    if (bc->arity != 0) return 0;
    if (bc->n_optional != 0) return 0;
    if (bc->flags != 0) return 0;          /* no &key, no allow-other-keys */
    if (bc->n_keys != 0) return 0;
    if (bc->n_upvalues != 0) return 0;
    /* n_locals == 1 is expected (the block return slot); anything else
     * is a shape we don't recognize yet. */
    if (bc->n_locals != 1) return 0;
    for (i = 0; i < 7; i++)
        if (bc->code[i] != pattern[i]) return 0;
    return 1;
}

void cl_jit_compile(CL_Bytecode *bc)
{
    CodeBuf cb;
    uint8_t *code;
    uint32_t len;

    if (bc == NULL || !jit_active) return;
    bc->native_code = NULL;
    bc->native_len  = 0;

    if (!matches_trivial_nil(bc)) return;

    /* Emit: moveq #0,d0 ; rts.  CL_NIL == 0, which fits trivially in
     * moveq's signed 8-bit immediate.  The m68k SysV ABI returns the
     * function result in D0, so a bare moveq+rts is a complete leaf
     * function callable from C. */
    cb_init(&cb, 4);
    m68k_emit_moveq(&cb, 0, REG_D0);
    m68k_emit_rts(&cb);
    code = cb_finish(&cb, &len);
    if (code == NULL) return;

    bc->native_code = code;
    bc->native_len  = len;

    /* Flush I-cache so the freshly written bytes aren't stale on
     * 68040/060.  On 68020/030 this is a no-op (no write-back I-cache). */
    platform_cache_clear(code, len);
}

/* Enter native code.  Currently only the zero-arg trivial-nil pattern
 * is ever compiled, so we ignore `nargs` and call as a no-arg leaf.
 * When more shapes land, this dispatches by arity / signature.
 *
 * m68k SysV ABI: D0 holds the return value on RTS; callee preserves
 * D2..D7 and A2..A6.  Our trivial fn clobbers only D0, so no register
 * save/restore is needed on either side. */
CL_Obj cl_jit_invoke(CL_Bytecode *bc, int nargs)
{
    typedef CL_Obj (*native_fn0_t)(void);
    (void)nargs;
    if (bc == NULL || bc->native_code == NULL) return CL_NIL;
    jit_invoke_count++;
    return ((native_fn0_t)bc->native_code)();
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

    if (bc->native_code) platform_free(bc->native_code);
    bc->native_code = code;
    bc->native_len  = len;
    platform_cache_clear(code, len);
    return 1;
}

#endif /* JIT_M68K */
