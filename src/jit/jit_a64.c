/* jit_a64.c — the AArch64 native backend (specs/native-backend-a64.md).
 *
 * Phase 1: a walker that cannot be wrong.  It runs the operand stack,
 * the locals and control flow natively and sends every other opcode to
 * the C helper behind it -- a shared one (runtime.c) where its semantics
 * are the interpreter's, else a VM-stack one (runtime_vmstack.c).
 * No inline fast paths and no register cache yet (phase 2); the opcodes
 * the walker does not know (the NLX frames, closures and upvalues,
 * dynamic binding, multiple values, &key/&optional/&rest prologues) keep
 * the function interpreted.
 *
 * Entry ABI (the spec's "Entry and frame layout"): one C signature for
 * every arity, the arguments left on cl_vm.stack where the caller pushed
 * them, so no trampoline is needed:
 *
 *     CL_Obj entry(CL_Thread *thr, CL_Obj *bp, uint32_t nargs, CL_Obj func)
 *
 * The frame is the interpreter's, in cl_vm.stack:
 *
 *     bp[0 .. arity-1]         the arguments
 *     bp[arity .. n_locals-1]  the other locals, NIL on entry
 *     bp[n_locals]             FUNC, the function value the frame was
 *                              entered with (OP_UPVAL's source in phase 3;
 *                              the self tail call's guard reads it)
 *     bp[n_locals+1 ..]        the operand stack
 *
 * Registers (callee-saved, so they survive every helper call):
 *     x19  the current CL_Thread *          x20  the operand-stack top
 *     x21  bp                               x22  bc->constants
 *     x27  cl_vm.stack (the thread's)       x28  scratch across a
 *                                                non-allocating call
 * x9-x12 and x16/x17 are scratch; x18 (reserved on Apple) is never used.
 *
 * GC ("Rule 1"): before every helper call the code writes cl_vm.sp from
 * x20, so every live value is below sp -- a root both collectors forward in
 * place -- and holds no CL_Obj in a register across the call.  Constants
 * are read from bc->constants at each use (that array never moves and the
 * collectors forward it, minor collections included), T through the
 * address of the CL_T global.  So native code bakes no heap object: no
 * relocation table, nothing for a collection to patch.  The backend never
 * sets jit_depth / jit_stack_top -- the conservative native-stack scan of
 * the m68k JIT does not apply.
 */

#include "jit/jit.h"

#ifdef JIT_A64

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "jit/jit_backend.h"
#include "jit/asm_a64.h"
#include "jit/codebuf.h"
#include "jit/codeheap.h"
#include "jit/runtime.h"
#include "jit/runtime_vmstack.h"
#include "core/mem.h"        /* cl_call_gen_bump */
#include "core/opcodes.h"
#include "core/stream.h"     /* cl_write_cstring_to_stdout */
#include "core/thread.h"     /* cl_vm, cl_get_current_thread, CL_Thread */
#include "core/vm.h"         /* cl_vm_apply_list */
#include "platform/platform.h"
#include "platform/platform_thread.h"   /* platform_memory_barrier */

typedef CL_Obj (*a64_entry_t)(CL_Thread *thr, CL_Obj *bp, uint32_t nargs,
                              CL_Obj func);

/* Register roles (see the banner). */
#define R_THR   19
#define R_TOP   20
#define R_BP    21
#define R_K     22
#define R_STK   27
#define R_KEEP  28

/* CL_Thread fields the code reads through x19.  Each must fit the scaled
 * unsigned-offset form of its access size. */
#define OFF_VM_STACK      ((uint32_t)offsetof(CL_Thread, vm.stack))
#define OFF_VM_STACK_SIZE ((uint32_t)offsetof(CL_Thread, vm.stack_size))
#define OFF_VM_SP         ((uint32_t)offsetof(CL_Thread, vm.sp))
#define OFF_MV_COUNT      ((uint32_t)offsetof(CL_Thread, mv_count))
#define OFF_LOOP_CTR      ((uint32_t)offsetof(CL_Thread, jit_loop_ctr))
typedef char a64_off_vm_stack_fits[(offsetof(CL_Thread, vm.stack) % 8 == 0 &&
                                    offsetof(CL_Thread, vm.stack) < 32760) ? 1 : -1];
typedef char a64_off_words_fit[(offsetof(CL_Thread, vm.stack_size) % 4 == 0 &&
                                offsetof(CL_Thread, vm.sp) % 4 == 0 &&
                                offsetof(CL_Thread, mv_count) % 4 == 0 &&
                                offsetof(CL_Thread, mv_count) < 16380) ? 1 : -1];
typedef char a64_off_loop_ctr_fits[(offsetof(CL_Thread, jit_loop_ctr) % 2 == 0 &&
                                    offsetof(CL_Thread, jit_loop_ctr) < 8190) ? 1 : -1];
typedef char a64_sp_is_int[(sizeof(((CL_Thread *)0)->vm.sp) == 4 &&
                            sizeof(((CL_Thread *)0)->mv_count) == 4 &&
                            sizeof(((CL_Thread *)0)->jit_loop_ctr) == 2) ? 1 : -1];

/* The frame the prologue pushes: x29/x30, x19-x22, x27/x28. */
#define FRAME_BYTES 64

/* Walker limits: the operand encodings below assume them. */
#define A64_MAX_LOCALS 1000

void cl_jit_backend_init(void)
{
    char envbuf[16];
    const char *v;
    cl_codeheap_init();
    /* CLAMIGA_JIT_HOT=N: the hot-call threshold at boot (0 = eager, every
     * function compiled at definition) -- the differential runs of the
     * whole suite against the interpreter (tests/test_jit_a64_walk.sh). */
    v = platform_getenv("CLAMIGA_JIT_HOT", envbuf, sizeof(envbuf));
    if (v != NULL && v[0] >= '0' && v[0] <= '9')
        cl_jit_set_hot_threshold(atoi(v));
}

void cl_jit_backend_shutdown(void)
{
    cl_codeheap_shutdown();
}

void cl_jit_free_native(void *code)
{
    cl_codeheap_free(code);
}

/* Drop BC's native code and invalidate every call site that might cache
 * it: unhook first, then bump, then free (the order jit_m68k.c uses). */
static void drop_native(CL_Bytecode *bc)
{
    void *old = bc->native_code;
    if (bc->native_relocs) platform_free(bc->native_relocs);
    bc->native_relocs = NULL;
    bc->native_reloc_count = 0;
    bc->native_code = NULL;
    bc->native_len = 0;
    if (old) {
        cl_call_gen_bump("native code replaced");
        cl_codeheap_free(old);
    }
}

/* --- Emitter state ------------------------------------------------------ */

typedef struct {
    A64Asm    a;
    CodeBuf   cb;
    uint64_t *lit_val;        /* 64-bit literals (helper and data addresses) */
    A64Label *lit_label;
    uint32_t  n_lits, cap_lits;
    int       oom;
} Gen;

/* The label of the 64-bit literal V, appended after the code by
 * finish_literals; equal values share one slot. */
static A64Label lit_label(Gen *g, uint64_t v)
{
    uint32_t i;
    for (i = 0; i < g->n_lits; i++)
        if (g->lit_val[i] == v) return g->lit_label[i];
    if (g->n_lits == g->cap_lits) {
        uint32_t cap = g->cap_lits ? g->cap_lits * 2 : 16;
        uint64_t *nv = (uint64_t *)platform_alloc((unsigned long)cap * sizeof(uint64_t));
        A64Label *nl = (A64Label *)platform_alloc((unsigned long)cap * sizeof(A64Label));
        if (nv == NULL || nl == NULL) {
            if (nv) platform_free(nv);
            if (nl) platform_free(nl);
            g->oom = 1;
            return 0;
        }
        for (i = 0; i < g->n_lits; i++) { nv[i] = g->lit_val[i]; nl[i] = g->lit_label[i]; }
        if (g->lit_val) platform_free(g->lit_val);
        if (g->lit_label) platform_free(g->lit_label);
        g->lit_val = nv;
        g->lit_label = nl;
        g->cap_lits = cap;
    }
    g->lit_val[g->n_lits] = v;
    g->lit_label[g->n_lits] = a64_label_new(&g->a);
    return g->lit_label[g->n_lits++];
}

/* The literals, 8-byte aligned, after the last instruction. */
static void finish_literals(Gen *g)
{
    uint32_t i;
    if (g->n_lits == 0) return;
    if (cb_len(&g->cb) & 7) a64_emit(&g->a, a64_brk(0));   /* never reached */
    for (i = 0; i < g->n_lits; i++) {
        a64_bind(&g->a, g->lit_label[i]);
        cb_emit_u32_le(&g->cb, (uint32_t)g->lit_val[i]);
        cb_emit_u32_le(&g->cb, (uint32_t)(g->lit_val[i] >> 32));
    }
}

#define E(insn) a64_emit(&g->a, (insn))

static void emit_ldr_lit64(Gen *g, int rt, const void *addr)
{
    a64_ldr_lit(&g->a, 8, rt, lit_label(g, (uint64_t)(uintptr_t)addr));
}

/* xRD = xRN + IMM, through x17 when IMM does not encode. */
static void emit_add_x(Gen *g, int rd, int rn, uint32_t imm)
{
    uint32_t w = a64_add_imm(1, rd, rn, imm);
    if (w != A64_BAD) { E(w); return; }
    a64_mov_imm(&g->a, 1, 17, imm);
    E(a64_add_reg(1, rd, rn, 17));
}

static void emit_sub_x(Gen *g, int rd, int rn, uint32_t imm)
{
    uint32_t w = a64_sub_imm(1, rd, rn, imm);
    if (w != A64_BAD) { E(w); return; }
    a64_mov_imm(&g->a, 1, 17, imm);
    E(a64_sub_reg(1, rd, rn, 17));
}

/* cl_vm.sp = (x20 - x27) / 4: every value of the frame is now a root. */
static void emit_sync_sp(Gen *g)
{
    E(a64_sub_reg(1, 9, R_TOP, R_STK));
    E(a64_lsr_imm(1, 9, 9, 2));
    E(a64_str_uoff(4, 9, R_THR, OFF_VM_SP));
}

/* Call the C function FN with the arguments already in x0-x7. */
static void emit_call(Gen *g, const void *fn)
{
    emit_sync_sp(g);
    emit_ldr_lit64(g, 16, fn);
    E(a64_blr(16));
}

static void emit_push(Gen *g, int r)   { E(a64_str_post(4, r, R_TOP, 4)); }
static void emit_pop(Gen *g, int r)    { E(a64_ldr_pre(4, r, R_TOP, -4)); }
/* The K-th value from the top (1 = TOS), not popped. */
static void emit_peek(Gen *g, int r, uint32_t k)
{
    uint32_t w = a64_ldur(4, r, R_TOP, -4 * (int32_t)k);
    if (w != A64_BAD) { E(w); return; }
    emit_sub_x(g, 17, R_TOP, 4 * k);
    E(a64_ldr_uoff(4, r, 17, 0));
}
static void emit_poke(Gen *g, int r, uint32_t k)
{
    E(a64_stur(4, r, R_TOP, -4 * (int32_t)k));
}
static void emit_drop(Gen *g, uint32_t n)
{
    if (n) emit_sub_x(g, R_TOP, R_TOP, 4 * n);
}
static void emit_load_local(Gen *g, int r, uint32_t slot)
{
    E(a64_ldr_uoff(4, r, R_BP, 4 * slot));
}
static void emit_store_local(Gen *g, int r, uint32_t slot)
{
    E(a64_str_uoff(4, r, R_BP, 4 * slot));
}
static void emit_load_const(Gen *g, int r, uint32_t idx)
{
    uint32_t w = a64_ldr_uoff(4, r, R_K, 4 * idx);
    if (w != A64_BAD) { E(w); return; }
    a64_mov_imm(&g->a, 0, 17, 4 * idx);
    E(a64_ldr_w_uxtw(r, R_K, 17));
}
/* xRD = &bc->constants[IDX]. */
static void emit_const_addr(Gen *g, int rd, uint32_t idx)
{
    emit_add_x(g, rd, R_K, 4 * idx);
}
/* wR = T, read through the CL_T global (a collection may move the symbol). */
static void emit_load_t(Gen *g, int r)
{
    emit_ldr_lit64(g, r, &CL_T);
    E(a64_ldr_uoff(4, r, r, 0));
}
/* cl_mv_count = 1: the interpreter's write after a single-valued opcode. */
static void emit_mv1(Gen *g)
{
    E(a64_movz(0, 9, 1, 0));
    E(a64_str_uoff(4, 9, R_THR, OFF_MV_COUNT));
}
/* The loop poll (spec "Rule 4"): count CL_Thread.jit_loop_ctr down and, on
 * the borrow, run cl_jit_runtime_loop_poll (safepoint, interrupt, Ctrl-C),
 * which resets the countdown. */
static void emit_loop_poll(Gen *g)
{
    A64Label skip = a64_label_new(&g->a);
    E(a64_ldr_uoff(2, 9, R_THR, OFF_LOOP_CTR));
    E(a64_subs_imm(0, 9, 9, 1));
    E(a64_str_uoff(2, 9, R_THR, OFF_LOOP_CTR));
    a64_bcond(&g->a, A64_HS, skip);
    emit_call(g, (const void *)&cl_jit_runtime_loop_poll);
    a64_bind(&g->a, skip);
}

/* Binary helper op: w0 = second, w1 = TOS (both still on the stack, so
 * rooted across the call), FN(w0, w1), the two replaced by the result. */
static void emit_binary_helper(Gen *g, const void *fn, int mv)
{
    emit_peek(g, 0, 2);
    emit_peek(g, 1, 1);
    emit_call(g, fn);
    emit_drop(g, 2);
    emit_push(g, 0);
    if (mv) emit_mv1(g);
}

/* --- Bytecode walk ------------------------------------------------------- */

#define T_TARGET 1      /* some branch lands here */
#define T_LOOP   2      /* a backward branch lands here: poll */

static int32_t read_i32_be(const uint8_t *p)
{
    return (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                     ((uint32_t)p[2] << 8) | (uint32_t)p[3]);
}

static uint16_t read_u16_be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/* landing = BASE + OFFSET within [0, code_len], else -1. */
static int32_t landing_ip(int32_t offset, uint32_t base, uint32_t code_len)
{
    int64_t t = (int64_t)base + offset;
    if (t < 0 || t > (int64_t)code_len) return -1;
    return (int32_t)t;
}

/* The operand bytes after opcode OP, or -1 for an opcode this walker does
 * not handle (the function then stays interpreted). */
static int op_operand_len(uint8_t op)
{
    switch (op) {
    case OP_NIL: case OP_T: case OP_POP: case OP_DUP: case OP_RET:
    case OP_CAR: case OP_CDR: case OP_CONS: case OP_NOT: case OP_EQ:
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
    case OP_LT: case OP_GT: case OP_LE: case OP_GE: case OP_NUMEQ:
    case OP_MV_RESET: case OP_RPLACA: case OP_RPLACD: case OP_ASET:
    case OP_APPLY: case OP_CHAREQ:
        return 0;
    case OP_LOAD: case OP_STORE: case OP_CALL: case OP_TAILCALL:
    case OP_STRUCT_REF: case OP_STRUCT_SET: case OP_LIST:
    case OP_AREF: case OP_PUSH_LOCAL: case OP_POP_LOCAL:
    case OP_STORE_POP: case OP_LOAD_MV_RESET: case OP_LOAD_RET: case OP_POP_LOAD:
        return 1;
    case OP_CONST: case OP_GLOAD: case OP_GSTORE: case OP_FLOAD: case OP_FSTORE:
    case OP_ASSERT_TYPE:
    case OP_LOAD_LOAD: case OP_LOAD_STRUCT_REF: case OP_LOAD_STORE_POP:
        return 2;
    case OP_CALL_GLOBAL: case OP_TAILCALL_GLOBAL: case OP_LOAD_CONST:
        return 3;
    case OP_LOAD_CALL_GLOBAL:                   /* u8 slot, u16 sym, u8 n */
    case OP_JMP: case OP_JNIL: case OP_JTRUE: case OP_EQ_JNIL:   /* i32 */
        return 4;
    case OP_GLOAD_CALL_GLOBAL:                  /* u16 sym, u16 sym, u8 n */
    case OP_LOAD_JNIL: case OP_CMP_BR:          /* u8, i32 */
        return 5;
    case OP_GLOAD_JNIL: case OP_GLOAD_EQ_JNIL:  /* u16 sym, i32 */
        return 6;
    default:
        return -1;
    }
}

/* The branch operand of a branching opcode: its offset's position after the
 * opcode byte, or -1 for a straight-line opcode. */
static int op_branch_pos(uint8_t op)
{
    switch (op) {
    case OP_JMP: case OP_JNIL: case OP_JTRUE: case OP_EQ_JNIL: return 0;
    case OP_LOAD_JNIL: case OP_CMP_BR: return 1;
    case OP_GLOAD_JNIL: case OP_GLOAD_EQ_JNIL: return 2;
    default: return -1;
    }
}

/* Mark every branch target (and loop header) and validate the stream: every
 * opcode known, every operand inside the code, every constant index and
 * local slot in range, the symbol operands symbols.  0 = decline. */
static int prescan(const CL_Bytecode *bc, uint8_t *tgt)
{
    uint32_t ip = 0;
    while (ip < bc->code_len) {
        uint8_t op = bc->code[ip];
        int n = op_operand_len(op), bpos;
        const uint8_t *o = bc->code + ip + 1;
        uint32_t end;
        if (n < 0) return 0;
        end = ip + 1 + (uint32_t)n;
        if (end > bc->code_len) return 0;
        switch (op) {      /* operand validation */
        case OP_LOAD: case OP_STORE: case OP_STORE_POP: case OP_LOAD_MV_RESET:
        case OP_LOAD_RET: case OP_POP_LOAD: case OP_PUSH_LOCAL: case OP_POP_LOCAL:
        case OP_LOAD_JNIL: case OP_LOAD_STRUCT_REF:
            if (o[0] >= bc->n_locals) return 0;
            break;
        case OP_LOAD_LOAD: case OP_LOAD_STORE_POP:
            if (o[0] >= bc->n_locals || o[1] >= bc->n_locals) return 0;
            break;
        case OP_CONST: case OP_ASSERT_TYPE:
            if (read_u16_be(o) >= bc->n_constants) return 0;
            break;
        case OP_LOAD_CONST:
            if (o[0] >= bc->n_locals || read_u16_be(o + 1) >= bc->n_constants) return 0;
            break;
        case OP_GLOAD: case OP_GSTORE: case OP_FLOAD: case OP_FSTORE:
        case OP_CALL_GLOBAL: case OP_TAILCALL_GLOBAL:
        case OP_GLOAD_JNIL: case OP_GLOAD_EQ_JNIL: {
            uint16_t k = read_u16_be(o);
            if (k >= bc->n_constants || !CL_SYMBOL_P(bc->constants[k])) return 0;
            break;
        }
        case OP_LOAD_CALL_GLOBAL: {
            uint16_t k = read_u16_be(o + 1);
            if (o[0] >= bc->n_locals || k >= bc->n_constants ||
                !CL_SYMBOL_P(bc->constants[k])) return 0;
            break;
        }
        case OP_GLOAD_CALL_GLOBAL: {
            uint16_t k1 = read_u16_be(o), k2 = read_u16_be(o + 2);
            if (k1 >= bc->n_constants || k2 >= bc->n_constants ||
                !CL_SYMBOL_P(bc->constants[k1]) || !CL_SYMBOL_P(bc->constants[k2]))
                return 0;
            break;
        }
        case OP_AREF:
            if (o[0] > CL_AREF_KIND_SCHAR) return 0;
            break;
        case OP_CMP_BR:
            if ((o[0] & CL_CMP_BR_CMP_MASK) > CL_CMP_BR_CHAREQ ||
                (o[0] & ~(CL_CMP_BR_CMP_MASK | CL_CMP_BR_IF_TRUE)))
                return 0;
            break;
        default:
            break;
        }
        bpos = op_branch_pos(op);
        if (bpos >= 0) {
            int32_t t = landing_ip(read_i32_be(o + bpos), end, bc->code_len);
            if (t < 0) return 0;
            tgt[t] |= T_TARGET;
            if ((uint32_t)t <= ip) tgt[t] |= T_LOOP;
        }
        ip = end;
    }
    return 1;
}

/* The operand-stack depth at each IP is fixed by the bytecode (the
 * compiler keeps it equal on every path into a join), and the walker
 * tracks it: to size the prologue's overflow check, and to check that
 * equality -- an edge whose depth disagrees with its target declines the
 * function rather than guessing.  DEPTH_UNSET marks a target no edge has
 * reached yet. */
#define DEPTH_UNSET (-1)

typedef struct {
    int32_t *at;        /* depth on entry to each IP, or DEPTH_UNSET */
    int32_t  cur;
    int32_t  max;
    int      bad;
} Depth;

static void depth_need(Depth *d, int32_t n) { if (d->cur < n) d->bad = 1; }
static void depth_add(Depth *d, int32_t n)
{
    d->cur += n;
    if (d->cur < 0) d->bad = 1;
    if (d->cur > d->max) d->max = d->cur;
}
/* An edge to TARGET with the current depth (from the branch at FROM). */
static void depth_edge(Depth *d, int32_t target, uint32_t from)
{
    if ((uint32_t)target <= from) {          /* backward: already walked */
        if (d->at[target] != d->cur) d->bad = 1;
    } else if (d->at[target] == DEPTH_UNSET) {
        d->at[target] = d->cur;
    } else if (d->at[target] != d->cur) {
        d->bad = 1;
    }
}

/* The function's native code, or NULL to leave it interpreted.  *LEN_OUT
 * receives its length. */
static uint8_t *walk(const CL_Bytecode *bc, uint32_t *len_out)
{
    Gen gs, *g = &gs;
    uint8_t *tgt = NULL;
    A64Label *ip_lab = NULL;
    Depth d;
    uint32_t arity, n_locals, ip, i;
    int fell = 1;               /* control reaches the next IP from above */
    A64Label l_body, l_epilogue, l_overflow, l_check;
    uint8_t *code = NULL;
    uint32_t len = 0;

    /* The gate: a fixed positional arity -- &rest, &optional and &key need
     * prologues the walker does not have yet (phase 3). */
    if (bc->code == NULL || bc->code_len == 0) return NULL;
    if (bc->arity & 0x8000) return NULL;
    if (bc->n_optional != 0 || bc->flags != 0 || bc->n_keys != 0) return NULL;
    arity = bc->arity & 0x7FFF;
    n_locals = bc->n_locals;
    if (n_locals < arity || n_locals > A64_MAX_LOCALS) return NULL;
    if (bc->n_constants > 0 && bc->constants == NULL) return NULL;

    d.at = NULL;
    d.cur = 0; d.max = 0; d.bad = 0;
    tgt = (uint8_t *)platform_alloc((unsigned long)bc->code_len + 1);
    ip_lab = (A64Label *)platform_alloc((unsigned long)(bc->code_len + 1) * sizeof(A64Label));
    d.at = (int32_t *)platform_alloc((unsigned long)(bc->code_len + 1) * sizeof(int32_t));
    if (tgt == NULL || ip_lab == NULL || d.at == NULL) goto out;
    for (i = 0; i <= bc->code_len; i++) { tgt[i] = 0; d.at[i] = DEPTH_UNSET; }
    if (!prescan(bc, tgt)) goto out;

    cb_init(&g->cb, 256);
    a64_asm_init(&g->a, &g->cb);
    g->lit_val = NULL; g->lit_label = NULL;
    g->n_lits = g->cap_lits = 0; g->oom = 0;
    for (i = 0; i <= bc->code_len; i++)
        ip_lab[i] = tgt[i] ? a64_label_new(&g->a) : 0;
    l_body = a64_label_new(&g->a);
    l_epilogue = a64_label_new(&g->a);
    l_overflow = a64_label_new(&g->a);
    l_check = a64_label_new(&g->a);

    /* Prologue: the frame record and the callee-saved registers, the
     * register roles, then the overflow check (its displacement is the
     * walk's maximum depth, known only at the end: a branch to a check
     * emitted after the body, which branches back), the locals and the
     * function slot. */
    E(a64_stp_pre(A64_FP, A64_LR, A64_SP, -FRAME_BYTES));
    E(a64_mov_sp(A64_FP, A64_SP));
    E(a64_stp_off(19, 20, A64_SP, 16));
    E(a64_stp_off(21, 22, A64_SP, 32));
    E(a64_stp_off(27, 28, A64_SP, 48));
    E(a64_mov_reg(1, R_THR, 0));
    E(a64_mov_reg(1, R_BP, 1));
    E(a64_ldr_uoff(8, R_STK, R_THR, OFF_VM_STACK));
    emit_ldr_lit64(g, R_K, bc->constants);
    a64_b(&g->a, l_check);
    {
        A64Label l_back = a64_label_new(&g->a);
        a64_bind(&g->a, l_back);
        for (i = arity; i < n_locals; i++)
            emit_store_local(g, A64_ZR, i);            /* NIL == 0 */
        emit_store_local(g, 3, n_locals);              /* func */
        emit_add_x(g, R_TOP, R_BP, 4 * (n_locals + 1));
        a64_bind(&g->a, l_body);

        ip = 0;
        while (ip < bc->code_len && !d.bad && !g->a.bad && !g->oom) {
            uint8_t op;
            const uint8_t *o;
            uint32_t insn = ip;
            if (tgt[ip]) {
                if (fell) {
                    if (d.at[ip] != DEPTH_UNSET && d.at[ip] != d.cur) { d.bad = 1; break; }
                    d.at[ip] = d.cur;
                } else if (d.at[ip] != DEPTH_UNSET) {
                    d.cur = d.at[ip];
                } else {
                    /* Reached only by a later backward branch: assume the
                     * depth control left with; that branch checks it. */
                    d.at[ip] = d.cur;
                }
                a64_bind(&g->a, ip_lab[ip]);
                if (tgt[ip] & T_LOOP) emit_loop_poll(g);
            }
            fell = 1;
            op = bc->code[ip];
            o = bc->code + ip + 1;
            ip += 1 + (uint32_t)op_operand_len(op);

            switch (op) {
            case OP_NIL:
                E(a64_str_post(4, A64_ZR, R_TOP, 4));
                depth_add(&d, 1);
                emit_mv1(g);
                break;
            case OP_T:
                emit_load_t(g, 9);
                emit_push(g, 9);
                depth_add(&d, 1);
                emit_mv1(g);
                break;
            case OP_CONST:
                emit_load_const(g, 9, read_u16_be(o));
                emit_push(g, 9);
                depth_add(&d, 1);
                emit_mv1(g);
                break;
            case OP_LOAD:
                emit_load_local(g, 9, o[0]);
                emit_push(g, 9);
                depth_add(&d, 1);
                break;
            case OP_STORE:
                depth_need(&d, 1);
                emit_peek(g, 9, 1);
                emit_store_local(g, 9, o[0]);
                break;
            case OP_POP:
                depth_add(&d, -1);
                emit_drop(g, 1);
                break;
            case OP_DUP:
                depth_need(&d, 1);
                emit_peek(g, 9, 1);
                emit_push(g, 9);
                depth_add(&d, 1);
                break;
            case OP_STORE_POP:
                depth_add(&d, -1);
                emit_pop(g, 9);
                emit_store_local(g, 9, o[0]);
                break;
            case OP_LOAD_LOAD:
                emit_load_local(g, 9, o[0]);
                emit_push(g, 9);
                emit_load_local(g, 9, o[1]);
                emit_push(g, 9);
                depth_add(&d, 2);
                break;
            case OP_LOAD_CONST:
                emit_load_local(g, 9, o[0]);
                emit_push(g, 9);
                emit_load_const(g, 9, read_u16_be(o + 1));
                emit_push(g, 9);
                depth_add(&d, 2);
                emit_mv1(g);
                break;
            case OP_LOAD_STORE_POP:
                emit_load_local(g, 9, o[0]);
                emit_store_local(g, 9, o[1]);
                break;
            case OP_POP_LOAD:
                depth_need(&d, 1);
                emit_load_local(g, 9, o[0]);
                emit_poke(g, 9, 1);
                break;
            case OP_LOAD_MV_RESET:
                emit_load_local(g, 9, o[0]);
                emit_push(g, 9);
                depth_add(&d, 1);
                emit_mv1(g);
                break;
            case OP_MV_RESET:
                emit_mv1(g);
                break;

            case OP_LOAD_RET:
                emit_load_local(g, 0, o[0]);
                a64_b(&g->a, l_epilogue);
                fell = 0;
                break;
            case OP_RET:
                /* The interpreter returns NIL from an empty operand stack. */
                if (d.cur > 0) {
                    emit_pop(g, 0);
                    depth_add(&d, -1);
                } else {
                    E(a64_movz(0, 0, 0, 0));
                }
                a64_b(&g->a, l_epilogue);
                fell = 0;
                break;

            /* Arithmetic and the numeric comparisons: the helpers are the
             * interpreter's slow paths, whose fixnum arms give the fast
             * paths' results. */
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
            case OP_LT: case OP_GT: case OP_LE: case OP_GE: case OP_NUMEQ: {
                const void *fn =
                    op == OP_ADD ? (const void *)&cl_jit_runtime_add :
                    op == OP_SUB ? (const void *)&cl_jit_runtime_sub :
                    op == OP_MUL ? (const void *)&cl_jit_runtime_mul :
                    op == OP_DIV ? (const void *)&cl_jit_runtime_div :
                    op == OP_LT  ? (const void *)&cl_jit_runtime_lt :
                    op == OP_GT  ? (const void *)&cl_jit_runtime_gt :
                    op == OP_LE  ? (const void *)&cl_jit_runtime_le :
                    op == OP_GE  ? (const void *)&cl_jit_runtime_ge :
                                   (const void *)&cl_jit_runtime_numeq;
                depth_need(&d, 2);
                emit_binary_helper(g, fn, 1);
                depth_add(&d, -1);
                break;
            }
            case OP_CHAREQ:
                depth_need(&d, 2);
                emit_binary_helper(g, (const void *)&cl_jit_runtime_chareq, 1);
                depth_add(&d, -1);
                break;
            case OP_RPLACA: case OP_RPLACD:
                depth_need(&d, 2);
                emit_binary_helper(g, op == OP_RPLACA
                                   ? (const void *)&cl_jit_runtime_rplaca
                                   : (const void *)&cl_jit_runtime_rplacd, 1);
                depth_add(&d, -1);
                break;
            case OP_APPLY:
                /* The interpreter's own OP_APPLY, entered from C: spreads
                 * any argument count, resolves a symbol, &rest callees. */
                depth_need(&d, 2);
                emit_binary_helper(g, (const void *)&cl_vm_apply_list, 0);
                depth_add(&d, -1);
                break;

            case OP_EQ: case OP_NOT:
                if (op == OP_EQ) {
                    depth_add(&d, -1);
                    emit_peek(g, 10, 2);
                    emit_peek(g, 11, 1);
                    emit_drop(g, 2);
                    E(a64_cmp_reg(0, 10, 11));
                } else {
                    depth_need(&d, 1);
                    emit_pop(g, 10);
                    E(a64_cmp_imm(0, 10, 0));
                }
                emit_load_t(g, 9);
                E(a64_csel(0, 9, 9, A64_ZR, A64_EQ));
                emit_push(g, 9);
                emit_mv1(g);
                break;

            case OP_CAR: case OP_CDR:
                depth_need(&d, 1);
                emit_peek(g, 0, 1);
                emit_call(g, op == OP_CAR ? (const void *)&cl_jit_runtime_car
                                          : (const void *)&cl_jit_runtime_cdr);
                emit_poke(g, 0, 1);
                emit_mv1(g);
                break;
            case OP_CONS:
                depth_need(&d, 2);
                emit_sub_x(g, 0, R_TOP, 8);
                emit_call(g, (const void *)&cl_jit_vmstack_cons);
                emit_drop(g, 2);
                emit_push(g, 0);
                depth_add(&d, -1);
                emit_mv1(g);
                break;
            case OP_LIST: {
                uint32_t n = o[0];
                depth_need(&d, (int32_t)n);
                emit_sub_x(g, 0, R_TOP, 4 * n);
                E(a64_movz(0, 1, n, 0));
                emit_call(g, (const void *)&cl_jit_vmstack_list);
                emit_drop(g, n);
                emit_push(g, 0);
                depth_add(&d, 1 - (int32_t)n);
                emit_mv1(g);
                break;
            }

            case OP_GLOAD:
                emit_load_const(g, 0, read_u16_be(o));
                emit_call(g, (const void *)&cl_jit_runtime_gload);
                emit_push(g, 0);
                depth_add(&d, 1);
                emit_mv1(g);
                break;
            case OP_GSTORE: case OP_FSTORE:
                depth_need(&d, 1);
                emit_load_const(g, 0, read_u16_be(o));
                emit_peek(g, 1, 1);
                emit_call(g, op == OP_GSTORE ? (const void *)&cl_jit_runtime_gstore
                                             : (const void *)&cl_jit_runtime_fstore);
                break;
            case OP_FLOAD:
                emit_load_const(g, 0, read_u16_be(o));
                emit_call(g, (const void *)&cl_jit_runtime_fload);
                emit_push(g, 0);
                depth_add(&d, 1);
                emit_mv1(g);
                break;

            case OP_LOAD_STRUCT_REF:
                emit_load_local(g, 9, o[0]);
                emit_push(g, 9);
                depth_add(&d, 1);
                o += 1;
                /* FALLTHROUGH */
            case OP_STRUCT_REF:
                depth_need(&d, 1);
                emit_peek(g, 0, 1);
                E(a64_movz(0, 1, o[0], 0));
                emit_call(g, (const void *)&cl_jit_runtime_struct_ref);
                emit_poke(g, 0, 1);
                emit_mv1(g);
                break;
            case OP_STRUCT_SET:
                depth_need(&d, 2);
                emit_peek(g, 0, 2);
                E(a64_movz(0, 1, o[0], 0));
                emit_peek(g, 2, 1);
                emit_call(g, (const void *)&cl_jit_runtime_struct_set);
                emit_drop(g, 2);
                emit_push(g, 0);
                depth_add(&d, -1);
                emit_mv1(g);
                break;
            case OP_ASET:
                depth_need(&d, 3);
                emit_peek(g, 0, 3);
                emit_peek(g, 1, 2);
                emit_peek(g, 2, 1);
                emit_call(g, (const void *)&cl_jit_runtime_aset);
                emit_drop(g, 3);
                emit_push(g, 0);
                depth_add(&d, -2);
                emit_mv1(g);
                break;
            case OP_ASSERT_TYPE:
                depth_need(&d, 1);
                emit_peek(g, 0, 1);
                emit_load_const(g, 1, read_u16_be(o));
                emit_call(g, (const void *)&cl_jit_runtime_assert_type);
                break;
            case OP_AREF:
                depth_need(&d, 2);
                emit_peek(g, 0, 2);
                emit_peek(g, 1, 1);
                E(a64_movz(0, 2, o[0], 0));
                emit_call(g, (const void *)&cl_jit_runtime_aref);
                emit_drop(g, 2);
                emit_push(g, 0);
                depth_add(&d, -1);
                emit_mv1(g);
                break;
            case OP_PUSH_LOCAL:
                depth_need(&d, 1);
                emit_sub_x(g, 0, R_TOP, 4);
                emit_add_x(g, 1, R_BP, 4u * o[0]);
                emit_call(g, (const void *)&cl_jit_vmstack_push_local);
                emit_poke(g, 0, 1);
                emit_mv1(g);
                break;
            case OP_POP_LOCAL:
                emit_add_x(g, 0, R_BP, 4u * o[0]);
                emit_call(g, (const void *)&cl_jit_runtime_pop_local);
                emit_push(g, 0);
                depth_add(&d, 1);
                emit_mv1(g);
                break;

            /* --- Calls ---------------------------------------------- */
            case OP_CALL: {
                uint32_t n = o[0];
                depth_need(&d, (int32_t)n + 1);
                E(a64_mov_reg(1, 0, R_THR));
                E(a64_mov_reg(1, 1, R_TOP));
                E(a64_movz(0, 2, n, 0));
                emit_call(g, (const void *)&cl_jit_vmstack_call);
                emit_drop(g, n + 1);
                emit_push(g, 0);
                depth_add(&d, -(int32_t)n);
                break;
            }
            case OP_LOAD_CALL_GLOBAL:
            case OP_GLOAD_CALL_GLOBAL:
            case OP_CALL_GLOBAL: {
                uint32_t n;
                uint16_t k;
                if (op == OP_LOAD_CALL_GLOBAL) {
                    emit_load_local(g, 9, o[0]);
                    emit_push(g, 9);
                    depth_add(&d, 1);
                    o += 1;
                } else if (op == OP_GLOAD_CALL_GLOBAL) {
                    /* The special's value; the callee sets the MV state. */
                    emit_load_const(g, 0, read_u16_be(o));
                    emit_call(g, (const void *)&cl_jit_runtime_gload);
                    emit_push(g, 0);
                    depth_add(&d, 1);
                    o += 2;
                }
                k = read_u16_be(o);
                n = o[2];
                depth_need(&d, (int32_t)n);
                E(a64_mov_reg(1, 0, R_THR));
                E(a64_mov_reg(1, 1, R_TOP));
                E(a64_movz(0, 2, n, 0));
                emit_const_addr(g, 3, k);
                emit_call(g, (const void *)&cl_jit_vmstack_call_global);
                emit_drop(g, n);
                emit_push(g, 0);
                depth_add(&d, 1 - (int32_t)n);
                break;
            }
            case OP_TAILCALL:
            case OP_TAILCALL_GLOBAL: {
                /* A self tail call (the callee runs this bytecode, the
                 * argument count is the arity) reuses the frame: the
                 * arguments over the parameters, the callee as the frame's
                 * function, the other locals NIL again, an empty operand
                 * stack, the loop poll, and back to the body.  Anything else
                 * is a call whose value this function returns.  The self
                 * path cannot be decided at compile time -- the binding may
                 * change -- so a guard decides each time. */
                int global = (op == OP_TAILCALL_GLOBAL);
                uint16_t k = global ? read_u16_be(o) : 0;
                uint32_t n = global ? o[2] : o[0];
                uint32_t fslot = global ? 0 : 1;
                int self = (n == arity) &&
                           (!global || bc->constants[k] == bc->name);
                depth_need(&d, (int32_t)(n + fslot));
                if (self) {
                    A64Label l_not = a64_label_new(&g->a);
                    if (global) {
                        emit_load_const(g, 0, k);
                        emit_call(g, (const void *)&cl_jit_runtime_fload);
                    } else {
                        emit_peek(g, 0, n + 1);
                    }
                    /* The callee is held across a non-allocating guard. */
                    E(a64_mov_reg(0, R_KEEP, 0));
                    emit_load_local(g, 1, n_locals);
                    emit_call(g, (const void *)&cl_jit_vmstack_is_self);
                    a64_cbz(&g->a, 0, 0, l_not);
                    emit_sub_x(g, 10, R_TOP, 4 * n);
                    for (i = 0; i < n; i++) {
                        E(a64_ldr_uoff(4, 9, 10, 4 * i));
                        emit_store_local(g, 9, i);
                    }
                    emit_store_local(g, R_KEEP, n_locals);
                    for (i = arity; i < n_locals; i++)
                        emit_store_local(g, A64_ZR, i);
                    emit_add_x(g, R_TOP, R_BP, 4 * (n_locals + 1));
                    emit_loop_poll(g);
                    a64_b(&g->a, l_body);
                    a64_bind(&g->a, l_not);
                }
                /* Any other callee: the tail helper either calls it (w0 is
                 * the value) or hands a native one to cl_jit_invoke. */
                E(a64_mov_reg(1, 0, R_THR));
                E(a64_mov_reg(1, 1, R_TOP));
                E(a64_movz(0, 2, n, 0));
                E(a64_mov_reg(1, 3, R_BP));
                if (global) emit_const_addr(g, 4, k);
                else E(a64_mov_reg(1, 4, A64_ZR));
                emit_call(g, (const void *)&cl_jit_vmstack_tail);
                a64_b(&g->a, l_epilogue);
                /* The code after it (the compiler's OP_RET) is dead unless
                 * a branch lands there; its depth is the call's result. */
                depth_add(&d, 1 - (int32_t)(n + fslot));
                fell = 0;
                break;
            }

            /* --- Branches ------------------------------------------- */
            case OP_JMP:
            case OP_JNIL:
            case OP_JTRUE: {
                int32_t t = landing_ip(read_i32_be(o), ip, bc->code_len);
                if (op != OP_JMP) {
                    depth_add(&d, -1);
                    emit_pop(g, 9);
                }
                depth_edge(&d, t, insn);
                if (op == OP_JMP) {
                    a64_b(&g->a, ip_lab[t]);
                    fell = 0;
                } else if (op == OP_JNIL) {
                    a64_cbz(&g->a, 0, 9, ip_lab[t]);
                } else {
                    a64_cbnz(&g->a, 0, 9, ip_lab[t]);
                }
                break;
            }
            case OP_LOAD_JNIL: {
                int32_t t = landing_ip(read_i32_be(o + 1), ip, bc->code_len);
                emit_load_local(g, 9, o[0]);
                depth_edge(&d, t, insn);
                a64_cbz(&g->a, 0, 9, ip_lab[t]);
                break;
            }
            case OP_EQ_JNIL: {
                int32_t t = landing_ip(read_i32_be(o), ip, bc->code_len);
                depth_add(&d, -2);
                emit_peek(g, 10, 2);
                emit_peek(g, 11, 1);
                emit_drop(g, 2);
                emit_mv1(g);
                depth_edge(&d, t, insn);
                E(a64_cmp_reg(0, 10, 11));
                a64_bcond(&g->a, A64_NE, ip_lab[t]);
                break;
            }
            case OP_GLOAD_JNIL:
            case OP_GLOAD_EQ_JNIL: {
                int32_t t = landing_ip(read_i32_be(o + 2), ip, bc->code_len);
                emit_load_const(g, 0, read_u16_be(o));
                emit_call(g, (const void *)&cl_jit_runtime_gload);
                emit_mv1(g);
                if (op == OP_GLOAD_JNIL) {
                    depth_edge(&d, t, insn);
                    a64_cbz(&g->a, 0, 0, ip_lab[t]);
                } else {
                    depth_add(&d, -1);
                    emit_pop(g, 10);
                    depth_edge(&d, t, insn);
                    E(a64_cmp_reg(0, 10, 0));
                    a64_bcond(&g->a, A64_NE, ip_lab[t]);
                }
                break;
            }
            case OP_CMP_BR: {
                /* The comparison through cl_vm_compare_kind -- the
                 * interpreter's slow arm, which decides fixnums (and
                 * characters) the way its fast arm does -- then the branch
                 * on T/NIL. */
                uint8_t kind = o[0];
                int32_t t = landing_ip(read_i32_be(o + 1), ip, bc->code_len);
                depth_need(&d, 2);
                emit_mv1(g);
                emit_peek(g, 0, 2);
                emit_peek(g, 1, 1);
                E(a64_movz(0, 2, (uint32_t)(kind & CL_CMP_BR_CMP_MASK), 0));
                emit_call(g, (const void *)&cl_jit_runtime_cmp_kind);
                emit_drop(g, 2);
                depth_add(&d, -2);
                depth_edge(&d, t, insn);
                if (kind & CL_CMP_BR_IF_TRUE)
                    a64_cbnz(&g->a, 0, 0, ip_lab[t]);
                else
                    a64_cbz(&g->a, 0, 0, ip_lab[t]);
                break;
            }

            default:
                d.bad = 1;      /* op_operand_len and this switch disagree */
                break;
            }
        }
        /* The last opcode must not fall off the end of the code. */
        if (fell) d.bad = 1;
        if (d.bad || g->a.bad || g->oom) goto fail;

        /* The overflow check: bp + locals + func slot + deepest operand
         * stack must fit the VM stack (the interpreter's push check). */
        a64_bind(&g->a, l_check);
        emit_add_x(g, 9, R_BP, 4 * (n_locals + 1 + (uint32_t)d.max));
        E(a64_ldr_uoff(4, 10, R_THR, OFF_VM_STACK_SIZE));
        E(a64_add_uxtw(10, R_STK, 10, 2));
        E(a64_cmp_reg(1, 9, 10));
        a64_bcond(&g->a, A64_HI, l_overflow);
        a64_b(&g->a, l_back);

        a64_bind(&g->a, l_overflow);
        emit_ldr_lit64(g, 16, (const void *)&cl_jit_vmstack_stack_overflow);
        E(a64_blr(16));                    /* never returns */

        /* The epilogue: the result is in w0. */
        a64_bind(&g->a, l_epilogue);
        E(a64_ldp_off(27, 28, A64_SP, 48));
        E(a64_ldp_off(21, 22, A64_SP, 32));
        E(a64_ldp_off(19, 20, A64_SP, 16));
        E(a64_ldp_post(A64_FP, A64_LR, A64_SP, FRAME_BYTES));
        E(a64_ret());
    }
    finish_literals(g);
    if (g->oom || !a64_finish(&g->a)) goto fail;
    code = cb_finish(&g->cb, &len);
    a64_asm_free(&g->a);
    goto lits;

fail:
    a64_asm_free(&g->a);
    cb_free(&g->cb);
    code = NULL;
lits:
    if (g->lit_val) platform_free(g->lit_val);
    if (g->lit_label) platform_free(g->lit_label);
out:
    if (tgt) platform_free(tgt);
    if (ip_lab) platform_free(ip_lab);
    if (d.at) platform_free(d.at);
    *len_out = len;
    return code;
}

/* REPLACE: drop the code BC carries and install the new one
 * (cl_jit_compile).  Without it (the hot path) code a peer thread installed
 * meanwhile is kept, and this compile's result dropped. */
void cl_jit_backend_compile(CL_Bytecode *bc, int replace)
{
    uint8_t *code;
    uint32_t len = 0;
    void *entry;

    if (bc == NULL || !cl_jitc_active) return;
    if (replace) drop_native(bc);
    else if (bc->native_code) return;

    code = walk(bc, &len);
    if (code == NULL) return;
    if (!replace && bc->native_code) { platform_free(code); return; }
    entry = cl_codeheap_install(code, len);
    platform_free(code);
    if (entry == NULL) return;
    /* The code is flushed; its bytes must be visible to a peer thread
     * before the pointer that leads there. */
    platform_memory_barrier();
    bc->native_code = (uint8_t *)entry;
    bc->native_len  = len;
    cl_jitc_native_bytes += len;
}

CL_Obj cl_jit_invoke(CL_Obj func_obj, CL_Bytecode *bc, int nargs)
{
    CL_Obj result;
    CL_Thread *t;
    int32_t prev_nargs;
    int saved_sp, bp;
    int pushed_frame = 0;

    if (bc == NULL || bc->native_code == NULL) return CL_NIL;
    t = cl_get_current_thread();
    t->jit_invoke_count++;
    prev_nargs = t->jit_current_nargs;
    t->jit_current_nargs = (int32_t)nargs;
    saved_sp = t->vm.sp;

    /* The shadow frame of %JIT-SET-FRAMES, as in jit_m68k.c. */
    if (cl_jitc_shadow_frames && t->vm.fp < t->vm.frame_size) {
        CL_Frame *sf = &t->vm.frames[t->vm.fp++];
        sf->bytecode  = func_obj;
        sf->code      = bc->code;
        sf->constants = bc->constants;
        sf->ip        = 0;
        sf->bp        = (uint32_t)(t->vm.sp - nargs);
        sf->n_locals  = nargs;
        sf->nargs     = (uint16_t)nargs;
        sf->nlx_level = cl_nlx_top;
        sf->fslot     = 0;
        pushed_frame  = 1;
    }

    bp = t->vm.sp - nargs;
    result = ((a64_entry_t)bc->native_code)(t, &t->vm.stack[bp],
                                            (uint32_t)nargs, func_obj);

    /* Tail calls between native functions (cl_jit_vmstack_tail): the
     * callee's arguments at bp, the callee above them; enter it from the
     * same base.  Nothing allocates between the handoff and the read. */
    while (t->jit_tail_pending) {
        uint32_t n = t->jit_tail_pending - 1;
        CL_Obj func = t->vm.stack[bp + (int)n];
        CL_Bytecode *cbc;
        t->jit_tail_pending = 0;
        t->vm.sp = bp + (int)n + 1;
        cbc = cl_jit_vmstack_native_callee(func, n);
        if (cbc == NULL) {             /* defensive: dispatch it as a call */
            result = cl_vm_apply(func, &t->vm.stack[bp], (int)n);
            break;
        }
        t->jit_invoke_count++;
        t->jit_current_nargs = (int32_t)n;
        if (pushed_frame) {
            CL_Frame *sf = &t->vm.frames[t->vm.fp - 1];
            sf->bytecode  = func;
            sf->code      = cbc->code;
            sf->constants = cbc->constants;
            sf->n_locals  = n;
            sf->nargs     = (uint16_t)n;
        }
        result = ((a64_entry_t)cbc->native_code)(t, &t->vm.stack[bp], n, func);
    }

    /* The body wrote sp above its frame at every helper call; the caller
     * pops the arguments from where it pushed them. */
    t->vm.sp = saved_sp;
    if (pushed_frame) t->vm.fp--;
    t->jit_current_nargs = prev_nargs;
    return result;
}

/* %JIT-COMPILE-STUB: `mov w0, #0; ret` -- returns NIL whatever the
 * function's body would.  Proves the assembler -> code heap -> entry path
 * independently of the walker. */
int cl_jit_emit_stub(CL_Bytecode *bc)
{
    CodeBuf cb;
    A64Asm a;
    uint8_t *code;
    uint32_t len;
    void *entry;

    if (bc == NULL) return 0;
    cb_init(&cb, 8);
    a64_asm_init(&a, &cb);
    a64_emit(&a, a64_movz(0, 0, (uint32_t)CL_NIL, 0));
    a64_emit(&a, a64_ret());
    if (!a64_finish(&a)) { a64_asm_free(&a); cb_free(&cb); return 0; }
    a64_asm_free(&a);
    code = cb_finish(&cb, &len);
    if (code == NULL) return 0;
    entry = cl_codeheap_install(code, len);
    platform_free(code);
    if (entry == NULL) return 0;

    drop_native(bc);
    platform_memory_barrier();
    bc->native_code = (uint8_t *)entry;
    bc->native_len  = len;
    cl_jitc_native_bytes += len;
    return 1;
}

/* Native-to-native direct calls (specs/jit-direct-calls.md) do not exist
 * on AArch64 yet: there is no call site to switch, so the switch reads NIL
 * whatever it is set to, and every counter stays zero. */
void cl_jit_set_direct_calls(int on)
{
    (void)on;
}

int cl_jit_direct_calls_enabled(void) { return 0; }

void cl_jit_direct_call_stats(uint32_t *out)
{
    int i;
    for (i = 0; i < CL_JIT_DS_COUNT; i++) out[i] = 0;
}

/* --- Disassembler -----------------------------------------------------
 * Decodes the forms the backend emits; anything else (the literal words
 * after the code among them) prints as .word.  Same line format as the
 * m68k one: offset, bytes, mnemonic. */

static const char *const a64_cond_names[16] = {
    "eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc",
    "hi", "ls", "ge", "lt", "gt", "le", "al", "nv"
};

static int32_t sext(uint32_t v, int bits)
{
    uint32_t m = (uint32_t)1 << (bits - 1);
    return (int32_t)((v ^ m) - m);
}

/* "w5", "x20", "wzr", "sp" ... ZR_IS_SP picks what 31 means. */
static const char *rn(char *buf, int sf, uint32_t r, int zr_is_sp)
{
    if (r == 31) return zr_is_sp ? "sp" : (sf ? "xzr" : "wzr");
    snprintf(buf, 8, "%c%u", sf ? 'x' : 'w', r);
    return buf;
}

static void disasm_word(uint32_t w, uint32_t off, char *out, size_t n)
{
    char a[8], b[8], c[8], d[8];
    int sf = (int)(w >> 31);
    uint32_t rd = w & 31u, r1 = (w >> 5) & 31u, r2 = (w >> 16) & 31u;

    if (w == a64_ret()) snprintf(out, n, "ret");
    else if (w == a64_nop()) snprintf(out, n, "nop");
    else if ((w & 0xFFE0001Fu) == 0xD4200000u)
        snprintf(out, n, "brk #0x%x", (w >> 5) & 0xFFFFu);
    else if ((w & 0x7F800000u) == 0x52800000u)
        snprintf(out, n, "movz %s, #0x%x, lsl #%u", rn(a, sf, rd, 0),
                 (w >> 5) & 0xFFFFu, ((w >> 21) & 3u) * 16);
    else if ((w & 0x7F800000u) == 0x72800000u)
        snprintf(out, n, "movk %s, #0x%x, lsl #%u", rn(a, sf, rd, 0),
                 (w >> 5) & 0xFFFFu, ((w >> 21) & 3u) * 16);
    else if ((w & 0x7F800000u) == 0x12800000u)
        snprintf(out, n, "movn %s, #0x%x, lsl #%u", rn(a, sf, rd, 0),
                 (w >> 5) & 0xFFFFu, ((w >> 21) & 3u) * 16);
    else if ((w & 0xFC000000u) == 0x14000000u || (w & 0xFC000000u) == 0x94000000u)
        snprintf(out, n, "%s %ld", (w >> 31) ? "bl" : "b",
                 (long)off + 4L * sext(w & 0x3FFFFFFu, 26));
    else if ((w & 0xFF000010u) == 0x54000000u)
        snprintf(out, n, "b.%s %ld", a64_cond_names[w & 15u],
                 (long)off + 4L * sext((w >> 5) & 0x7FFFFu, 19));
    else if ((w & 0x7E000000u) == 0x34000000u)
        snprintf(out, n, "%s %s, %ld", (w & 0x01000000u) ? "cbnz" : "cbz",
                 rn(a, sf, rd, 0), (long)off + 4L * sext((w >> 5) & 0x7FFFFu, 19));
    else if ((w & 0xFFFFFC1Fu) == 0xD63F0000u)
        snprintf(out, n, "blr x%u", r1);
    else if ((w & 0xFFFFFC1Fu) == 0xD61F0000u)
        snprintf(out, n, "br x%u", r1);
    else if ((w & 0xBF000000u) == 0x18000000u)
        snprintf(out, n, "ldr %s, %ld", rn(a, (int)((w >> 30) & 1u), rd, 0),
                 (long)off + 4L * sext((w >> 5) & 0x7FFFFu, 19));
    else if ((w & 0x3B000000u) == 0x39000000u) {
        /* LDR/STR (unsigned offset): size in 31:30, load in bit 22. */
        uint32_t size = w >> 30, scale = 1u << size;
        const char *mn = (w & 0x00400000u)
            ? (size == 0 ? "ldrb" : size == 1 ? "ldrh" : "ldr")
            : (size == 0 ? "strb" : size == 1 ? "strh" : "str");
        snprintf(out, n, "%s %s, [%s, #%u]", mn, rn(a, size == 3, rd, 0),
                 rn(b, 1, r1, 1), ((w >> 10) & 0xFFFu) * scale);
    } else if ((w & 0x3B200000u) == 0x38000000u) {
        /* LDUR/STUR, pre- and post-index. */
        uint32_t size = w >> 30, mode = (w >> 10) & 3u;
        int ld = (w & 0x00400000u) != 0;
        int32_t imm = sext((w >> 12) & 0x1FFu, 9);
        if (mode == 0)
            snprintf(out, n, "%s %s, [%s, #%d]", ld ? "ldur" : "stur",
                     rn(a, size == 3, rd, 0), rn(b, 1, r1, 1), (int)imm);
        else if (mode == 1)
            snprintf(out, n, "%s %s, [%s], #%d", ld ? "ldr" : "str",
                     rn(a, size == 3, rd, 0), rn(b, 1, r1, 1), (int)imm);
        else if (mode == 3)
            snprintf(out, n, "%s %s, [%s, #%d]!", ld ? "ldr" : "str",
                     rn(a, size == 3, rd, 0), rn(b, 1, r1, 1), (int)imm);
        else
            snprintf(out, n, ".word 0x%08x", w);
    } else if ((w & 0x3B200C00u) == 0x38200800u)
        snprintf(out, n, "%s %s, [%s, %s, uxtw]", (w & 0x00400000u) ? "ldr" : "str",
                 rn(a, (w >> 30) == 3, rd, 0), rn(b, 1, r1, 1), rn(c, 0, r2, 0));
    else if ((w & 0x7E000000u) == 0x28000000u && (w >> 30) == 2u) {
        /* STP/LDP of X registers: 23:22 index mode + load. */
        uint32_t mode = (w >> 23) & 3u, rt2 = (w >> 10) & 31u;
        int32_t imm = sext((w >> 15) & 0x7Fu, 7) * 8;
        const char *mn = (w & 0x00400000u) ? "ldp" : "stp";
        if (mode == 1)
            snprintf(out, n, "%s %s, %s, [%s], #%d", mn, rn(a, 1, rd, 0),
                     rn(b, 1, rt2, 0), rn(c, 1, r1, 1), (int)imm);
        else if (mode == 3)
            snprintf(out, n, "%s %s, %s, [%s, #%d]!", mn, rn(a, 1, rd, 0),
                     rn(b, 1, rt2, 0), rn(c, 1, r1, 1), (int)imm);
        else
            snprintf(out, n, "%s %s, %s, [%s, #%d]", mn, rn(a, 1, rd, 0),
                     rn(b, 1, rt2, 0), rn(c, 1, r1, 1), (int)imm);
    } else if ((w & 0x1F000000u) == 0x11000000u) {
        /* ADD/SUB(S) immediate; MOV to/from SP; CMP. */
        int sub = (w >> 30) & 1, s = (w >> 29) & 1;
        uint32_t imm = ((w >> 10) & 0xFFFu) << (((w >> 22) & 1u) ? 12 : 0);
        if (!sub && !s && imm == 0 && (rd == 31 || r1 == 31))
            snprintf(out, n, "mov %s, %s", rn(a, sf, rd, 1), rn(b, sf, r1, 1));
        else if (sub && s && rd == 31)
            snprintf(out, n, "cmp %s, #%u", rn(a, sf, r1, 1), imm);
        else
            snprintf(out, n, "%s%s %s, %s, #%u", sub ? "sub" : "add", s ? "s" : "",
                     rn(a, sf, rd, !s), rn(b, sf, r1, 1), imm);
    } else if ((w & 0x1F200000u) == 0x0B000000u) {
        /* ADD/SUB(S) shifted register (no shift emitted). */
        int sub = (w >> 30) & 1, s = (w >> 29) & 1;
        if (sub && s && rd == 31)
            snprintf(out, n, "cmp %s, %s", rn(a, sf, r1, 0), rn(b, sf, r2, 0));
        else
            snprintf(out, n, "%s%s %s, %s, %s", sub ? "sub" : "add", s ? "s" : "",
                     rn(a, sf, rd, 0), rn(b, sf, r1, 0), rn(c, sf, r2, 0));
    } else if ((w & 0xFFE0E000u) == 0x8B204000u)
        snprintf(out, n, "add %s, %s, %s, uxtw #%u", rn(a, 1, rd, 1),
                 rn(b, 1, r1, 1), rn(c, 0, r2, 0), (w >> 10) & 7u);
    else if ((w & 0x7FE0FFE0u) == 0x2A0003E0u)
        snprintf(out, n, "mov %s, %s", rn(a, sf, rd, 0), rn(b, sf, r2, 0));
    else if ((w & 0x7F800000u) == 0x53000000u) {
        /* UBFM: LSR (imms = 31/63) or LSL. */
        uint32_t immr = (w >> 16) & 63u, imms = (w >> 10) & 63u;
        uint32_t top = sf ? 63u : 31u;
        if (imms == top)
            snprintf(out, n, "lsr %s, %s, #%u", rn(a, sf, rd, 0), rn(b, sf, r1, 0), immr);
        else
            snprintf(out, n, "lsl %s, %s, #%u", rn(a, sf, rd, 0), rn(b, sf, r1, 0),
                     top - imms);
    } else if ((w & 0x7FE00C00u) == 0x1A800000u)
        snprintf(out, n, "csel %s, %s, %s, %s", rn(a, sf, rd, 0), rn(b, sf, r1, 0),
                 rn(c, sf, r2, 0), a64_cond_names[(w >> 12) & 15u]);
    else
        snprintf(out, n, ".word 0x%08x", w);
    (void)d;
}

void cl_jit_disassemble(const uint8_t *code, uint32_t len)
{
    uint32_t off;
    char mnem[80];
    char line[144];
    for (off = 0; off + 4 <= len; off += 4) {
        uint32_t w = (uint32_t)code[off] | ((uint32_t)code[off + 1] << 8)
                   | ((uint32_t)code[off + 2] << 16) | ((uint32_t)code[off + 3] << 24);
        disasm_word(w, off, mnem, sizeof mnem);
        snprintf(line, sizeof line, "  %04lu: %02X %02X %02X %02X        %s\n",
                 (unsigned long)off, code[off], code[off + 1], code[off + 2],
                 code[off + 3], mnem);
        cl_write_cstring_to_stdout(line);
    }
}

#endif /* JIT_A64 */
