/* jit.c — orchestration for the m68k template JIT.
 *
 * Current codegen: a single "trivial leaf returning a literal" pattern
 * — `(defun f () <literal>)` — emitted as `moveq` or `move.l #imm32`
 * followed by `rts`.  Recognized literal forms are OP_NIL, OP_T, and
 * OP_CONST (any constant pool entry); everything else still leaves
 * `bc->native_code == NULL` and runs through the bytecode interpreter.
 *
 * See specs/native-backend.md §"Suggested staging".
 */

#ifdef JIT_M68K

#include <string.h>

#include "jit/jit.h"
#include "jit/codebuf.h"
#include "jit/asm_m68k.h"
#include "jit/codegen_m68k.h"
#include "jit/runtime.h"
#include "core/opcodes.h"
#include "core/types.h"
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
            "; [jit] m68k template backend: trivial-literal-leaf only\n");
    }
}

/* Match `(defun f () <literal>)` and extract the returned CL_Obj.
 *
 * The compiler emits a fixed 6-byte epilogue for any zero-arg lambda
 * whose body is a single value-producing form:
 *
 *   <push-value>     1 byte  (OP_NIL / OP_T)
 *                or  3 bytes (OP_CONST u16)
 *   OP_STORE 0       2 bytes
 *   OP_POP           1 byte
 *   OP_LOAD  0       2 bytes
 *   OP_RET           1 byte
 *
 * Local 0 is the implicit block return-value slot.  Body has no side
 * effects beyond producing the value, which is what makes it safe to
 * replace with a single immediate-load.
 *
 * Returns 1 and stores the literal value in *value_out on a match.
 * Stays strict on every metadata field so a near-match (different
 * arity, locals, &rest, etc.) never silently lands wrong code.
 */
static int matches_trivial_leaf(const CL_Bytecode *bc, CL_Obj *value_out)
{
    static const uint8_t epilogue[6] = {
        OP_STORE, 0x00,
        OP_POP,
        OP_LOAD,  0x00,
        OP_RET
    };
    uint32_t prefix_len;
    uint8_t op;

    if (bc->arity != 0) return 0;
    if (bc->n_optional != 0) return 0;
    if (bc->flags != 0) return 0;      /* no &key, no allow-other-keys */
    if (bc->n_keys != 0) return 0;
    if (bc->n_upvalues != 0) return 0;
    if (bc->n_locals != 1) return 0;
    if (bc->code_len < 7) return 0;

    op = bc->code[0];
    if (op == OP_NIL || op == OP_T) {
        prefix_len = 1;
    } else if (op == OP_CONST) {
        prefix_len = 3;
    } else {
        return 0;
    }
    if (bc->code_len != prefix_len + 6) return 0;
    if (memcmp(bc->code + prefix_len, epilogue, 6) != 0) return 0;

    if (op == OP_NIL) {
        *value_out = CL_NIL;
    } else if (op == OP_T) {
        *value_out = CL_T;
    } else {
        /* OP_CONST <u16 index, big-endian> */
        uint16_t idx = ((uint16_t)bc->code[1] << 8) | bc->code[2];
        if (idx >= bc->n_constants || bc->constants == NULL) return 0;
        *value_out = bc->constants[idx];
    }
    return 1;
}

/* Pick the shortest m68k encoding that materializes `val` in D0.
 * MOVEQ sign-extends an 8-bit immediate, so a CL_Obj whose 32-bit
 * value lies in [-128, 127] (interpreted as int32) round-trips
 * through it exactly.  Everything else needs the full 6-byte
 * MOVE.L #imm32. */
static void emit_load_imm_d0(CodeBuf *cb, CL_Obj val)
{
    int32_t sv = (int32_t)val;
    if (sv >= -128 && sv <= 127) {
        m68k_emit_moveq(cb, (int8_t)sv, REG_D0);
    } else {
        m68k_emit_move_l_imm32(cb, (uint32_t)val, REG_D0);
    }
}

void cl_jit_compile(CL_Bytecode *bc)
{
    CodeBuf cb;
    uint8_t *code;
    uint32_t len;
    CL_Obj value;

    if (bc == NULL || !jit_active) return;
    bc->native_code = NULL;
    bc->native_len  = 0;

    if (!matches_trivial_leaf(bc, &value)) return;

    /* Emit: load <value> into D0 ; rts.  m68k SysV ABI returns the
     * function result in D0, so the load + rts is a complete leaf
     * function callable from C with no register save/restore (the
     * load only touches D0, which is caller-saved). */
    cb_init(&cb, 8);
    emit_load_imm_d0(&cb, value);
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
