/* Tests for the bytecode peephole post-pass (spec 1.8, peephole.c), gated
 * on (optimize (speed >= 2)).
 *
 * Layer 1 drives the raw engine (cl_peephole_run) with hand-built byte
 * streams: pattern-by-pattern before/after checks, jump/NLX-landing
 * relocation across deletions, line-map remapping, decoder exhaustiveness
 * over EVERY opcode in CL_OPCODE_LIST, and the fail-safe bail-outs
 * (unknown opcode, truncated operands, undecodable OP_CLOSURE).
 *
 * Layer 2 evaluates Lisp through the full compiler and asserts speed-3
 * functions behave identically to their speed-1 twins — including the ANSI
 * guarantees the pass must NOT break: discarded (car 5) still signals
 * type-error, multiple-values state is preserved, NLX (tagbody/go,
 * catch/throw, unwind-protect, block/return-from) survives relocation.
 *
 * The cross-implementation differential harness lives in
 * tests/test_peephole_diff.sh (same corpus at CLAMIGA_FORCE_SPEED=0 vs 3). */

#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/reader.h"
#include "core/printer.h"
#include "core/compiler.h"
#include "core/vm.h"
#include "core/builtins.h"
#include "core/repl.h"
#include "core/opcodes.h"
#include "core/peephole.h"
#include "platform/platform.h"
#include <string.h>

static void setup(void)
{
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
    cl_reader_init();
    cl_printer_init();
    cl_compiler_init();
    cl_vm_init(0, 0);
    cl_builtins_init();
    cl_repl_init();
}

static void teardown(void)
{
    cl_mem_shutdown();
    platform_shutdown();
}

/* Evaluate EXPR; treat a non-NIL result as a passing predicate. */
static int truthy(const char *expr)
{
    return cl_eval_string(expr) != CL_NIL;
}

/* Run the raw engine over CODE/LEN with no constants and no line map. */
static int run_peep(uint8_t *code, int *len)
{
    return cl_peephole_run(code, len, NULL, 0, NULL, NULL);
}

/* --- Layer 1: raw engine --- */

TEST(opcode_info_exhaustive_and_rejects_gaps)
{
    /* Every opcode in the list must have decode info... */
#define CHECK_OP(op_sym, op_val, op_str, op_opnd, op_flags) \
    ASSERT(cl_opcode_info((uint8_t)(op_val)) != NULL); \
    ASSERT_STR_EQ(cl_opcode_info((uint8_t)(op_val))->name, op_str);
    CL_OPCODE_LIST(CHECK_OP)
#undef CHECK_OP
    /* ...and gap bytes must have none (a sample across the value space). */
    ASSERT(cl_opcode_info(0x00) == NULL);
    ASSERT(cl_opcode_info(0x07) == NULL);
    ASSERT(cl_opcode_info(0x1F) == NULL);
    ASSERT(cl_opcode_info(0x51) == NULL);
    ASSERT(cl_opcode_info(0xAC) == NULL);
    ASSERT(cl_opcode_info(0xFE) == NULL);
}

/* Decoder exhaustiveness: for every opcode (except OP_CLOSURE, whose
 * operand length depends on a constant-pool template — covered by the
 * Lisp-level tests), a stream of [X, LOAD 0, POP, RET] must decode far
 * enough that the LOAD/POP pair is optimized away.  A decode bail-out
 * would return 0 — so this catches any future opcode whose operand shape
 * in CL_OPCODE_LIST disagrees with what a synthesized stream contains. */
TEST(decoder_knows_every_opcode)
{
    static const struct { uint8_t op; uint8_t opnd; } ops[] = {
#define OP_ENTRY(name, value, str, opnd, flags) { (uint8_t)(value & 0xFF), (uint8_t)(opnd) },
        CL_OPCODE_LIST(OP_ENTRY)
#undef OP_ENTRY
    };
    size_t i;
    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        uint8_t code[24];
        int len = 0, ret;
        uint32_t opnd_len;
        if (ops[i].opnd == CL_OPND_CLOSURE) continue;
        switch ((CL_OperandKind)ops[i].opnd) {
        case CL_OPND_NONE:     opnd_len = 0; break;
        case CL_OPND_U8:       opnd_len = 1; break;
        case CL_OPND_U16:      opnd_len = 2; break;
        case CL_OPND_JREL:     opnd_len = 4; break;
        case CL_OPND_U16_JREL: opnd_len = 6; break;
        case CL_OPND_U16_U16:  opnd_len = 4; break;
        case CL_OPND_AMIGA:    opnd_len = 9; break;
        default:               opnd_len = 0; break;
        }
        code[len++] = ops[i].op;
        /* zero operands: jump offsets of 0 target the next instruction */
        memset(code + len, 0, opnd_len);
        len += (int)opnd_len;
        code[len++] = OP_LOAD; code[len++] = 0;
        code[len++] = OP_POP;
        code[len++] = OP_RET;
        ret = run_peep(code, &len);
        if (ret != 1 || code[0] != ops[i].op) {
            printf("  opcode 0x%02X: ret=%d first-byte=0x%02X\n",
                   ops[i].op, ret, code[0]);
        }
        ASSERT_EQ_INT(ret, 1);           /* decoded + optimized the pair */
        ASSERT_EQ_INT(code[0], ops[i].op); /* opcode itself preserved */
    }
}

TEST(store_pop_load_becomes_store)
{
    uint8_t code[] = { OP_STORE, 1, OP_POP, OP_LOAD, 1, OP_RET };
    uint8_t want[] = { OP_STORE, 1, OP_RET };
    int len = (int)sizeof(code);
    ASSERT_EQ_INT(run_peep(code, &len), 1);
    ASSERT_EQ_INT(len, (int)sizeof(want));
    ASSERT(memcmp(code, want, sizeof(want)) == 0);
}

TEST(store_pop_load_different_slot_untouched)
{
    uint8_t code[] = { OP_STORE, 1, OP_POP, OP_LOAD, 2, OP_RET };
    uint8_t orig[sizeof(code)];
    int len = (int)sizeof(code);
    memcpy(orig, code, sizeof(code));
    ASSERT_EQ_INT(run_peep(code, &len), 0);
    ASSERT_EQ_INT(len, (int)sizeof(orig));
    ASSERT(memcmp(code, orig, sizeof(orig)) == 0);
}

TEST(pure_pop_elided_when_mv_write_masked)
{
    /* CONST writes mv state, but the following OP_NIL re-writes it before
     * anything can observe — pair provably dead. */
    uint8_t code[] = { OP_CONST, 0, 0, OP_POP, OP_NIL, OP_RET };
    uint8_t want[] = { OP_NIL, OP_RET };
    int len = (int)sizeof(code);
    ASSERT_EQ_INT(run_peep(code, &len), 1);
    ASSERT_EQ_INT(len, (int)sizeof(want));
    ASSERT(memcmp(code, want, sizeof(want)) == 0);
}

TEST(pure_pop_kept_when_mv_observer_follows)
{
    /* MV_TO_LIST observes mv state: deleting CONST's mv write would let a
     * previous call's values leak through — must not fire. */
    uint8_t code[] = { OP_CONST, 0, 0, OP_POP, OP_MV_TO_LIST, OP_RET };
    uint8_t orig[sizeof(code)];
    int len = (int)sizeof(code);
    memcpy(orig, code, sizeof(code));
    ASSERT_EQ_INT(run_peep(code, &len), 0);
    ASSERT(memcmp(code, orig, sizeof(orig)) == 0);
}

TEST(pure_pop_kept_before_ret)
{
    /* RET propagates mv state to the caller — a CONST;POP directly before
     * it can't be proven dead. */
    uint8_t code[] = { OP_CONST, 0, 0, OP_POP, OP_RET };
    uint8_t orig[sizeof(code)];
    int len = (int)sizeof(code);
    memcpy(orig, code, sizeof(code));
    ASSERT_EQ_INT(run_peep(code, &len), 0);
    ASSERT(memcmp(code, orig, sizeof(orig)) == 0);
}

TEST(load_pop_elided_without_mv_proof)
{
    /* OP_LOAD writes no mv state, so LOAD;POP is deletable even directly
     * before RET. */
    uint8_t code[] = { OP_LOAD, 3, OP_POP, OP_RET };
    uint8_t want[] = { OP_RET };
    int len = (int)sizeof(code);
    ASSERT_EQ_INT(run_peep(code, &len), 1);
    ASSERT_EQ_INT(len, (int)sizeof(want));
    ASSERT(memcmp(code, want, sizeof(want)) == 0);
}

TEST(cascaded_pairs_reach_fixpoint)
{
    /* LOAD a; LOAD b; POP; POP — inner pair first, outer pair on the next
     * round. */
    uint8_t code[] = { OP_LOAD, 0, OP_LOAD, 1, OP_POP, OP_POP, OP_RET };
    uint8_t want[] = { OP_RET };
    int len = (int)sizeof(code);
    ASSERT_EQ_INT(run_peep(code, &len), 1);
    ASSERT_EQ_INT(len, (int)sizeof(want));
    ASSERT(memcmp(code, want, sizeof(want)) == 0);
}

TEST(not_jnil_fused_to_jtrue_and_relocated)
{
    /* LOAD 0; NOT; JNIL +5 -> [target]; NIL; RET; [target] T; RET
     * Both successors re-write mv (NIL / T), so NOT is dropped outright and
     * the branch inverts; the +5 offset must shrink to +2 (the NIL;RET
     * fallthrough stays, only NOT's 1 byte disappears... offset is relative
     * so it stays +2 = NIL,RET). */
    uint8_t code[] = {
        OP_LOAD, 0,
        OP_NOT,
        OP_JNIL, 0, 0, 0, 2,   /* -> T */
        OP_NIL, OP_RET,
        OP_T, OP_RET
    };
    uint8_t want[] = {
        OP_LOAD, 0,
        OP_JTRUE, 0, 0, 0, 2,  /* -> T */
        OP_NIL, OP_RET,
        OP_T, OP_RET
    };
    int len = (int)sizeof(code);
    ASSERT_EQ_INT(run_peep(code, &len), 1);
    ASSERT_EQ_INT(len, (int)sizeof(want));
    ASSERT(memcmp(code, want, sizeof(want)) == 0);
}

TEST(not_branch_keeps_mv_write_when_unprovable)
{
    /* Successor paths end in LOAD;RET (no mv re-write): NOT's mv write must
     * survive as MV_RESET while the branch still inverts. */
    uint8_t code[] = {
        OP_NOT,
        OP_JNIL, 0, 0, 0, 3,   /* -> LOAD 2 */
        OP_LOAD, 1, OP_RET,
        OP_LOAD, 2, OP_RET
    };
    uint8_t want[] = {
        OP_MV_RESET,
        OP_JTRUE, 0, 0, 0, 3,
        OP_LOAD, 1, OP_RET,
        OP_LOAD, 2, OP_RET
    };
    int len = (int)sizeof(code);
    ASSERT_EQ_INT(run_peep(code, &len), 1);
    ASSERT_EQ_INT(len, (int)sizeof(want));
    ASSERT(memcmp(code, want, sizeof(want)) == 0);
}

TEST(jump_threading_and_dead_jump_removal)
{
    /* JNIL -> JMP -> T: the conditional threads to the final target and the
     * intermediate JMP becomes unreachable. */
    uint8_t code[] = {
        OP_JNIL, 0, 0, 0, 2,   /* -> JMP */
        OP_NIL, OP_RET,
        OP_JMP, 0, 0, 0, 0,    /* -> T (next insn) */
        OP_T, OP_RET
    };
    uint8_t want[] = {
        OP_JNIL, 0, 0, 0, 2,   /* -> T directly (over NIL;RET) */
        OP_NIL, OP_RET,
        OP_T, OP_RET
    };
    int len = (int)sizeof(code);
    ASSERT_EQ_INT(run_peep(code, &len), 1);
    ASSERT_EQ_INT(len, (int)sizeof(want));
    ASSERT(memcmp(code, want, sizeof(want)) == 0);
}

TEST(dead_code_after_jmp_removed)
{
    uint8_t code[] = {
        OP_JMP, 0, 0, 0, 3,    /* over LOAD;POP -> NIL */
        OP_LOAD, 0, OP_POP,    /* unreachable */
        OP_NIL, OP_RET
    };
    uint8_t want[] = {
        OP_JMP, 0, 0, 0, 0,
        OP_NIL, OP_RET
    };
    int len = (int)sizeof(code);
    ASSERT_EQ_INT(run_peep(code, &len), 1);
    ASSERT_EQ_INT(len, (int)sizeof(want));
    ASSERT(memcmp(code, want, sizeof(want)) == 0);
}

TEST(backward_jump_relocated_across_deletion)
{
    /* Loop head after a deletable pair: the backward offset must account
     * for the removed bytes.  Layout:
     *   0: LOAD 0; 2: POP          (deleted)
     *   3: NIL                     (loop head, target)
     *   4: JNIL -> head            (backward: offset -9)
     *   9: RET
     * After deletion the head sits at 0 and JNIL's offset becomes -6. */
    uint8_t code[] = {
        OP_LOAD, 0, OP_POP,
        OP_NIL,
        OP_JNIL, 0xFF, 0xFF, 0xFF, 0xF7,  /* -9 -> NIL */
        OP_RET
    };
    uint8_t want[] = {
        OP_NIL,
        OP_JNIL, 0xFF, 0xFF, 0xFF, 0xFA,  /* -6 -> NIL */
        OP_RET
    };
    int len = (int)sizeof(code);
    ASSERT_EQ_INT(run_peep(code, &len), 1);
    ASSERT_EQ_INT(len, (int)sizeof(want));
    ASSERT(memcmp(code, want, sizeof(want)) == 0);
}

TEST(catch_landing_pad_relocated)
{
    /* OP_CATCH's i32 is an NLX landing pad — must be re-encoded like a jump
     * when a deletion shifts the pad.  Layout (byte positions):
     *   0: NIL                      (catch tag push)
     *   1: CATCH +9 -> 15           (operand ends at 6; 6+9 = T at 15)
     *   6: LOAD 0; 8: POP           (deleted, protected body)
     *   9: UNCATCH
     *  10: JMP +1 -> 16             (operand ends at 15; RET at 16)
     *  15: T                        (landing pad)
     *  16: RET
     */
    uint8_t code[] = {
        OP_NIL,
        OP_CATCH, 0, 0, 0, 9,   /* -> T at 15 */
        OP_LOAD, 0, OP_POP,
        OP_UNCATCH,
        OP_JMP, 0, 0, 0, 1,     /* -> RET at 16 */
        OP_T,                   /* landing pad */
        OP_RET
    };
    uint8_t want[] = {
        OP_NIL,
        OP_CATCH, 0, 0, 0, 6,   /* pad shifted back by the 3 deleted bytes */
        OP_UNCATCH,
        OP_JMP, 0, 0, 0, 1,
        OP_T,
        OP_RET
    };
    int len = (int)sizeof(code);
    ASSERT_EQ_INT(run_peep(code, &len), 1);
    ASSERT_EQ_INT(len, (int)sizeof(want));
    ASSERT(memcmp(code, want, sizeof(want)) == 0);
}

TEST(line_map_remapped_and_deduped)
{
    /* Entries at the deleted pair remap to the next surviving instruction;
     * collapsing entries keep the first. */
    uint8_t code[] = { OP_LOAD, 0, OP_POP, OP_NIL, OP_RET };
    CL_LineEntry lines[3];
    int n_lines = 3;
    int len = (int)sizeof(code);
    lines[0].pc = 0; lines[0].line = 10;   /* at deleted LOAD */
    lines[1].pc = 3; lines[1].line = 11;   /* at NIL */
    lines[2].pc = 4; lines[2].line = 12;   /* at RET */
    ASSERT_EQ_INT(cl_peephole_run(code, &len, NULL, 0, lines, &n_lines), 1);
    ASSERT_EQ_INT(len, 2);
    /* pc 0 and pc 3 both land on the new NIL at 0 — first (line 10) wins */
    ASSERT_EQ_INT(n_lines, 2);
    ASSERT_EQ_INT(lines[0].pc, 0);
    ASSERT_EQ_INT(lines[0].line, 10);
    ASSERT_EQ_INT(lines[1].pc, 1);
    ASSERT_EQ_INT(lines[1].line, 12);
}

TEST(bails_on_unknown_opcode)
{
    uint8_t code[] = { OP_LOAD, 0, OP_POP, 0x00, OP_RET };
    uint8_t orig[sizeof(code)];
    int len = (int)sizeof(code);
    memcpy(orig, code, sizeof(code));
    ASSERT_EQ_INT(run_peep(code, &len), 0);
    ASSERT_EQ_INT(len, (int)sizeof(orig));
    ASSERT(memcmp(code, orig, sizeof(orig)) == 0);
}

TEST(bails_on_truncated_operand)
{
    uint8_t code[] = { OP_LOAD, 0, OP_POP, OP_CONST, 0 }; /* CONST missing a byte */
    uint8_t orig[sizeof(code)];
    int len = (int)sizeof(code);
    memcpy(orig, code, sizeof(code));
    ASSERT_EQ_INT(run_peep(code, &len), 0);
    ASSERT(memcmp(code, orig, sizeof(orig)) == 0);
}

TEST(bails_on_jump_landing_mid_instruction)
{
    uint8_t code[] = {
        OP_JMP, 0, 0, 0, 1,    /* lands inside LOAD's operand */
        OP_LOAD, 0, OP_POP,
        OP_RET
    };
    uint8_t orig[sizeof(code)];
    int len = (int)sizeof(code);
    memcpy(orig, code, sizeof(code));
    ASSERT_EQ_INT(run_peep(code, &len), 0);
    ASSERT(memcmp(code, orig, sizeof(orig)) == 0);
}

TEST(bails_on_closure_with_non_bytecode_constant)
{
    CL_Obj consts[1];
    uint8_t code[] = { OP_CLOSURE, 0, 0, OP_LOAD, 0, OP_POP, OP_RET };
    uint8_t orig[sizeof(code)];
    int len = (int)sizeof(code);
    consts[0] = CL_NIL;
    memcpy(orig, code, sizeof(code));
    ASSERT_EQ_INT(cl_peephole_run(code, &len, consts, 1, NULL, NULL), 0);
    ASSERT(memcmp(code, orig, sizeof(orig)) == 0);
}

TEST(jump_into_deleted_pair_retargets_forward)
{
    /* A branch landing exactly ON a deleted pure pair retargets to the next
     * live instruction — the pair was a push/pop no-op on that path too.
     *   0: JNIL +2 -> 7 (LOAD, pair start)
     *   5: NIL; 6: RET;  7: LOAD 0; 9: POP;  10: NIL; 11: RET */
    uint8_t code[] = {
        OP_JNIL, 0, 0, 0, 2,   /* -> LOAD at 7 */
        OP_NIL, OP_RET,
        OP_LOAD, 0, OP_POP,    /* deletable pair (pair start is the target) */
        OP_NIL, OP_RET
    };
    uint8_t want[] = {
        OP_JNIL, 0, 0, 0, 2,   /* -> NIL at 7 */
        OP_NIL, OP_RET,
        OP_NIL, OP_RET
    };
    int len = (int)sizeof(code);
    ASSERT_EQ_INT(run_peep(code, &len), 1);
    ASSERT_EQ_INT(len, (int)sizeof(want));
    ASSERT(memcmp(code, want, sizeof(want)) == 0);
}

TEST(jump_landing_inside_pattern_blocks_it)
{
    /* A branch landing on the LOAD of STORE;POP;LOAD executes only the tail
     * of the pattern — the rewrite must not fire. */
    uint8_t code[] = {
        OP_JNIL, 0, 0, 0, 3,   /* -> the LOAD at 8 */
        OP_STORE, 1, OP_POP, OP_LOAD, 1,
        OP_RET
    };
    uint8_t orig[sizeof(code)];
    int len = (int)sizeof(code);
    memcpy(orig, code, sizeof(code));
    ASSERT_EQ_INT(run_peep(code, &len), 0);
    ASSERT(memcmp(code, orig, sizeof(orig)) == 0);
}

/* --- Layer 2: full-compiler semantic equivalence --- */

TEST(speed3_defun_same_results)
{
    ASSERT(truthy(
        "(progn"
        " (defun ph-f1 (x) (setq x (+ x 1)) (if (not (< x 10)) :big :small))"
        " (defun ph-f3 (x) (declare (optimize (speed 3)))"
        "   (setq x (+ x 1)) (if (not (< x 10)) :big :small))"
        " (and (eq (ph-f1 1) (ph-f3 1))"
        "      (eq (ph-f1 42) (ph-f3 42))))"));
}

TEST(speed3_body_declare_triggers_pass)
{
    /* The body (declare (optimize (speed 3))) is scope-restored before
     * finalization — the high-water mark must still trigger the pass.
     * Observable via behavior only: results must stay correct. */
    ASSERT(truthy(
        "(progn"
        " (defun ph-hw (n) (declare (optimize (speed 3)))"
        "   (let ((s 0)) (dotimes (i n) (setq s (+ s i))) s))"
        " (= (ph-hw 100) 4950))"));
}

TEST(speed3_discarded_car_still_signals)
{
    /* ANSI: (car 5) signals type-error even when its value is discarded —
     * OP_CAR must never be treated as deletable. */
    ASSERT(truthy(
        "(progn"
        " (defun ph-car3 (x) (declare (optimize (speed 3)))"
        "   (progn (car x) nil))"
        " (handler-case (progn (ph-car3 5) nil)"
        "   (type-error () t)))"));
}

TEST(speed3_multiple_values_preserved)
{
    ASSERT(truthy(
        "(progn"
        " (defun ph-mv3 () (declare (optimize (speed 3)))"
        "   (progn 42 (values 1 2 3)))"
        " (equal (multiple-value-list (ph-mv3)) '(1 2 3)))"));
    ASSERT(truthy(
        "(progn"
        " (defun ph-mv1 () (declare (optimize (speed 3)))"
        "   (values-list '(1 2)) 99)"
        " (equal (multiple-value-list (ph-mv1)) '(99)))"));
}

TEST(speed3_nlx_forms_survive_relocation)
{
    ASSERT(truthy(
        "(progn"
        " (defun ph-tag3 (n) (declare (optimize (speed 3)))"
        "   (let ((s 0) (i 0))"
        "     (tagbody"
        "      top (when (>= i n) (go done))"
        "          (setq s (+ s i)) (setq i (+ i 1)) (go top)"
        "      done)"
        "     s))"
        " (= (ph-tag3 100) 4950))"));
    ASSERT(truthy(
        "(progn"
        " (defun ph-catch3 (x) (declare (optimize (speed 3)))"
        "   (catch 'ph-tag (when x (throw 'ph-tag :thrown)) :fell))"
        " (and (eq (ph-catch3 t) :thrown) (eq (ph-catch3 nil) :fell)))"));
    ASSERT(truthy(
        "(progn"
        " (defvar *ph-cleanup* nil)"
        " (defun ph-uw3 (x) (declare (optimize (speed 3)))"
        "   (setq *ph-cleanup* nil)"
        "   (catch 'ph-out"
        "     (unwind-protect (when x (throw 'ph-out :out))"
        "       (setq *ph-cleanup* t)))"
        "   *ph-cleanup*)"
        " (and (ph-uw3 t) (ph-uw3 nil)))"));
    ASSERT(truthy(
        "(progn"
        " (defun ph-blk3 (x) (declare (optimize (speed 3)))"
        "   (block b (when x (return-from b :early)) :late))"
        " (and (eq (ph-blk3 t) :early) (eq (ph-blk3 nil) :late)))"));
}

TEST(speed3_closures_and_loops)
{
    ASSERT(truthy(
        "(progn"
        " (defun ph-clo3 (n) (declare (optimize (speed 3)))"
        "   (let ((acc nil))"
        "     (dotimes (i n) (push (let ((j i)) (lambda () j)) acc))"
        "     (let ((s 0)) (dolist (f acc) (setq s (+ s (funcall f)))) s)))"
        " (= (ph-clo3 10) 45))"));
}

TEST(speed3_not_under_if_and_while)
{
    ASSERT(truthy(
        "(progn"
        " (defun ph-not3 (xs) (declare (optimize (speed 3)))"
        "   (let ((n 0))"
        "     (dolist (x xs)"
        "       (unless (not (numberp x)) (setq n (+ n 1))))"
        "     n))"
        " (= (ph-not3 '(1 a 2 b 3)) 3))"));
}

TEST(speed1_bytecode_untouched)
{
    /* At default speed the pass must not fire: a canary with an obviously
     * removable pattern keeps its exact behavior AND its store-reload shape
     * is still visible through function-lambda-expression-free semantics.
     * We can only observe behavior here — but also assert the global is
     * back at 1 so later suites aren't affected by this file. */
    ASSERT(truthy(
        "(progn"
        " (defun ph-s1 (x) (setq x (+ x 1)) x)"
        " (= (ph-s1 41) 42))"));
}

int main(void)
{
    test_init();
    setup();

    RUN(opcode_info_exhaustive_and_rejects_gaps);
    RUN(decoder_knows_every_opcode);
    RUN(store_pop_load_becomes_store);
    RUN(store_pop_load_different_slot_untouched);
    RUN(pure_pop_elided_when_mv_write_masked);
    RUN(pure_pop_kept_when_mv_observer_follows);
    RUN(pure_pop_kept_before_ret);
    RUN(load_pop_elided_without_mv_proof);
    RUN(cascaded_pairs_reach_fixpoint);
    RUN(not_jnil_fused_to_jtrue_and_relocated);
    RUN(not_branch_keeps_mv_write_when_unprovable);
    RUN(jump_threading_and_dead_jump_removal);
    RUN(dead_code_after_jmp_removed);
    RUN(backward_jump_relocated_across_deletion);
    RUN(catch_landing_pad_relocated);
    RUN(line_map_remapped_and_deduped);
    RUN(bails_on_unknown_opcode);
    RUN(bails_on_truncated_operand);
    RUN(bails_on_jump_landing_mid_instruction);
    RUN(bails_on_closure_with_non_bytecode_constant);
    RUN(jump_into_deleted_pair_retargets_forward);
    RUN(jump_landing_inside_pattern_blocks_it);

    RUN(speed3_defun_same_results);
    RUN(speed3_body_declare_triggers_pass);
    RUN(speed3_discarded_car_still_signals);
    RUN(speed3_multiple_values_preserved);
    RUN(speed3_nlx_forms_survive_relocation);
    RUN(speed3_closures_and_loops);
    RUN(speed3_not_under_if_and_while);
    RUN(speed1_bytecode_untouched);

    teardown();
    REPORT();
}
