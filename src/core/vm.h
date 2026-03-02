#ifndef CL_VM_H
#define CL_VM_H

#include "types.h"
#include <setjmp.h>

/*
 * Stack-based bytecode interpreter.
 */

#define CL_VM_STACK_SIZE  (16 * 1024)  /* 16K entries = 64KB */
#define CL_VM_FRAME_SIZE  256          /* Max call frames */

/* Call frame */
typedef struct {
    CL_Obj bytecode;     /* Current CL_Bytecode or CL_Closure */
    uint8_t *code;       /* Code pointer */
    CL_Obj *constants;   /* Constants pool */
    uint32_t ip;         /* Instruction pointer */
    uint32_t bp;         /* Base pointer (locals start) */
    int n_locals;
    uint8_t nargs;       /* Actual argument count (for OP_ARGC) */
} CL_Frame;

typedef struct {
    CL_Obj stack[CL_VM_STACK_SIZE];
    int sp;              /* Stack pointer (next free slot) */
    CL_Frame frames[CL_VM_FRAME_SIZE];
    int fp;              /* Frame pointer (current frame index) */
} CL_VM;

extern CL_VM cl_vm;

/* --- Dynamic (special) variable binding stack --- */

typedef struct {
    CL_Obj symbol;
    CL_Obj old_value;
} CL_DynBinding;

#define CL_MAX_DYN_BINDINGS 256

extern CL_DynBinding cl_dyn_stack[CL_MAX_DYN_BINDINGS];
extern int cl_dyn_top;

/* Restore dynamic bindings down to mark */
void cl_dynbind_restore_to(int mark);

/* --- Non-Local eXit (NLX) stack for catch/throw and unwind-protect --- */

#define CL_NLX_CATCH   0
#define CL_NLX_UWPROT  1
#define CL_MAX_NLX_FRAMES 32

typedef struct {
    uint8_t type;          /* CL_NLX_CATCH or CL_NLX_UWPROT */
    jmp_buf buf;
    int vm_sp;
    int vm_fp;
    CL_Obj tag;            /* catch tag (CATCH only) */
    CL_Obj result;         /* value to propagate */
    uint32_t catch_ip;     /* IP after the CATCH/UWPROT instruction */
    int16_t offset;        /* jump offset to landing */
    uint8_t *code;         /* code pointer to restore */
    CL_Obj *constants;     /* constants pointer to restore */
    int base_fp;           /* base_fp of the cl_vm_eval call */
    int dyn_mark;          /* binding stack depth at frame creation */
    int handler_mark;      /* handler stack depth at frame creation */
} CL_NLXFrame;

extern CL_NLXFrame cl_nlx_stack[CL_MAX_NLX_FRAMES];
extern int cl_nlx_top;

/* Pending throw state (for unwind-protect re-throw) */
extern int cl_pending_throw;      /* 0=none, 1=throw, 2=error */
extern CL_Obj cl_pending_tag;
extern CL_Obj cl_pending_value;
extern int cl_pending_error_code;
extern char cl_pending_error_msg[512];

/* --- Condition handler binding stack --- */

typedef struct {
    CL_Obj type_name;     /* Condition type symbol to match */
    CL_Obj handler;       /* Handler function (closure or function) */
    int handler_mark;     /* Handler stack depth when this binding was established */
} CL_HandlerBinding;

#define CL_MAX_HANDLER_BINDINGS 64
extern CL_HandlerBinding cl_handler_stack[CL_MAX_HANDLER_BINDINGS];
extern int cl_handler_top;

/* Signal a condition — walks handler stack, returns NIL if no handler transferred */
CL_Obj cl_signal_condition(CL_Obj condition);

/* Create a condition from an error code and message */
CL_Obj cl_create_condition_from_error(int code, const char *msg);

/* --- Multiple Values --- */
#define CL_MAX_MV 20
extern CL_Obj cl_mv_values[CL_MAX_MV];
extern int cl_mv_count;

/* --- Trace --- */
extern int cl_trace_depth;
extern int cl_trace_count;

/* --- Backtrace --- */
#define CL_BACKTRACE_BUF_SIZE 2048
extern char cl_backtrace_buf[CL_BACKTRACE_BUF_SIZE];

/* Capture current VM call stack into cl_backtrace_buf */
void cl_capture_backtrace(void);

/* Initialize VM */
void cl_vm_init(void);

/* Execute a bytecode object, return result */
CL_Obj cl_vm_eval(CL_Obj bytecode);

/* Apply a function to arguments (builds temp bytecode, calls cl_vm_eval) */
CL_Obj cl_vm_apply(CL_Obj func, CL_Obj *args, int nargs);

/* Push/pop on VM value stack (for builtins) */
void cl_vm_push(CL_Obj val);
CL_Obj cl_vm_pop(void);

#endif /* CL_VM_H */
