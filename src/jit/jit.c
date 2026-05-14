/* jit.c — orchestration for the m68k template JIT.
 *
 * Two codegen paths layered front-to-back:
 *
 *   - Whole-function pattern matchers.  Recognize a small set of
 *     known-tight shapes and emit hand-written templates of optimal
 *     size (4–8 bytes for the body).  Today: 0-arg constant return
 *     (OP_NIL/OP_T/OP_CONST → moveq or move.l#imm32 + rts) and 1..k-arg
 *     parameter pass-through (move.l offset(sp),d0 + rts).
 *
 *   - Per-opcode walker.  Falls through when no matcher fires.  Walks
 *     the bytecode once and emits one m68k template per opcode using
 *     the m68k hardware stack as the operand stack and a LINK'd frame
 *     at A6 for locals.  Bails (leaves native_code NULL) the first
 *     time it sees an opcode it doesn't yet handle; the function then
 *     runs through the bytecode interpreter exactly as before.
 *
 *     Currently supported opcodes: OP_NIL, OP_T, OP_CONST, OP_LOAD,
 *     OP_STORE, OP_POP, OP_DUP, OP_JMP, OP_JNIL, OP_JTRUE, OP_ADD,
 *     OP_SUB, OP_EQ, OP_LT, OP_GT, OP_LE, OP_GE, OP_NUMEQ, OP_NOT,
 *     OP_RET.  Adding an opcode means adding one emitter case and one
 *     or two new asm encoders — the walker handles the composition.
 *
 * See specs/native-backend.md §"Per-opcode emitter shape".
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
            "; [jit] m68k template backend: matchers (0-arg literal, 1..6-arg pass-through) + per-opcode walker\n");
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

/* --- Per-opcode walker ---------------------------------------------------
 *
 * The walker emits a complete native function for any bytecode built
 * from the small but growing set of opcodes below.  It uses A6 as a
 * frame pointer (set up via LINK) and the m68k hardware stack (A7) as
 * the operand stack.
 *
 * Stack frame, after `link a6,#-N`:
 *
 *   8(a6) + 4*i  parameter i (i in [0, arity)) — placed there by the
 *                m68k C ABI before the JSR
 *   4(a6)        return address (pushed by JSR)
 *   0(a6)        saved A6 (pushed by LINK)
 *  -4(a6) - 4*j  "extra" local j (j = slot - arity, j in [0, n_extra))
 *   (a7)         operand-stack TOS, grows downward as opcodes push
 *
 * For bytecode slot s, slot_disp() picks the right displacement.
 *
 * Branches are resolved in a single pass with a small forward-patch
 * list.  `bc_to_native[ip]` records the native-code offset at which the
 * opcode beginning at bytecode IP `ip` starts emitting.  Backward
 * branches read directly from this map; forward branches emit a Bcc.W
 * with placeholder 0 and remember (patch_off, target_bc_off) for later
 * fix-up.  Once the walker reaches OP_RET we resolve every pending
 * patch.  If a 16-bit displacement won't fit we bail out — the function
 * runs interpreted instead.  (32-bit Bcc.L exists on 68020+ but isn't
 * needed at the function sizes the walker handles today.)
 *
 * On any unsupported opcode the walker returns 0 and the caller drops
 * the partially-built buffer — the function then runs interpreted.
 */

/* Forward branch waiting for its target's native offset.  The branch
 * opcode word lives at (patch_off - 2); the 16-bit displacement field
 * the patcher overwrites lives at patch_off.  The displacement is
 * computed relative to patch_off itself, since m68k Bcc.W references
 * PC = (instruction_start + 2) = patch_off. */
typedef struct {
    uint32_t patch_off;      /* native byte offset of the disp word */
    uint32_t target_bc_off;  /* bytecode offset of the branch target */
} BranchPatch;

static int16_t slot_disp(uint8_t slot, uint16_t arity)
{
    if (slot < arity) {
        return (int16_t)(8 + 4 * (int)slot);
    }
    return (int16_t)(-4 * ((int)slot - (int)arity + 1));
}

/* Decode a 4-byte big-endian signed integer from the bytecode stream.
 * Matches the encoding cl_emit_i32 produces for branch operands. */
static int32_t read_i32_be(const uint8_t *p)
{
    return (int32_t)(((uint32_t)p[0] << 24) |
                     ((uint32_t)p[1] << 16) |
                     ((uint32_t)p[2] <<  8) |
                      (uint32_t)p[3]);
}

/* Patch list grows on demand starting from this size.  A pessimistic
 * upper bound is code_len / 5 (every byte a branch instruction), but
 * realistic Lisp rarely needs more than a dozen forward branches per
 * function.  Doubling on overflow keeps amortised cost O(1). */
#define WALKER_PATCH_INITIAL_CAP 8

/* Emit a 2-operand arithmetic template (OP_ADD / OP_SUB) — pops b/a
 * into D1/D0, runs the inline fixnum fast path, falls through to a JSR
 * helper for non-fixnums or fixnum overflow.  `is_sub == 0` produces
 * an ADD; `is_sub == 1` produces a SUB.
 *
 * Tag accounting: both operands carry bit-0 tag = 1.  For ADD the raw
 * tagged sum carries bit-0 = 0 with one surplus tag, so SUBQ #1
 * restores `... | 1`.  For SUB the raw tagged difference cancels both
 * tags, so ADDQ #1 sets bit-0 back.
 *
 * Overflow recovery (BVS taken): D0 holds the 32-bit-wrapped result.
 * For ADD, original a = wrapped - b, so SUB d1,d0; for SUB, original
 * a = wrapped + b, so ADD d1,d0.  Either way D0 ends up with a, D1
 * still holds b, and we fall into the common slow-path JSR.
 *
 * Caller-saved discipline: D0/D1/A0/A1 only — see OP_ADD's comment. */
static void emit_arith(CodeBuf *cb, int is_sub, uint32_t helper)
{
    int32_t beq_a_pc, beq_b_pc, bvs_pc, bra_pc;
    int32_t slow_off, done_off;

    m68k_emit_move_l_postinc_an_to_dn(cb, REG_A7, REG_D1);
    m68k_emit_move_l_postinc_an_to_dn(cb, REG_A7, REG_D0);

    m68k_emit_btst_imm_dn(cb, 0, REG_D0);
    beq_a_pc = (int32_t)cb_len(cb) + 2;
    m68k_emit_beq_w(cb, 0);
    m68k_emit_btst_imm_dn(cb, 0, REG_D1);
    beq_b_pc = (int32_t)cb_len(cb) + 2;
    m68k_emit_beq_w(cb, 0);

    if (is_sub) m68k_emit_sub_l_dn_to_dm(cb, REG_D1, REG_D0);
    else        m68k_emit_add_l_dn_to_dm(cb, REG_D1, REG_D0);
    bvs_pc = (int32_t)cb_len(cb) + 2;
    m68k_emit_bvs_w(cb, 0);

    if (is_sub) m68k_emit_addq_l_dn(cb, 1, REG_D0);  /* tag was cancelled */
    else        m68k_emit_subq_l_dn(cb, 1, REG_D0);  /* strip surplus tag */
    m68k_emit_move_l_dn_predec_an(cb, REG_D0, REG_A7);
    bra_pc = (int32_t)cb_len(cb) + 2;
    m68k_emit_bra_w(cb, 0);

    /* Overflow-recovery shim: reconstruct original a in D0. */
    {
        int32_t overflow_off = (int32_t)cb_len(cb);
        if (is_sub) m68k_emit_add_l_dn_to_dm(cb, REG_D1, REG_D0);
        else        m68k_emit_sub_l_dn_to_dm(cb, REG_D1, REG_D0);
        m68k_patch_disp16(cb_data(cb), cb_len(cb),
                          (uint32_t)bvs_pc,
                          (int16_t)(overflow_off - bvs_pc));
    }

    slow_off = (int32_t)cb_len(cb);
    m68k_emit_move_l_dn_predec_an(cb, REG_D1, REG_A7);
    m68k_emit_move_l_dn_predec_an(cb, REG_D0, REG_A7);
    m68k_emit_jsr_abs_l(cb, helper);
    m68k_emit_addq_l_an(cb, 8, REG_A7);
    m68k_emit_move_l_dn_predec_an(cb, REG_D0, REG_A7);
    done_off = (int32_t)cb_len(cb);

    m68k_patch_disp16(cb_data(cb), cb_len(cb), (uint32_t)beq_a_pc,
                      (int16_t)(slow_off - beq_a_pc));
    m68k_patch_disp16(cb_data(cb), cb_len(cb), (uint32_t)beq_b_pc,
                      (int16_t)(slow_off - beq_b_pc));
    m68k_patch_disp16(cb_data(cb), cb_len(cb), (uint32_t)bra_pc,
                      (int16_t)(done_off - bra_pc));
}

/* Emit a 2-operand comparison template (OP_LT / OP_GT / OP_LE /
 * OP_GE / OP_NUMEQ) — pops b/a, fixnum fast path, slow-path JSR for
 * non-fixnums.  Always pushes CL_T or CL_NIL.  `cond_code` is the
 * m68k Bcc condition that branches to push_t (e.g. 13=BLT, 14=BGT,
 * 12=BGE, 15=BLE, 7=BEQ).  `helper` returns CL_T/CL_NIL directly. */
static void emit_compare(CodeBuf *cb, uint8_t cond_code, uint32_t helper)
{
    int32_t beq_a_pc, beq_b_pc, taken_pc, bra1_pc, bra2_pc;
    int32_t slow_off, done_off, push_t_off;

    m68k_emit_move_l_postinc_an_to_dn(cb, REG_A7, REG_D1);
    m68k_emit_move_l_postinc_an_to_dn(cb, REG_A7, REG_D0);

    m68k_emit_btst_imm_dn(cb, 0, REG_D0);
    beq_a_pc = (int32_t)cb_len(cb) + 2;
    m68k_emit_beq_w(cb, 0);
    m68k_emit_btst_imm_dn(cb, 0, REG_D1);
    beq_b_pc = (int32_t)cb_len(cb) + 2;
    m68k_emit_beq_w(cb, 0);

    m68k_emit_cmp_l_dn_dm(cb, REG_D1, REG_D0);  /* flags = a - b */
    taken_pc = (int32_t)cb_len(cb) + 2;
    m68k_emit_bcc_w(cb, cond_code, 0);
    m68k_emit_clr_l_predec(cb, REG_A7);          /* push NIL */
    bra1_pc = (int32_t)cb_len(cb) + 2;
    m68k_emit_bra_w(cb, 0);

    push_t_off = (int32_t)cb_len(cb);
    m68k_emit_move_l_imm32_predec(cb, (uint32_t)CL_T, REG_A7);
    bra2_pc = (int32_t)cb_len(cb) + 2;
    m68k_emit_bra_w(cb, 0);

    slow_off = (int32_t)cb_len(cb);
    m68k_emit_move_l_dn_predec_an(cb, REG_D1, REG_A7);
    m68k_emit_move_l_dn_predec_an(cb, REG_D0, REG_A7);
    m68k_emit_jsr_abs_l(cb, helper);
    m68k_emit_addq_l_an(cb, 8, REG_A7);
    m68k_emit_move_l_dn_predec_an(cb, REG_D0, REG_A7);
    done_off = (int32_t)cb_len(cb);

    m68k_patch_disp16(cb_data(cb), cb_len(cb), (uint32_t)beq_a_pc,
                      (int16_t)(slow_off  - beq_a_pc));
    m68k_patch_disp16(cb_data(cb), cb_len(cb), (uint32_t)beq_b_pc,
                      (int16_t)(slow_off  - beq_b_pc));
    m68k_patch_disp16(cb_data(cb), cb_len(cb), (uint32_t)taken_pc,
                      (int16_t)(push_t_off - taken_pc));
    m68k_patch_disp16(cb_data(cb), cb_len(cb), (uint32_t)bra1_pc,
                      (int16_t)(done_off  - bra1_pc));
    m68k_patch_disp16(cb_data(cb), cb_len(cb), (uint32_t)bra2_pc,
                      (int16_t)(done_off  - bra2_pc));
}

static int patches_push(BranchPatch **patches, uint32_t *n, uint32_t *cap,
                        uint32_t patch_off, uint32_t target_bc_off)
{
    if (*n == *cap) {
        uint32_t new_cap = (*cap == 0) ? WALKER_PATCH_INITIAL_CAP : (*cap * 2);
        BranchPatch *grown = (BranchPatch *)platform_alloc(
            (unsigned long)(new_cap * sizeof(BranchPatch)));
        if (grown == NULL) return 0;
        if (*patches != NULL) {
            uint32_t i;
            for (i = 0; i < *n; i++) grown[i] = (*patches)[i];
            platform_free(*patches);
        }
        *patches = grown;
        *cap = new_cap;
    }
    (*patches)[*n].patch_off     = patch_off;
    (*patches)[*n].target_bc_off = target_bc_off;
    (*n)++;
    return 1;
}

static int walker_compile(const CL_Bytecode *bc, CodeBuf *cb)
{
    uint16_t arity;
    uint16_t n_locals;
    uint32_t n_extra;
    uint32_t ip;
    uint32_t i;
    int16_t frame_size;
    int32_t *bc_to_native = NULL;
    BranchPatch *patches = NULL;
    uint32_t n_patches = 0;
    uint32_t cap_patches = 0;
    int result = 0;

    /* Conservative gate: same metadata constraints as the matchers.
     * &optional/&key/&rest/&aux/upvalues all need work that hasn't
     * landed yet.  Arity bounded by the dispatch-switch cap so a
     * walker-emitted function can actually be invoked. */
    if (bc->arity & 0x8000)  return 0;
    if (bc->n_optional != 0) return 0;
    if (bc->flags != 0)      return 0;
    if (bc->n_keys != 0)     return 0;
    if (bc->n_upvalues != 0) return 0;

    arity = (uint16_t)(bc->arity & 0x7FFF);
    if (arity > CL_JIT_PASSTHROUGH_MAX_ARITY) return 0;

    n_locals = bc->n_locals;
    if (n_locals < arity) return 0;
    n_extra = (uint32_t)(n_locals - arity);
    /* LINK takes a 16-bit signed disp; cap extra locals so 4*n_extra
     * stays within range with headroom.  In practice n_locals never
     * approaches this for hand-written Lisp. */
    if (n_extra > 1000) return 0;
    frame_size = (int16_t)(-4 * (int32_t)n_extra);

    /* bc_to_native[i] = native offset where the opcode beginning at
     * bytecode position i starts emitting, or -1 if that position is
     * mid-operand / not yet reached.  Indexed up to code_len inclusive
     * so a branch whose target equals code_len (one past the last
     * byte) is detectable as out-of-range. */
    bc_to_native = (int32_t *)platform_alloc(
        (unsigned long)((bc->code_len + 1) * sizeof(int32_t)));
    if (bc_to_native == NULL) return 0;
    for (i = 0; i <= bc->code_len; i++) bc_to_native[i] = -1;

    /* Prologue: save A6, set A6 = new SP, allocate frame for extras. */
    m68k_emit_link_an_disp16(cb, REG_A6, frame_size);

    ip = 0;
    while (ip < bc->code_len) {
        uint8_t op;
        bc_to_native[ip] = (int32_t)cb_len(cb);
        op = bc->code[ip++];
        switch (op) {
        case OP_NIL:
            /* CL_NIL == 0 — clear longword at -(a7). */
            m68k_emit_clr_l_predec(cb, REG_A7);
            break;

        case OP_T:
            m68k_emit_move_l_imm32_predec(cb, (uint32_t)CL_T, REG_A7);
            break;

        case OP_CONST: {
            uint16_t idx;
            CL_Obj val;
            if (ip + 1 >= bc->code_len) goto fail;
            idx = ((uint16_t)bc->code[ip] << 8) | bc->code[ip + 1];
            ip += 2;
            if (idx >= bc->n_constants || bc->constants == NULL) goto fail;
            val = bc->constants[idx];
            m68k_emit_move_l_imm32_predec(cb, (uint32_t)val, REG_A7);
            break;
        }

        case OP_LOAD: {
            uint8_t slot;
            int16_t disp;
            if (ip >= bc->code_len) goto fail;
            slot = bc->code[ip++];
            if (slot >= n_locals) goto fail;
            disp = slot_disp(slot, arity);
            m68k_emit_move_l_disp_an_predec_am(cb, disp, REG_A6, REG_A7);
            break;
        }

        case OP_STORE: {
            uint8_t slot;
            int16_t disp;
            if (ip >= bc->code_len) goto fail;
            slot = bc->code[ip++];
            if (slot >= n_locals) goto fail;
            disp = slot_disp(slot, arity);
            /* STORE writes TOS without popping; the matching OP_POP
             * (if any) pops separately. */
            m68k_emit_move_l_an_to_disp_am(cb, REG_A7, disp, REG_A6);
            break;
        }

        case OP_POP:
            m68k_emit_addq_l_an(cb, 4, REG_A7);
            break;

        case OP_DUP:
            /* Copy TOS onto the operand stack without disturbing the
             * existing slot.  m68k semantics: src is read before dst's
             * predecrement applies, so move.l (a7),-(a7) duplicates. */
            m68k_emit_move_l_an_to_predec_am(cb, REG_A7, REG_A7);
            break;

        case OP_ADD:
            emit_arith(cb, /*is_sub=*/0,
                       (uint32_t)(uintptr_t)&cl_jit_runtime_add);
            break;

        case OP_SUB:
            emit_arith(cb, /*is_sub=*/1,
                       (uint32_t)(uintptr_t)&cl_jit_runtime_sub);
            break;

        case OP_LT:
            emit_compare(cb, /*BLT*/13,
                         (uint32_t)(uintptr_t)&cl_jit_runtime_lt);
            break;

        case OP_GT:
            emit_compare(cb, /*BGT*/14,
                         (uint32_t)(uintptr_t)&cl_jit_runtime_gt);
            break;

        case OP_LE:
            emit_compare(cb, /*BLE*/15,
                         (uint32_t)(uintptr_t)&cl_jit_runtime_le);
            break;

        case OP_GE:
            emit_compare(cb, /*BGE*/12,
                         (uint32_t)(uintptr_t)&cl_jit_runtime_ge);
            break;

        case OP_NUMEQ:
            emit_compare(cb, /*BEQ*/7,
                         (uint32_t)(uintptr_t)&cl_jit_runtime_numeq);
            break;

        case OP_EQ: {
            /* Pop b, a; push CL_T iff a == b (pointer compare), else
             * CL_NIL.  No slow path — CL_EQ is pure object identity.
             * Mirrors VM's OP_EQ semantics. */
            int32_t beq_pc, bra_pc;
            int32_t push_t_off, done_off;

            m68k_emit_move_l_postinc_an_to_dn(cb, REG_A7, REG_D1);
            m68k_emit_move_l_postinc_an_to_dn(cb, REG_A7, REG_D0);
            m68k_emit_cmp_l_dn_dm(cb, REG_D1, REG_D0);
            beq_pc = (int32_t)cb_len(cb) + 2;
            m68k_emit_beq_w(cb, 0);
            m68k_emit_clr_l_predec(cb, REG_A7);          /* push NIL */
            bra_pc = (int32_t)cb_len(cb) + 2;
            m68k_emit_bra_w(cb, 0);

            push_t_off = (int32_t)cb_len(cb);
            m68k_emit_move_l_imm32_predec(cb, (uint32_t)CL_T, REG_A7);
            done_off = (int32_t)cb_len(cb);

            m68k_patch_disp16(cb_data(cb), cb_len(cb), (uint32_t)beq_pc,
                              (int16_t)(push_t_off - beq_pc));
            m68k_patch_disp16(cb_data(cb), cb_len(cb), (uint32_t)bra_pc,
                              (int16_t)(done_off - bra_pc));
            break;
        }

        case OP_NOT: {
            /* Pop value; push CL_T iff CL_NULL_P (value == 0), else
             * CL_NIL.  MOVE.L (a7)+,d0 sets Z based on the popped
             * value — no separate TST needed.  No slow path. */
            int32_t beq_pc, bra_pc;
            int32_t push_t_off, done_off;

            m68k_emit_move_l_postinc_an_to_dn(cb, REG_A7, REG_D0);
            beq_pc = (int32_t)cb_len(cb) + 2;
            m68k_emit_beq_w(cb, 0);
            m68k_emit_clr_l_predec(cb, REG_A7);          /* push NIL */
            bra_pc = (int32_t)cb_len(cb) + 2;
            m68k_emit_bra_w(cb, 0);

            push_t_off = (int32_t)cb_len(cb);
            m68k_emit_move_l_imm32_predec(cb, (uint32_t)CL_T, REG_A7);
            done_off = (int32_t)cb_len(cb);

            m68k_patch_disp16(cb_data(cb), cb_len(cb), (uint32_t)beq_pc,
                              (int16_t)(push_t_off - beq_pc));
            m68k_patch_disp16(cb_data(cb), cb_len(cb), (uint32_t)bra_pc,
                              (int16_t)(done_off - bra_pc));
            break;
        }

        case OP_JMP:
        case OP_JNIL:
        case OP_JTRUE: {
            /* All three branches share a 4-byte i32 displacement
             * relative to the bytecode position *after* the operand
             * (matching read_i32 / cl_emit_jump semantics).
             *
             * For JNIL/JTRUE, the VM pops the test value before
             * branching.  We mirror that by popping TOS into D0 via
             * postinc; m68k MOVE sets Z based on the source value, so
             * BEQ/BNE then test "is the popped value CL_NIL (= 0)".
             * For JMP no pop is needed. */
            int32_t offset;
            uint32_t target_bc_off;
            uint32_t patch_off;
            if (ip + 4 > bc->code_len) goto fail;
            offset = read_i32_be(bc->code + ip);
            ip += 4;
            /* Target is reached *after* the operand bytes are consumed —
             * matches VM's `ip += offset` where ip already passed the
             * 4-byte read. */
            if (offset < 0) {
                uint32_t neg = (uint32_t)(-offset);
                if (neg > ip) goto fail;       /* underflow */
                target_bc_off = ip - neg;
            } else {
                target_bc_off = ip + (uint32_t)offset;
            }
            if (target_bc_off > bc->code_len) goto fail;

            if (op == OP_JNIL || op == OP_JTRUE) {
                m68k_emit_move_l_postinc_an_to_dn(cb, REG_A7, REG_D0);
            }

            patch_off = cb_len(cb) + 2;  /* disp field of Bcc.W */

            if (target_bc_off <= ip - 5) {
                /* Backward (or self) branch — target already emitted.
                 * `ip - 5` is the position of *this* branch opcode, so
                 * any target ≤ that is in the past.  bc_to_native
                 * lookup is authoritative. */
                int32_t target_native = bc_to_native[target_bc_off];
                int32_t disp32;
                if (target_native < 0) goto fail;
                disp32 = target_native - (int32_t)patch_off;
                if (disp32 < -32768 || disp32 > 32767) goto fail;
                switch (op) {
                case OP_JMP:   m68k_emit_bra_w(cb, (int16_t)disp32); break;
                case OP_JNIL:  m68k_emit_beq_w(cb, (int16_t)disp32); break;
                case OP_JTRUE: m68k_emit_bne_w(cb, (int16_t)disp32); break;
                }
            } else {
                /* Forward branch — record the patch site and emit a
                 * placeholder. */
                if (!patches_push(&patches, &n_patches, &cap_patches,
                                  patch_off, target_bc_off)) goto fail;
                switch (op) {
                case OP_JMP:   m68k_emit_bra_w(cb, 0); break;
                case OP_JNIL:  m68k_emit_beq_w(cb, 0); break;
                case OP_JTRUE: m68k_emit_bne_w(cb, 0); break;
                }
            }
            break;
        }

        case OP_RET:
            /* Pop result into D0, restore A6, return.  Bytecode is
             * required to end with OP_RET (cl_compile_defun emits it as
             * the function's last byte), so we resolve forward
             * branches and exit here. */
            bc_to_native[ip] = (int32_t)cb_len(cb);  /* fall-through target */
            m68k_emit_move_l_postinc_an_to_dn(cb, REG_A7, REG_D0);
            m68k_emit_unlk_an(cb, REG_A6);
            m68k_emit_rts(cb);

            /* Resolve every forward branch's displacement against the
             * map.  Any branch whose target wasn't reached during the
             * walk (target_bc_off without a bc_to_native entry) is a
             * bug — bail rather than emit a garbage displacement. */
            for (i = 0; i < n_patches; i++) {
                BranchPatch *p = &patches[i];
                int32_t target_native;
                int32_t disp32;
                if (p->target_bc_off > bc->code_len) goto fail;
                target_native = bc_to_native[p->target_bc_off];
                if (target_native < 0) goto fail;
                disp32 = target_native - (int32_t)p->patch_off;
                if (disp32 < -32768 || disp32 > 32767) goto fail;
                m68k_patch_disp16(cb_data(cb), cb_len(cb),
                                  p->patch_off, (int16_t)disp32);
            }
            result = 1;
            goto cleanup;

        default:
            /* Unsupported opcode → bail.  cb's buffer is discarded by
             * the caller. */
            goto fail;
        }
    }
    /* Falling off the end without OP_RET = malformed bytecode. */

fail:
    result = 0;
cleanup:
    if (bc_to_native) platform_free(bc_to_native);
    if (patches)      platform_free(patches);
    return result;
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
    } else if (walker_compile(bc, &cb)) {
        /* Walker handled it — full per-opcode template emission with
         * LINK frame + m68k-stack operand stack.  Larger code than the
         * matchers' tight templates, but covers any shape built from
         * the supported opcodes. */
    } else {
        /* Walker bailed (unsupported opcode, oversized frame, etc.) —
         * discard whatever it emitted into cb and fall back to the
         * bytecode interpreter for this function. */
        cb_free(&cb);
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

/* --- Disassembler -----------------------------------------------------
 *
 * Decodes the m68k instructions the JIT can emit and prints one line of
 * assembly per instruction to platform_write_string.  Not a general
 * m68k disassembler — only the ~13 instruction forms the matchers and
 * walker use.  Anything else falls through to ".word $xxxx" so the user
 * still sees the raw word.
 *
 * Exposed to Lisp as `clamiga::%JIT-DISASSEMBLE`; the `jitexpand` macro
 * in lib/boot.lisp wraps the common case.
 */

/* Decode one effective-address mode/reg pair, advancing *pos past any
 * extension words.  size_l selects between 32-bit (immediate .L) and
 * 16-bit immediates — only matters for mode=7/reg=4.  Returns 0 on
 * success, -1 if the EA is one the JIT never emits (so we don't have to
 * cover the full m68k EA space here). */
static int disasm_ea(int mode, int reg, const uint8_t *code, uint32_t len,
                     uint32_t *pos, int size_l, char *buf, int bufsize)
{
    switch (mode) {
    case 0: snprintf(buf, (size_t)bufsize, "d%d", reg); return 0;
    case 1: snprintf(buf, (size_t)bufsize, "a%d", reg); return 0;
    case 2: snprintf(buf, (size_t)bufsize, "(a%d)", reg); return 0;
    case 3: snprintf(buf, (size_t)bufsize, "(a%d)+", reg); return 0;
    case 4: snprintf(buf, (size_t)bufsize, "-(a%d)", reg); return 0;
    case 5: {
        int16_t d;
        if (*pos + 2 > len) return -1;
        d = (int16_t)(((uint16_t)code[*pos] << 8) | code[*pos + 1]);
        *pos += 2;
        snprintf(buf, (size_t)bufsize, "%d(a%d)", (int)d, reg);
        return 0;
    }
    case 7:
        if (reg == 4) {
            uint32_t v;
            if (size_l) {
                if (*pos + 4 > len) return -1;
                v = ((uint32_t)code[*pos]     << 24)
                  | ((uint32_t)code[*pos + 1] << 16)
                  | ((uint32_t)code[*pos + 2] << 8)
                  |  (uint32_t)code[*pos + 3];
                *pos += 4;
            } else {
                if (*pos + 2 > len) return -1;
                v = ((uint32_t)code[*pos] << 8) | code[*pos + 1];
                *pos += 2;
            }
            snprintf(buf, (size_t)bufsize, "#$%08lx", (unsigned long)v);
            return 0;
        }
        return -1;
    default:
        return -1;
    }
}

/* Decode one instruction at code+offset.  Returns its length in bytes
 * (>=2), or 0 if the buffer ran out before the operand words could be
 * read.  On unknown opcodes returns 2 with mnemonic ".word $xxxx" so
 * the caller still advances. */
static uint32_t disasm_one(const uint8_t *code, uint32_t len,
                           uint32_t offset, char *mnemonic, int msize)
{
    uint16_t op;
    uint32_t pos;
    int matched = 0;

    if (offset + 2 > len) return 0;
    op = (uint16_t)(((uint16_t)code[offset] << 8) | code[offset + 1]);
    pos = offset + 2;

    if (op == 0x4E71) {
        snprintf(mnemonic, (size_t)msize, "nop"); matched = 1;
    } else if (op == 0x4E75) {
        snprintf(mnemonic, (size_t)msize, "rts"); matched = 1;
    } else if ((op & 0xFFF8) == 0x4E50) {           /* LINK An,#d16 */
        int an = op & 7;
        int16_t d;
        if (pos + 2 > len) return 0;
        d = (int16_t)(((uint16_t)code[pos] << 8) | code[pos + 1]);
        pos += 2;
        snprintf(mnemonic, (size_t)msize, "link a%d,#%d", an, (int)d);
        matched = 1;
    } else if ((op & 0xFFF8) == 0x4E58) {           /* UNLK An */
        int an = op & 7;
        snprintf(mnemonic, (size_t)msize, "unlk a%d", an);
        matched = 1;
    } else if ((op & 0xFFF8) == 0x42A0) {           /* CLR.L -(An) */
        int an = op & 7;
        snprintf(mnemonic, (size_t)msize, "clr.l -(a%d)", an);
        matched = 1;
    } else if ((op & 0xF100) == 0x7000) {           /* MOVEQ #imm,Dn */
        int dn = (op >> 9) & 7;
        int8_t imm = (int8_t)(op & 0xFF);
        snprintf(mnemonic, (size_t)msize, "moveq #%d,d%d", (int)imm, dn);
        matched = 1;
    } else if ((op & 0xF1F8) == 0x5088) {           /* ADDQ.L #data,An */
        int data = (op >> 9) & 7;
        int an = op & 7;
        if (data == 0) data = 8;
        snprintf(mnemonic, (size_t)msize, "addq.l #%d,a%d", data, an);
        matched = 1;
    } else if ((op & 0xF0FF) == 0x6000) {           /* Bcc.W (disp=0 in opcode) */
        /* Bcc with 8-bit disp field == 0 means a 16-bit displacement
         * word follows, relative to PC = (instr_start + 2).  Condition
         * is in bits 11-8: 0=BRA, 6=BNE, 7=BEQ, 9=BVS, 13=BLT.  Other
         * conditions decode as bXX.w with the numeric condition for
         * readability. */
        static const char *cc_names[16] = {
            "bra", "bsr", "bhi", "bls", "bcc", "bcs", "bne", "beq",
            "bvc", "bvs", "bpl", "bmi", "bge", "blt", "bgt", "ble"
        };
        int16_t d;
        int cond = (op >> 8) & 0xF;
        long target;
        if (pos + 2 > len) return 0;
        d = (int16_t)(((uint16_t)code[pos] << 8) | code[pos + 1]);
        pos += 2;
        target = (long)offset + 2L + (long)d;
        snprintf(mnemonic, (size_t)msize, "%s.w %ld",
                 cc_names[cond], target);
        matched = 1;
    } else if (op == 0x4EB9) {                      /* JSR (xxx).L */
        uint32_t addr;
        if (pos + 4 > len) return 0;
        addr = ((uint32_t)code[pos]     << 24)
             | ((uint32_t)code[pos + 1] << 16)
             | ((uint32_t)code[pos + 2] <<  8)
             |  (uint32_t)code[pos + 3];
        pos += 4;
        snprintf(mnemonic, (size_t)msize, "jsr $%08lx", (unsigned long)addr);
        matched = 1;
    } else if ((op & 0xFFC0) == 0x0800) {           /* BTST #imm,<ea> (static) */
        /* Two-word form: 0000 1000 00 mmm rrr + 16-bit imm word.  We
         * only emit BTST #imm,Dn (mode 000). */
        int mode = (op >> 3) & 7;
        int reg  = op & 7;
        uint16_t imm_word;
        if (pos + 2 > len) return 0;
        imm_word = (uint16_t)(((uint16_t)code[pos] << 8) | code[pos + 1]);
        pos += 2;
        if (mode == 0) {
            snprintf(mnemonic, (size_t)msize, "btst #%u,d%d",
                     (unsigned)(imm_word & 0x1F), reg);
        } else {
            snprintf(mnemonic, (size_t)msize, "btst #%u,<ea>",
                     (unsigned)(imm_word & 0x1F));
        }
        matched = 1;
    } else if ((op & 0xF000) == 0xC000
            && ((op >> 6) & 7) == 2
            && ((op >> 3) & 7) == 0) {              /* AND.L Dn,Dm */
        int dm = (op >> 9) & 7;
        int dn = op & 7;
        snprintf(mnemonic, (size_t)msize, "and.l d%d,d%d", dn, dm);
        matched = 1;
    } else if ((op & 0xF000) == 0xD000
            && ((op >> 6) & 7) == 2
            && ((op >> 3) & 7) == 0) {              /* ADD.L Dn,Dm */
        int dm = (op >> 9) & 7;
        int dn = op & 7;
        snprintf(mnemonic, (size_t)msize, "add.l d%d,d%d", dn, dm);
        matched = 1;
    } else if ((op & 0xF000) == 0x9000
            && ((op >> 6) & 7) == 2
            && ((op >> 3) & 7) == 0) {              /* SUB.L Dn,Dm */
        int dm = (op >> 9) & 7;
        int dn = op & 7;
        snprintf(mnemonic, (size_t)msize, "sub.l d%d,d%d", dn, dm);
        matched = 1;
    } else if ((op & 0xF000) == 0xB000
            && ((op >> 6) & 7) == 2
            && ((op >> 3) & 7) == 0) {              /* CMP.L Dn,Dm */
        int dm = (op >> 9) & 7;
        int dn = op & 7;
        snprintf(mnemonic, (size_t)msize, "cmp.l d%d,d%d", dn, dm);
        matched = 1;
    } else if ((op & 0xF1C0) == 0x5080) {           /* ADDQ.L #imm,Dn */
        int data = (op >> 9) & 7;
        int dn = op & 7;
        if (data == 0) data = 8;
        snprintf(mnemonic, (size_t)msize, "addq.l #%d,d%d", data, dn);
        matched = 1;
    } else if ((op & 0xF1C0) == 0x5180) {           /* SUBQ.L #imm,Dn */
        int data = (op >> 9) & 7;
        int dn = op & 7;
        if (data == 0) data = 8;
        snprintf(mnemonic, (size_t)msize, "subq.l #%d,d%d", data, dn);
        matched = 1;
    } else if ((op & 0xF000) == 0x2000) {           /* MOVE.L src,dst */
        int dst_reg = (op >> 9) & 7;
        int dst_mode = (op >> 6) & 7;
        int src_mode = (op >> 3) & 7;
        int src_reg = op & 7;
        char src[40], dst[40];
        if (disasm_ea(src_mode, src_reg, code, len, &pos, 1,
                      src, sizeof src) < 0 ||
            disasm_ea(dst_mode, dst_reg, code, len, &pos, 1,
                      dst, sizeof dst) < 0) {
            snprintf(mnemonic, (size_t)msize, ".word $%04x", op);
            pos = offset + 2;  /* roll back; we'll only consume the opcode */
        } else {
            snprintf(mnemonic, (size_t)msize, "move.l %s,%s", src, dst);
        }
        matched = 1;
    }

    if (!matched) {
        snprintf(mnemonic, (size_t)msize, ".word $%04x", op);
    }
    return pos - offset;
}

void cl_jit_disassemble(const uint8_t *code, uint32_t len)
{
    uint32_t off = 0;
    char mnem[80];
    char line[160];
    while (off < len) {
        uint32_t n = disasm_one(code, len, off, mnem, sizeof mnem);
        char hex[40];
        char *hp = hex;
        uint32_t i;
        if (n == 0) {
            snprintf(line, sizeof line, "  %04lu: <decode short> end\n",
                     (unsigned long)off);
            platform_write_string(line);
            return;
        }
        for (i = 0; i < n && (hp - hex) + 4 < (int)sizeof hex; i++) {
            int written = snprintf(hp, sizeof hex - (size_t)(hp - hex),
                                   "%s%02X", i == 0 ? "" : " ",
                                   code[off + i]);
            if (written < 0) break;
            hp += written;
        }
        snprintf(line, sizeof line, "  %04lu: %-18s %s\n",
                 (unsigned long)off, hex, mnem);
        platform_write_string(line);
        off += n;
    }
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

void cl_jit_set_active(int active) { jit_active = active ? 1 : 0; }

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
