#ifndef CL_VM_H
#define CL_VM_H

#include "types.h"
#include "printer.h"
#include <setjmp.h>
#include <stdio.h>

/*
 * Stack-based bytecode interpreter.
 */

#define CL_VM_STACK_SIZE  (16 * 1024)  /* 16K entries = 64KB */
#define CL_VM_FRAME_SIZE  1024         /* Max call frames */

/* CALL-ARGUMENTS-LIMIT: the most arguments OP_APPLY will spread onto the VM
 * stack for a single call.  Independent of LAMBDA-PARAMETERS-LIMIT (the cap on
 * *declared* parameters) because a &rest function accepts arbitrarily many
 * actual arguments.  Bounded well below CL_VM_STACK_SIZE so a large APPLY plus
 * the callee's frame/locals cannot overflow the stack. */
#define CL_CALL_ARGS_LIMIT  4096

/* Call frame */
typedef struct {
    CL_Obj bytecode;     /* Current CL_Bytecode or CL_Closure */
    uint8_t *code;       /* Code pointer */
    CL_Obj *constants;   /* Constants pool */
    uint32_t ip;         /* Instruction pointer */
    uint32_t bp;         /* Base pointer (locals start) */
    int n_locals;
    uint16_t nargs;      /* Actual argument count (for OP_ARGC); widened to
                            uint16_t so APPLY can spread > 255 args (up to
                            CL_CALL_ARGS_LIMIT) to a &rest function. */
    uint8_t stub_code[3]; /* For cl_vm_apply stub frames: OP_CALL,nargs,OP_HALT.
                             Embedded here so it survives longjmp past the
                             cl_vm_apply C frame (stack-use-after-return fix). */
    int nlx_level;       /* NLX stack depth when this frame was entered.
                            Used by OP_TAILCALL to pop BLOCK/CATCH frames
                            from the current function before reusing the frame. */
} CL_Frame;

typedef struct {
    CL_Obj *stack;
    uint32_t stack_size; /* Number of entries */
    int sp;              /* Stack pointer (next free slot) */
    CL_Frame *frames;
    int frame_size;      /* Max call frames */
    int fp;              /* Frame pointer (current frame index) */
} CL_VM;

/* --- Dynamic (special) variable binding stack --- */

typedef struct {
    CL_Obj symbol;
    CL_Obj old_value;
} CL_DynBinding;

#define CL_MAX_DYN_BINDINGS 4096

/* Restore dynamic bindings down to mark */
void cl_dynbind_restore_to(int mark);

/* --- Multiple Values --- */
#define CL_MAX_MV 20

/* --- Non-Local eXit (NLX) stack for catch/throw and unwind-protect --- */

#define CL_NLX_CATCH    0
#define CL_NLX_UWPROT   1
#define CL_NLX_BLOCK    2
#define CL_NLX_TAGBODY  3
#define CL_MAX_NLX_FRAMES 2048

typedef struct {
    uint8_t type;          /* CL_NLX_CATCH or CL_NLX_UWPROT */
    jmp_buf buf;
    CL_JMPBUF_GUARD        /* MorphOS PPC setjmp overrun guard — see types.h */
    int vm_sp;
    int vm_fp;
    CL_Obj tag;            /* catch tag (CATCH only) */
    CL_Obj result;         /* value to propagate */
    uint32_t catch_ip;     /* IP after the CATCH/UWPROT instruction */
    int32_t offset;        /* jump offset to landing */
    uint8_t *code;         /* code pointer to restore */
    CL_Obj *constants;     /* constants pointer to restore */
    CL_Obj bytecode;       /* frame->bytecode at NLX push time */
    int base_fp;           /* base_fp of the cl_vm_eval call */
    int dyn_mark;          /* binding stack depth at frame creation */
    int handler_mark;      /* handler stack depth at frame creation */
    int restart_mark;      /* restart stack depth at frame creation */
    int error_mark;        /* C-level error-frame (CL_CATCH) stack depth at
                            * frame creation.  Restored when this NLX frame is
                            * unwound to, so error frames pushed in C functions
                            * deeper than this NLX — abandoned by the longjmp —
                            * are dropped.  Without this, a later cl_error /
                            * rethrow longjmps to a stale error frame whose
                            * setjmp SP points into an already-unwound (now
                            * reused) stack region, corrupting a live caller's
                            * frame (observed: COMPILE-FILE canary smash when an
                            * error unwinds through an unwind-protect during a
                            * compile-time eval). */
    int gc_root_mark;      /* GC root stack depth at frame creation */
    void *compiler_mark;   /* active compiler chain head at frame creation */
    uint64_t handler_active_mask; /* cl_handler_active_mask snapshot at frame
                            * creation.  Restored when this NLX frame is unwound
                            * to, so a CLHS 9.1.4 disabled-handler band set by a
                            * running handler is re-enabled when that handler
                            * exits via a non-local transfer through here (e.g.
                            * INVOKE-RESTART out of a HANDLER-BIND handler). */
    int mv_count;          /* multiple value count to preserve across NLX */
    CL_Obj mv_values[CL_MAX_MV]; /* multiple values to preserve across NLX */
    int saved_pending_mark; /* saved_pending_top depth at frame creation */
    int saved_jit_depth;   /* CT->jit_depth at frame creation.  Restored on
                            * every longjmp landing (like the CL_ErrorFrame
                            * twin): a THROW/RETURN-FROM/GO that unwinds past
                            * cl_jit_invoke skips its C epilogue, and a stale
                            * jit_depth makes the next JIT entry keep a stale
                            * jit_stack_top — the conservative GC scan window
                            * then excludes live JIT frames (missed roots). */
    CL_PrinterState printer_mark; /* printer flags at frame creation.  Restored
                            * on the longjmp landing so a THROW out of a print
                            * hook / pprint-dispatch fn can't leak pr_depth,
                            * pr_inprog_top, pr_pprint_dispatch_active or
                            * pr_circle_active (see printer.h; mirrors the
                            * CL_ErrorFrame.saved_printer twin). */
} CL_NLXFrame;

/* --- Condition handler binding stack --- */

typedef struct {
    CL_Obj type_name;     /* Condition type symbol to match */
    CL_Obj handler;       /* Handler function (closure or function) */
    int handler_mark;     /* Handler stack depth when this binding was established */
    /* The per-binding enabled/disabled state (CLHS 9.1.4) now lives in the
     * per-thread cl_handler_active_mask (bit j = binding j enabled), so it can
     * be snapshot/restored across non-local exits.  See thread.h. */
} CL_HandlerBinding;

#define CL_MAX_HANDLER_BINDINGS 64

/* Bit mask for handler indices [lo, hi) (used to disable a band in
 * cl_handler_active_mask).  hi may equal CL_MAX_HANDLER_BINDINGS (64); shifting
 * by 64 is undefined, so clamp that to the all-ones upper bound. */
#define CL_HANDLER_BAND_MASK(lo, hi) \
    ((((hi) >= 64) ? ~(uint64_t)0 : (((uint64_t)1 << (hi)) - 1)) \
     & ~(((uint64_t)1 << (lo)) - 1))
/* --- Restart binding stack --- */

typedef struct {
    CL_Obj name;        /* Restart name symbol (ABORT, CONTINUE, etc.) or NIL */
    CL_Obj handler;     /* Closure to invoke */
    CL_Obj tag;         /* Internal catch tag for transfer of control */
    CL_Obj restart;     /* First-class restart object (TYPE_RESTART) */
} CL_RestartBinding;

#define CL_MAX_RESTART_BINDINGS 64
/* Signal a condition — walks handler stack, returns NIL if no handler transferred */
CL_Obj cl_signal_condition(CL_Obj condition);

/* COMPILE warning/failure detection — see builtins_condition.c.  While
 * cl_compile_detect_depth > 0, signaled warnings/errors set these flags so
 * COMPILE can return warnings-p / failure-p. */
extern int cl_compile_detect_depth;
extern int cl_compile_warnings_p;
extern int cl_compile_failure_p;

/* Throw a value to a catch tag (for restart invocation from debugger) */
void cl_throw_to_tag(CL_Obj tag, CL_Obj value);

/* Create a condition from an error code and message */
CL_Obj cl_create_condition_from_error(int code, const char *msg);

/* --- Backtrace --- */
#define CL_BACKTRACE_BUF_SIZE 2048

/* Capture current VM call stack into cl_backtrace_buf */
void cl_capture_backtrace(void);

/* --- Structured backtrace introspection (Sly/SLYNK SLDB backend) ---
 *
 * These walk the error-time frame window (cl_debug_base_fp, snapshotted by
 * cl_capture_backtrace; falls back to the live frame pointer outside an
 * error).  Frame index 0 is the innermost frame. */

/* Backtrace as a list of (INDEX NAME FILE LINE) entries, innermost first.
 * NAME is the function-name symbol (NIL if anonymous); FILE a namestring
 * string (NIL if unknown); LINE a 1-based fixnum (NIL if unknown).
 * MAX_FRAMES <= 0 means "all". */
CL_Obj cl_vm_backtrace_list(int max_frames);

/* Locals of frame INDEX as a list of (NAME . VALUE), with placeholder names
 * (#:ARGn for argument slots, #:LOCALn for the rest).  Returns :NOT-AVAILABLE
 * for an out-of-range index. */
CL_Obj cl_vm_frame_locals(int index);

/* Initialize VM (0 = use default for either parameter) */
void cl_vm_init(uint32_t stack_size, int frame_size);

/* Shutdown VM (free dynamic allocations) */
void cl_vm_shutdown(void);

/* Execute a bytecode object, return result */
CL_Obj cl_vm_eval(CL_Obj bytecode);

/* Apply a function to arguments (builds temp bytecode, calls cl_vm_eval) */
CL_Obj cl_vm_apply(CL_Obj func, CL_Obj *args, int nargs);

/* Unwrap a funcallable instance (standard-generic-function struct) to its
 * discriminating function. Returns the input unchanged for other types. */
CL_Obj cl_unwrap_funcallable(CL_Obj obj);

/* True if OBJ is a funcallable instance (i.e. a GF struct). */
int cl_funcallable_instance_p(CL_Obj obj);

/* Register SYM (a struct-type name symbol) as a funcallable generic-function
 * type, so structs of that type are recognized by cl_funcallable_instance_p.
 * Used to support custom generic-function metaclasses (CLHS
 * :generic-function-class) — subclasses of STANDARD-GENERIC-FUNCTION whose
 * instances must still dispatch through their discriminating-function slot.
 * Idempotent; STANDARD-GENERIC-FUNCTION is always registered. */
void cl_register_funcallable_gf_type(CL_Obj sym);

/* Push/pop on VM value stack (for builtins) */
void cl_vm_push(CL_Obj val);
CL_Obj cl_vm_pop(void);

/* C stack overflow detection */
void cl_check_c_stack(const char *context);

/* Heuristic sanity check on a builtin's native function pointer (used before
 * dispatch to catch a clobbered func slot).  Returns 1 if plausible, 0 if it
 * should be rejected as corrupt.  Exposed for unit testing — see vm.c for the
 * platform rationale (notably: host function pointers are NOT 4-byte aligned). */
int cl_vm_builtin_fptr_plausible(const void *fptr);

/* Opcode-frequency profiler. Counters only increment when built with
 * -DPROFILE_OPCODES; dump still works (prints "not compiled in" message). */
void cl_op_counts_reset(void);
void cl_op_counts_dump(FILE *out);

#include "thread.h"

#endif /* CL_VM_H */
