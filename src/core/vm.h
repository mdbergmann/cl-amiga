#ifndef CL_VM_H
#define CL_VM_H

#include "types.h"

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
} CL_Frame;

typedef struct {
    CL_Obj stack[CL_VM_STACK_SIZE];
    int sp;              /* Stack pointer (next free slot) */
    CL_Frame frames[CL_VM_FRAME_SIZE];
    int fp;              /* Frame pointer (current frame index) */
} CL_VM;

extern CL_VM cl_vm;

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
