#ifndef CL_VM_H
#define CL_VM_H

#include "types.h"
#include <setjmp.h>

/*
 * Stack-based bytecode interpreter.
 */

#define CL_VM_STACK_SIZE  (16 * 1024)  /* 16K entries = 64KB */
#define CL_VM_FRAME_SIZE  1024         /* Max call frames */

/* Call frame */
typedef struct {
    CL_Obj bytecode;     /* Current CL_Bytecode or CL_Closure */
    uint8_t *code;       /* Code pointer */
    CL_Obj *constants;   /* Constants pool */
    uint32_t ip;         /* Instruction pointer */
    uint32_t bp;         /* Base pointer (locals start) */
    int n_locals;
    uint8_t nargs;       /* Actual argument count (for OP_ARGC) */
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
    int gc_root_mark;      /* GC root stack depth at frame creation */
    void *compiler_mark;   /* active compiler chain head at frame creation */
    int mv_count;          /* multiple value count to preserve across NLX */
    CL_Obj mv_values[CL_MAX_MV]; /* multiple values to preserve across NLX */
} CL_NLXFrame;

/* --- Condition handler binding stack --- */

typedef struct {
    CL_Obj type_name;     /* Condition type symbol to match */
    CL_Obj handler;       /* Handler function (closure or function) */
    int handler_mark;     /* Handler stack depth when this binding was established */
} CL_HandlerBinding;

#define CL_MAX_HANDLER_BINDINGS 64
/* --- Restart binding stack --- */

typedef struct {
    CL_Obj name;        /* Restart name symbol (ABORT, CONTINUE, etc.) or NIL */
    CL_Obj handler;     /* Closure to invoke */
    CL_Obj tag;         /* Internal catch tag for transfer of control */
} CL_RestartBinding;

#define CL_MAX_RESTART_BINDINGS 64
/* Signal a condition — walks handler stack, returns NIL if no handler transferred */
CL_Obj cl_signal_condition(CL_Obj condition);

/* Throw a value to a catch tag (for restart invocation from debugger) */
void cl_throw_to_tag(CL_Obj tag, CL_Obj value);

/* Create a condition from an error code and message */
CL_Obj cl_create_condition_from_error(int code, const char *msg);

/* --- Backtrace --- */
#define CL_BACKTRACE_BUF_SIZE 2048

/* Capture current VM call stack into cl_backtrace_buf */
void cl_capture_backtrace(void);

/* Initialize VM (0 = use default for either parameter) */
void cl_vm_init(uint32_t stack_size, int frame_size);

/* Shutdown VM (free dynamic allocations) */
void cl_vm_shutdown(void);

/* Execute a bytecode object, return result */
CL_Obj cl_vm_eval(CL_Obj bytecode);

/* Apply a function to arguments (builds temp bytecode, calls cl_vm_eval) */
CL_Obj cl_vm_apply(CL_Obj func, CL_Obj *args, int nargs);

/* Push/pop on VM value stack (for builtins) */
void cl_vm_push(CL_Obj val);
CL_Obj cl_vm_pop(void);

/* C stack overflow detection */
void cl_check_c_stack(const char *context);

#include "thread.h"

#endif /* CL_VM_H */
