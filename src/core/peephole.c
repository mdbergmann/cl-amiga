/*
 * Bytecode peephole post-pass (spec 1.8) — gated on (optimize (speed >= 2)).
 *
 * Runs over a function's finished bytecode after the single emit pass, as a
 * decode -> rewrite -> re-encode pipeline:
 *
 *   1. DECODE the flat byte buffer into an instruction list, resolving every
 *      jump/NLX-landing offset to an instruction INDEX and marking each
 *      branch target as a basic-block boundary.
 *   2. REWRITE patterns on the instruction list (never across a boundary):
 *        - jump-to-jump threading      JMP/JNIL/JTRUE -> JMP t   ==> direct t
 *        - dead code                   unreachable after JMP/RET/... removed
 *        - store-then-reload           STORE n; POP; LOAD n      ==> STORE n
 *        - pure-push + pop elision     CONST/NIL/T/LOAD/DUP/UPVAL; POP ==> -
 *        - not-branch fusion           NOT; JNIL t               ==> JTRUE t
 *   3. RE-ENCODE, recomputing all relative offsets from scratch and
 *      remapping the pc-keyed source line table.
 *
 * FAIL-SAFE BY DESIGN: any byte the decoder does not recognize — an opcode
 * missing from CL_OPCODE_LIST, a jump landing between instructions, an
 * OP_CLOSURE whose template constant is not bytecode, an over-budget
 * function, an allocation failure — aborts the pass and leaves the original
 * bytecode untouched.  A new opcode someone forgets to classify costs a
 * missed optimization, never a miscompile.
 *
 * SEMANTIC GUARDS:
 *   - Only CL_OPF_PURE opcodes may be deleted with their OP_POP: they push
 *     exactly one value and cannot signal.  OP_CAR/arith/etc. must keep
 *     signaling type errors at any speed (ANSI: the error is observable even
 *     when the value is discarded).
 *   - cl_mv_count: CONST/NIL/T/UPVAL write it; LOAD/DUP/POP do not.  Deleting
 *     a writing pair is only allowed when a scan forward inside the basic
 *     block reaches another unconditional mv-writer before anything that
 *     observes mv state (MV_LOAD/MV_TO_LIST/NTH_VALUE/RET/CALL/...), so the
 *     deleted write is provably masked.  Same scan gates dropping OP_NOT in
 *     the not-branch fusion; when the scan can't prove it, the fusion keeps
 *     the mv write by substituting OP_MV_RESET (same 1-byte length).
 *   - NLX landing pads (CATCH/UWPROT/BLOCK_PUSH/TAGBODY_PUSH offsets) are
 *     treated exactly like jump targets; their offsets are re-encoded, but
 *     they are never threaded.
 *   - Backward-jump GC safepoints survive: offsets are recomputed from final
 *     positions, so a real loop's back edge stays a backward jump.
 *
 * The pass allocates NO Lisp objects — it cannot trigger GC, so raw
 * CL_Bytecode pointers read from the constant pool (OP_CLOSURE templates)
 * cannot go stale while it runs.
 *
 * Debug: build with -DDEBUG_PEEPHOLE for per-function rewrite statistics.
 */

#include <string.h>
#ifdef DEBUG_PEEPHOLE
#include <stdio.h>
#endif

#include "types.h"
#include "opcodes.h"
#include "peephole.h"
#include "compiler.h"
#include "compiler_internal.h"
#include "../platform/platform.h"

/* --- Opcode info table (single source of truth: CL_OPCODE_LIST) --- */

static const CL_OpcodeInfo cl_opcode_table[256] = {
#define CL_OPCODE_TABLE_ENTRY(name, value, str, opnd, fl) \
    [value] = { str, (uint8_t)(opnd), (uint8_t)(fl) },
    CL_OPCODE_LIST(CL_OPCODE_TABLE_ENTRY)
#undef CL_OPCODE_TABLE_ENTRY
};

const CL_OpcodeInfo *cl_opcode_info(uint8_t op)
{
    const CL_OpcodeInfo *info = &cl_opcode_table[op];
    return info->name ? info : NULL;
}

/* --- Instruction list --- */

#define PEEP_DELETED 0x01   /* instruction removed; jumps into it retarget forward */
#define PEEP_TARGET  0x02   /* branch/NLX-landing target: basic-block boundary */

typedef struct {
    uint32_t old_pos;     /* byte offset of the opcode in the input buffer */
    uint32_t new_pos;     /* byte offset in the output buffer (re-encode) */
    const uint8_t *raw;   /* points at the opcode byte in the input buffer */
    uint16_t raw_len;     /* total instruction length in bytes */
    int32_t jump_target;  /* instruction index for JREL/U16_JREL, else -1 */
    uint8_t op;           /* current opcode (patterns may rewrite it) */
    uint8_t flags;        /* PEEP_* */
} PeepInsn;

typedef struct {
    PeepInsn *insns;
    int32_t count;
    const CL_Obj *constants;
    int32_t n_constants;
} PeepCode;

/* Instruction byte length for re-encoding.  Everything keeps its decoded
 * length except opcode substitutions, which are all 1-byte <-> 1-byte. */
static uint32_t peep_insn_len(const PeepInsn *in)
{
    return in->raw_len;
}

/* Binary search: instruction index whose old_pos == pos, or -1. */
static int32_t peep_index_of_pos(const PeepCode *pc, uint32_t pos)
{
    int32_t lo = 0, hi = pc->count - 1;
    while (lo <= hi) {
        int32_t mid = lo + (hi - lo) / 2;
        if (pc->insns[mid].old_pos == pos) return mid;
        if (pc->insns[mid].old_pos < pos) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static uint16_t peep_read_u16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static int32_t peep_read_i32(const uint8_t *p)
{
    return (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                     ((uint32_t)p[2] << 8) | (uint32_t)p[3]);
}

static void peep_write_i32(uint8_t *p, int32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

/* --- Decode --- */

/* Decode CODE[0..LEN) into PC->insns.  Returns 1 on success, 0 to bail out
 * (unknown opcode, truncated operands, undecodable OP_CLOSURE). */
static int peep_decode(PeepCode *pc, const uint8_t *code, uint32_t len)
{
    uint32_t ip = 0;
    int32_t n = 0;

    while (ip < len) {
        const CL_OpcodeInfo *info = cl_opcode_info(code[ip]);
        PeepInsn *in = &pc->insns[n];
        uint32_t opnd_len;

        if (!info) return 0;  /* unknown opcode — bail */

        switch ((CL_OperandKind)info->operands) {
        case CL_OPND_NONE:     opnd_len = 0; break;
        case CL_OPND_U8:       opnd_len = 1; break;
        case CL_OPND_U16:      opnd_len = 2; break;
        case CL_OPND_JREL:     opnd_len = 4; break;
        case CL_OPND_U16_JREL: opnd_len = 6; break;
        case CL_OPND_U16_U16:  opnd_len = 4; break;
        case CL_OPND_AMIGA:    opnd_len = 9; break;
        case CL_OPND_CLOSURE: {
            /* u16 template const index + 2 bytes per template upvalue.
             * Needs the constant pool; anything unexpected -> bail. */
            uint16_t idx;
            CL_Obj tmpl;
            const CL_Bytecode *tmpl_bc;
            if (ip + 3 > len) return 0;
            idx = peep_read_u16(code + ip + 1);
            if (idx >= pc->n_constants || !pc->constants) return 0;
            tmpl = pc->constants[idx];
            if (!CL_BYTECODE_P(tmpl)) return 0;
            tmpl_bc = (const CL_Bytecode *)CL_OBJ_TO_PTR(tmpl);
            opnd_len = 2 + 2u * tmpl_bc->n_upvalues;
            break;
        }
        default: return 0;
        }

        if (ip + 1 + opnd_len > len) return 0;  /* truncated — bail */

        in->old_pos = ip;
        in->new_pos = 0;
        in->raw = code + ip;
        in->raw_len = (uint16_t)(1 + opnd_len);
        in->jump_target = -1;
        in->op = code[ip];
        in->flags = 0;
        ip += 1 + opnd_len;
        n++;
    }

    pc->count = n;

    /* Resolve jump/NLX-landing offsets to instruction indices. */
    {
        int32_t i;
        for (i = 0; i < pc->count; i++) {
            PeepInsn *in = &pc->insns[i];
            const CL_OpcodeInfo *info = cl_opcode_info(in->op);
            int32_t off;
            uint32_t tpos;
            int32_t tidx;

            if (info->operands == CL_OPND_JREL)
                off = peep_read_i32(in->raw + 1);
            else if (info->operands == CL_OPND_U16_JREL)
                off = peep_read_i32(in->raw + 3);
            else
                continue;

            /* Offsets are relative to the first byte after the instruction. */
            tpos = in->old_pos + in->raw_len + (uint32_t)off;
            if (tpos > len) return 0;
            if (tpos == len) {
                /* Jump to end-of-code: legal only as "fall off the end"
                 * target; be conservative and bail (compiler never emits
                 * it — a final RET/HALT always exists). */
                return 0;
            }
            tidx = peep_index_of_pos(pc, tpos);
            if (tidx < 0) return 0;  /* lands mid-instruction — bail */
            in->jump_target = tidx;
        }
    }
    return 1;
}

/* Recompute PEEP_TARGET flags from all live jumps/NLX landings. */
static void peep_mark_targets(PeepCode *pc)
{
    int32_t i;
    for (i = 0; i < pc->count; i++)
        pc->insns[i].flags &= (uint8_t)~PEEP_TARGET;
    for (i = 0; i < pc->count; i++) {
        PeepInsn *in = &pc->insns[i];
        if (in->flags & PEEP_DELETED) continue;
        if (in->jump_target >= 0)
            pc->insns[in->jump_target].flags |= PEEP_TARGET;
    }
}

/* --- mv-state scan guard ---
 *
 * May the mv_count write of a deleted/dropped instruction be observed?
 * Scan straight-line code forward from FROM: if we reach an unconditional
 * mv-writer first, the write is masked (return 1 = provably dead).
 * Anything that observes mv state, branches, or ends the function means we
 * can't prove it (return 0).  Join points (PEEP_TARGET) are handled
 * conservatively: a straight-line scan stays sound across a join, but we
 * fail there anyway to keep the reasoning local — except at the scan's
 * entry instruction when ALLOW_ENTRY_TARGET is set (used for scanning a
 * branch's own landing point).  The TARGET flag is honored even on deleted
 * instructions: a jump that landed on a deleted pattern-pair retargets to
 * the following live instruction, which therefore sits on a join. */
static int peep_mv_write_is_dead(const PeepCode *pc, int32_t from,
                                 int allow_entry_target)
{
    int32_t i;
    int entry = 1;
    for (i = from; i < pc->count; i++) {
        const PeepInsn *in = &pc->insns[i];
        const CL_OpcodeInfo *info;
        if ((in->flags & PEEP_TARGET) && !(entry && allow_entry_target))
            return 0;                            /* join point */
        entry = 0;
        if (in->flags & PEEP_DELETED) continue;
        info = cl_opcode_info(in->op);
        if (info->flags & CL_OPF_MVR) return 0;  /* observer first — bail */
        if (info->flags & CL_OPF_MVW) return 1;  /* masked — provably dead */
        if (info->flags & CL_OPF_UNCOND) return 0;
        if (in->jump_target >= 0) return 0;      /* conditional branch */
    }
    return 0;
}

/* Next non-deleted instruction index after I, or pc->count. */
static int32_t peep_next_live(const PeepCode *pc, int32_t i)
{
    i++;
    while (i < pc->count && (pc->insns[i].flags & PEEP_DELETED)) i++;
    return i;
}

/* Like peep_next_live, but additionally reports whether the step crossed a
 * branch target — either a skipped deleted instruction carrying PEEP_TARGET
 * (a jump retargets through it onto the result) or the resulting live
 * instruction itself.  Multi-instruction patterns must reject such steps:
 * a jump landing INSIDE a pattern would execute only its tail, so the
 * members are not equivalent to a straight-line pair. */
static int32_t peep_next_live_unjoined(const PeepCode *pc, int32_t i,
                                       int *crossed_target)
{
    *crossed_target = 0;
    i++;
    while (i < pc->count && (pc->insns[i].flags & PEEP_DELETED)) {
        if (pc->insns[i].flags & PEEP_TARGET) *crossed_target = 1;
        i++;
    }
    if (i < pc->count && (pc->insns[i].flags & PEEP_TARGET))
        *crossed_target = 1;
    return i;
}

/* --- Rewrite patterns (each returns number of rewrites applied) --- */

/* Jump-to-jump threading: a JMP/JNIL/JTRUE whose target is an OP_JMP jumps
 * directly to that JMP's target.  NLX landing offsets are never threaded.
 * Chain chase is capped to stay clear of jump cycles. */
static int peep_thread_jumps(PeepCode *pc)
{
    int changed = 0;
    int32_t i;
    for (i = 0; i < pc->count; i++) {
        PeepInsn *in = &pc->insns[i];
        int hops;
        if (in->flags & PEEP_DELETED) continue;
        if (in->op != OP_JMP && in->op != OP_JNIL && in->op != OP_JTRUE)
            continue;
        if (in->jump_target < 0) continue;
        for (hops = 0; hops < 16; hops++) {
            int32_t t = in->jump_target;
            PeepInsn *ti = &pc->insns[t];
            if (ti->flags & PEEP_DELETED) break;
            if (ti->op != OP_JMP || ti->jump_target < 0) break;
            if (ti->jump_target == in->jump_target) break;  /* self-loop */
            in->jump_target = ti->jump_target;
            changed++;
        }
    }
    return changed;
}

/* Dead code: instructions that can only be reached by falling through an
 * UNCOND instruction (JMP/RET/TAILCALL/HALT/BLOCK_RETURN/TAGBODY_GO) and are
 * not branch targets.  Requires peep_mark_targets to be current. */
static int peep_dead_code(PeepCode *pc)
{
    int changed = 0;
    int reachable = 1;
    int32_t i;
    for (i = 0; i < pc->count; i++) {
        PeepInsn *in = &pc->insns[i];
        const CL_OpcodeInfo *info;
        if (in->flags & PEEP_TARGET) reachable = 1;
        if (in->flags & PEEP_DELETED) continue;
        if (!reachable) {
            in->flags |= PEEP_DELETED;
            in->jump_target = -1;
            changed++;
            continue;
        }
        info = cl_opcode_info(in->op);
        if (info->flags & CL_OPF_UNCOND) reachable = 0;
    }
    return changed;
}

/* STORE n; POP; LOAD n  ==>  STORE n
 * OP_STORE peeks (locals[n] = TOS without popping), so the reloaded value is
 * exactly the value STORE left on top — POP+LOAD is a no-op round trip.
 * POP and LOAD write no mv state, so no mv guard is needed. */
static int peep_store_reload(PeepCode *pc)
{
    int changed = 0;
    int32_t i;
    for (i = 0; i < pc->count; i++) {
        PeepInsn *s = &pc->insns[i];
        int32_t pi, li;
        int joined;
        PeepInsn *p, *l;
        if (s->flags & PEEP_DELETED) continue;
        if (s->op != OP_STORE) continue;
        pi = peep_next_live_unjoined(pc, i, &joined);
        if (pi >= pc->count || joined) continue;
        p = &pc->insns[pi];
        if (p->op != OP_POP) continue;
        li = peep_next_live_unjoined(pc, pi, &joined);
        if (li >= pc->count || joined) continue;
        l = &pc->insns[li];
        if (l->op != OP_LOAD) continue;
        if (s->raw[1] != l->raw[1]) continue;  /* different slot */
        p->flags |= PEEP_DELETED;
        l->flags |= PEEP_DELETED;
        changed++;
    }
    return changed;
}

/* <pure-push>; POP  ==>  (nothing)
 * Only CL_OPF_PURE opcodes qualify (no side effects, cannot signal).  When
 * the push writes mv state (CONST/NIL/T/UPVAL), deletion additionally
 * requires the mv-scan proof; LOAD/DUP write no mv state and always
 * qualify. */
static int peep_pure_pop(PeepCode *pc)
{
    int changed = 0;
    int32_t i;
    for (i = 0; i < pc->count; i++) {
        PeepInsn *u = &pc->insns[i];
        const CL_OpcodeInfo *info;
        int32_t pi;
        int joined;
        PeepInsn *p;
        if (u->flags & PEEP_DELETED) continue;
        info = cl_opcode_info(u->op);
        if (!(info->flags & CL_OPF_PURE)) continue;
        pi = peep_next_live_unjoined(pc, i, &joined);
        if (pi >= pc->count || joined) continue;
        p = &pc->insns[pi];
        if (p->op != OP_POP) continue;
        if ((info->flags & CL_OPF_MVW) &&
            !peep_mv_write_is_dead(pc, peep_next_live(pc, pi), 0))
            continue;
        u->flags |= PEEP_DELETED;
        p->flags |= PEEP_DELETED;
        changed++;
    }
    return changed;
}

/* NOT; JNIL t  ==>  JTRUE t     (and NOT; JTRUE t ==> JNIL t)
 * OP_NOT pops v and pushes its negation; the following conditional pops that
 * negation — fusing inverts the branch sense on the original value.  OP_NOT
 * also writes cl_mv_count: the write must be provably dead on BOTH
 * successor paths (fallthrough and branch target) to drop it outright;
 * otherwise it is preserved by substituting OP_MV_RESET (1 byte, same as
 * OP_NOT), which still removes the negate + pop/push round trip. */
static int peep_not_branch(PeepCode *pc)
{
    int changed = 0;
    int32_t i;
    for (i = 0; i < pc->count; i++) {
        PeepInsn *n = &pc->insns[i];
        int32_t ji;
        int joined;
        PeepInsn *j;
        if (n->flags & PEEP_DELETED) continue;
        if (n->op != OP_NOT) continue;
        ji = peep_next_live_unjoined(pc, i, &joined);
        if (ji >= pc->count || joined) continue;
        j = &pc->insns[ji];
        if (j->op != OP_JNIL && j->op != OP_JTRUE) continue;
        j->op = (j->op == OP_JNIL) ? OP_JTRUE : OP_JNIL;
        if (peep_mv_write_is_dead(pc, peep_next_live(pc, ji), 0) &&
            j->jump_target >= 0 &&
            peep_mv_write_is_dead(pc, j->jump_target, 1)) {
            n->flags |= PEEP_DELETED;
        } else {
            n->op = OP_MV_RESET;  /* keep the mv_count=1 write, drop the negate */
        }
        changed++;
    }
    return changed;
}

/* --- Re-encode --- */

/* Assign new positions, emit into OUT (size >= input length), rewrite the
 * pc-keyed line map in place.  Returns the new code length. */
static uint32_t peep_encode(PeepCode *pc, uint8_t *out,
                            CL_LineEntry *lines, int *n_lines)
{
    uint32_t pos = 0;
    int32_t i;

    /* Pass 1: assign new positions to live instructions. */
    for (i = 0; i < pc->count; i++) {
        PeepInsn *in = &pc->insns[i];
        if (in->flags & PEEP_DELETED) continue;
        in->new_pos = pos;
        pos += peep_insn_len(in);
    }

    /* Pass 2: emit bytes; jump offsets recomputed from new positions.  A
     * jump whose target instruction was deleted retargets to the next live
     * instruction (deleting a jump target is only ever done for pattern
     * pairs that are semantically no-ops on every path through them). */
    for (i = 0; i < pc->count; i++) {
        PeepInsn *in = &pc->insns[i];
        const CL_OpcodeInfo *info;
        uint8_t *dst;
        if (in->flags & PEEP_DELETED) continue;
        info = cl_opcode_info(in->op);
        dst = out + in->new_pos;
        dst[0] = in->op;
        if (in->raw_len > 1)
            memcpy(dst + 1, in->raw + 1, (size_t)(in->raw_len - 1));
        if (in->jump_target >= 0) {
            int32_t t = in->jump_target;
            uint32_t tpos;
            int32_t off;
            while (t < pc->count && (pc->insns[t].flags & PEEP_DELETED)) t++;
            /* The function's final RET/HALT is never deleted while a live
             * jump references past it (it is reachable via that jump), so a
             * live target always exists. */
            tpos = pc->insns[t].new_pos;
            off = (int32_t)tpos - (int32_t)(in->new_pos + in->raw_len);
            if (info->operands == CL_OPND_JREL)
                peep_write_i32(dst + 1, off);
            else /* CL_OPND_U16_JREL */
                peep_write_i32(dst + 3, off);
        }
    }

    /* Remap the source line table: each entry's pc moves to the new position
     * of the first live instruction at old_pos >= pc; entries that collapse
     * onto the same new pc keep only the first. */
    if (lines && n_lines && *n_lines > 0) {
        int out_n = 0;
        int li;
        int32_t last_pc = -1;
        for (li = 0; li < *n_lines; li++) {
            uint32_t want = lines[li].pc;
            int32_t idx;
            /* first instruction at old_pos >= want */
            {
                int32_t lo = 0, hi = pc->count;
                while (lo < hi) {
                    int32_t mid = lo + (hi - lo) / 2;
                    if (pc->insns[mid].old_pos < want) lo = mid + 1;
                    else hi = mid;
                }
                idx = lo;
            }
            while (idx < pc->count && (pc->insns[idx].flags & PEEP_DELETED))
                idx++;
            if (idx >= pc->count) continue;  /* line mapped to deleted tail */
            if ((int32_t)pc->insns[idx].new_pos == last_pc) continue;
            last_pc = (int32_t)pc->insns[idx].new_pos;
            lines[out_n].pc = (uint16_t)pc->insns[idx].new_pos;
            lines[out_n].line = lines[li].line;
            out_n++;
        }
        *n_lines = out_n;
    }

    return pos;
}

/* --- Driver --- */

int cl_peephole_run(uint8_t *code, int *code_len,
                    const CL_Obj *constants, int n_constants,
                    CL_LineEntry *lines, int *n_lines)
{
    PeepCode pc;
    uint8_t *out;
    uint32_t len = (uint32_t)*code_len;
    uint32_t new_len;
    int rounds;
    int total = 0;

    if (*code_len <= 0 || (uint32_t)*code_len > CL_PEEPHOLE_MAX_CODE)
        return 0;

    pc.insns = (PeepInsn *)platform_alloc(len * sizeof(PeepInsn));
    if (!pc.insns) return 0;  /* OOM — skip optimization, never fail */
    pc.count = 0;
    pc.constants = constants;
    pc.n_constants = (int32_t)n_constants;

    if (!peep_decode(&pc, code, len)) {
        platform_free(pc.insns);
        return 0;
    }

    /* Fixpoint: deleting a pair can expose a new pair (LOAD a; LOAD b; POP;
     * POP).  Targets are recomputed each round because deleted jumps drop
     * their target marks.  Bounded — each round must delete/rewrite at
     * least one instruction to continue. */
    for (rounds = 0; rounds < 8; rounds++) {
        int changed = 0;
        peep_mark_targets(&pc);
        changed += peep_thread_jumps(&pc);
        peep_mark_targets(&pc);  /* threading moves target marks */
        changed += peep_dead_code(&pc);
        changed += peep_store_reload(&pc);
        changed += peep_pure_pop(&pc);
        changed += peep_not_branch(&pc);
        total += changed;
        if (!changed) break;
    }

    if (!total) {
        platform_free(pc.insns);
        return 0;
    }

    /* Validate: every live jump must retarget (forward past deletions) to a
     * live instruction.  The invariant "a targeted region always ends in a
     * live RET/HALT" makes failure impossible; if it ever breaks, bail out
     * rather than emit a corrupt offset. */
    {
        int32_t i;
        for (i = 0; i < pc.count; i++) {
            PeepInsn *in = &pc.insns[i];
            int32_t t;
            if (in->flags & PEEP_DELETED) continue;
            if (in->jump_target < 0) continue;
            t = in->jump_target;
            while (t < pc.count && (pc.insns[t].flags & PEEP_DELETED)) t++;
            if (t >= pc.count) {
                platform_free(pc.insns);
                return 0;
            }
        }
    }

    out = (uint8_t *)platform_alloc(len);
    if (!out) {
        platform_free(pc.insns);
        return 0;
    }

    new_len = peep_encode(&pc, out, lines, n_lines);
    memcpy(code, out, new_len);
    *code_len = (int)new_len;

#ifdef DEBUG_PEEPHOLE
    fprintf(stderr, "[peephole] %d rewrites, %u -> %u bytes\n",
            total, (unsigned)len, (unsigned)new_len);
#endif

    platform_free(out);
    platform_free(pc.insns);
    return 1;
}

void cl_peephole_optimize(struct CL_Compiler_s *c)
{
    if (c->peep_speed_max < 2) return;
    cl_peephole_run(c->code, &c->code_pos, c->constants, c->const_count,
                    c->line_entries, &c->line_entry_count);
}
