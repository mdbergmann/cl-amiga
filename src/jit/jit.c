/* jit.c — orchestration for the m68k template JIT.
 *
 * Current codegen covers two function-shape families:
 *
 *   1. `(defun f () <literal>)` — 0-arg, returns a constant.  Emitted
 *      as `moveq` or `move.l #imm32` into D0 followed by `rts`.
 *      Recognized literals: OP_NIL, OP_T, OP_CONST.
 *
 *   2. `(defun f (x1..xk) xj)` — k-arg parameter pass-through, k up to
 *      CL_JIT_PASSTHROUGH_MAX_ARITY.  Emitted as
 *      `move.l (4+4*j)(sp),d0 ; rts`, reading the C-ABI arg slot
 *      directly.  Subsumes the 1-arg identity case.
 *
 * Everything else still leaves `bc->native_code == NULL` and runs
 * through the bytecode interpreter.
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
#include "core/thread.h"   /* cl_vm.stack[sp - nargs ... sp - 1] are the args */
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
            "; [jit] m68k template backend: trivial-literal-leaf + 1..6-arg pass-through\n");
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

/* Match `(defun f (x1..xk) xj)` — k-arg parameter pass-through.
 *
 * Body `xj` compiles to `OP_LOAD <j>`; the implicit block-return
 * postlude adds `OP_STORE <k> ; OP_POP ; OP_LOAD <k>` (slot k is the
 * block-return cell since slots 0..k-1 hold the k parameters), and the
 * function trailer adds `OP_RET`.  Total 8 bytes regardless of k:
 *
 *   OP_LOAD  j   2 bytes   (0 <= j < k)
 *   OP_STORE k   2 bytes
 *   OP_POP       1 byte
 *   OP_LOAD  k   2 bytes
 *   OP_RET       1 byte
 *
 * No allocation, no side effects — safe to collapse to a single "load
 * arg, return" sequence.  Strict on metadata so optional/&key/&rest
 * variants don't sneak through.
 *
 * Capped at the arity that `cl_jit_invoke` knows how to dispatch
 * (`CL_JIT_PASSTHROUGH_MAX_ARITY` — bump in lockstep with the switch).
 *
 * Returns 1 and stores the source slot j in *slot_out on match. */
#define CL_JIT_PASSTHROUGH_MAX_ARITY 6

static int matches_passthrough(const CL_Bytecode *bc, uint8_t *slot_out)
{
    uint8_t arity, slot;

    if (bc->arity < 1 || bc->arity > CL_JIT_PASSTHROUGH_MAX_ARITY) return 0;
    if (bc->n_optional != 0) return 0;
    if (bc->flags != 0) return 0;
    if (bc->n_keys != 0) return 0;
    if (bc->n_upvalues != 0) return 0;
    arity = (uint8_t)bc->arity;
    if (bc->n_locals != (uint16_t)(arity + 1)) return 0;
    if (bc->code_len != 8) return 0;

    if (bc->code[0] != OP_LOAD)  return 0;
    slot = bc->code[1];
    if (slot >= arity) return 0;
    if (bc->code[2] != OP_STORE) return 0;
    if (bc->code[3] != arity)    return 0;
    if (bc->code[4] != OP_POP)   return 0;
    if (bc->code[5] != OP_LOAD)  return 0;
    if (bc->code[6] != arity)    return 0;
    if (bc->code[7] != OP_RET)   return 0;

    *slot_out = slot;
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
    uint8_t slot;

    if (bc == NULL || !jit_active) return;
    bc->native_code = NULL;
    bc->native_len  = 0;

    cb_init(&cb, 8);

    if (matches_trivial_leaf(bc, &value)) {
        /* Emit: load <value> into D0 ; rts.  m68k SysV ABI returns the
         * function result in D0, so the load + rts is a complete leaf
         * function callable from C with no register save/restore (the
         * load only touches D0, which is caller-saved). */
        emit_load_imm_d0(&cb, value);
        m68k_emit_rts(&cb);
    } else if (matches_passthrough(bc, &slot)) {
        /* Emit: move.l (4 + 4*slot)(sp),d0 ; rts.  The C ABI on m68k
         * lays args out starting at 4(A7) after JSR pushes the return
         * address, each 32-bit slot bumping by 4.  cl_jit_invoke casts
         * native_code to the matching arity's C function-pointer type
         * and passes args through normal calling convention, so reading
         * slot j is just a fixed displacement load. */
        int16_t disp = (int16_t)(4 + 4 * (int)slot);
        m68k_emit_move_l_disp_an_to_dn(&cb, disp, REG_A7, REG_D0);
        m68k_emit_rts(&cb);
    } else {
        return;
    }

    code = cb_finish(&cb, &len);
    if (code == NULL) return;

    bc->native_code = code;
    bc->native_len  = len;

    /* Flush I-cache so the freshly written bytes aren't stale on
     * 68040/060.  On 68020/030 this is a no-op (no write-back I-cache). */
    platform_cache_clear(code, len);
}

/* Enter native code.  Dispatches by `nargs` to the matching C function
 * pointer cast — the JIT only compiles shapes whose arity is fixed and
 * matches one of the cases here (matchers reject optional/&key/&rest),
 * and OP_CALL has already verified nargs == bc->arity before reaching
 * us, so the cast is sound.
 *
 * Args are read straight off `cl_vm.stack[sp - nargs ... sp - 1]` and
 * passed via the normal m68k C calling convention; emitted code reads
 * them from `4(sp)`, `8(sp)`, etc.  Caller is responsible for popping
 * the args and function object after we return.
 *
 * m68k SysV ABI: D0 holds the return value on RTS; callee preserves
 * D2..D7 and A2..A6.  The current templates clobber only D0, which is
 * caller-saved, so no register save/restore is needed on either side.
 *
 * Unknown arities fall back to CL_NIL — that shouldn't happen because
 * cl_jit_compile gatekeeps which shapes get native_code, but keep the
 * defensive branch so a future matcher mismatch surfaces as a wrong
 * value rather than a wild jump. */
CL_Obj cl_jit_invoke(CL_Bytecode *bc, int nargs)
{
    if (bc == NULL || bc->native_code == NULL) return CL_NIL;
    jit_invoke_count++;

    switch (nargs) {
    case 0: {
        typedef CL_Obj (*native_fn0_t)(void);
        return ((native_fn0_t)bc->native_code)();
    }
    case 1: {
        typedef CL_Obj (*native_fn1_t)(CL_Obj);
        CL_Obj a0 = cl_vm.stack[cl_vm.sp - 1];
        return ((native_fn1_t)bc->native_code)(a0);
    }
    case 2: {
        typedef CL_Obj (*native_fn2_t)(CL_Obj, CL_Obj);
        CL_Obj a0 = cl_vm.stack[cl_vm.sp - 2];
        CL_Obj a1 = cl_vm.stack[cl_vm.sp - 1];
        return ((native_fn2_t)bc->native_code)(a0, a1);
    }
    case 3: {
        typedef CL_Obj (*native_fn3_t)(CL_Obj, CL_Obj, CL_Obj);
        CL_Obj a0 = cl_vm.stack[cl_vm.sp - 3];
        CL_Obj a1 = cl_vm.stack[cl_vm.sp - 2];
        CL_Obj a2 = cl_vm.stack[cl_vm.sp - 1];
        return ((native_fn3_t)bc->native_code)(a0, a1, a2);
    }
    case 4: {
        typedef CL_Obj (*native_fn4_t)(CL_Obj, CL_Obj, CL_Obj, CL_Obj);
        CL_Obj a0 = cl_vm.stack[cl_vm.sp - 4];
        CL_Obj a1 = cl_vm.stack[cl_vm.sp - 3];
        CL_Obj a2 = cl_vm.stack[cl_vm.sp - 2];
        CL_Obj a3 = cl_vm.stack[cl_vm.sp - 1];
        return ((native_fn4_t)bc->native_code)(a0, a1, a2, a3);
    }
    case 5: {
        typedef CL_Obj (*native_fn5_t)(CL_Obj, CL_Obj, CL_Obj, CL_Obj, CL_Obj);
        CL_Obj a0 = cl_vm.stack[cl_vm.sp - 5];
        CL_Obj a1 = cl_vm.stack[cl_vm.sp - 4];
        CL_Obj a2 = cl_vm.stack[cl_vm.sp - 3];
        CL_Obj a3 = cl_vm.stack[cl_vm.sp - 2];
        CL_Obj a4 = cl_vm.stack[cl_vm.sp - 1];
        return ((native_fn5_t)bc->native_code)(a0, a1, a2, a3, a4);
    }
    case 6: {
        typedef CL_Obj (*native_fn6_t)(CL_Obj, CL_Obj, CL_Obj, CL_Obj, CL_Obj, CL_Obj);
        CL_Obj a0 = cl_vm.stack[cl_vm.sp - 6];
        CL_Obj a1 = cl_vm.stack[cl_vm.sp - 5];
        CL_Obj a2 = cl_vm.stack[cl_vm.sp - 4];
        CL_Obj a3 = cl_vm.stack[cl_vm.sp - 3];
        CL_Obj a4 = cl_vm.stack[cl_vm.sp - 2];
        CL_Obj a5 = cl_vm.stack[cl_vm.sp - 1];
        return ((native_fn6_t)bc->native_code)(a0, a1, a2, a3, a4, a5);
    }
    default:
        return CL_NIL;
    }
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
