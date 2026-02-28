#include "vm.h"
#include "opcodes.h"
#include "symbol.h"
#include "mem.h"
#include "error.h"
#include "compiler.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <string.h>

CL_VM cl_vm;

void cl_vm_init(void)
{
    cl_vm.sp = 0;
    cl_vm.fp = 0;
}

void cl_vm_push(CL_Obj val)
{
    if (cl_vm.sp >= CL_VM_STACK_SIZE)
        cl_error(CL_ERR_OVERFLOW, "VM stack overflow");
    cl_vm.stack[cl_vm.sp++] = val;
}

CL_Obj cl_vm_pop(void)
{
    if (cl_vm.sp <= 0)
        cl_error(CL_ERR_OVERFLOW, "VM stack underflow");
    return cl_vm.stack[--cl_vm.sp];
}

CL_Obj cl_vm_apply(CL_Obj func, CL_Obj *args, int nargs)
{
    /*
     * Build a temporary bytecode stub:
     *   CONST func_idx      (3 bytes)
     *   CONST arg0_idx      (3 bytes per arg)
     *   ...
     *   CALL nargs           (2 bytes)
     *   HALT                 (1 byte)
     */
    uint8_t stub_code[256];
    CL_Obj stub_consts[64];
    int cp = 0, cc = 0, i;
    CL_Bytecode *bc;
    CL_Obj bc_obj, result;

    /* Constant 0 = function */
    stub_consts[cc] = func;
    stub_code[cp++] = OP_CONST;
    stub_code[cp++] = 0;
    stub_code[cp++] = (uint8_t)cc;
    cc++;

    /* Constants 1..nargs = arguments */
    for (i = 0; i < nargs && cc < 64; i++) {
        stub_consts[cc] = args[i];
        stub_code[cp++] = OP_CONST;
        stub_code[cp++] = 0;
        stub_code[cp++] = (uint8_t)cc;
        cc++;
    }

    stub_code[cp++] = OP_CALL;
    stub_code[cp++] = (uint8_t)nargs;
    stub_code[cp++] = OP_HALT;

    /* Allocate bytecode on the heap (arena) */
    bc = (CL_Bytecode *)cl_alloc(TYPE_BYTECODE, sizeof(CL_Bytecode));
    if (!bc) return CL_NIL;

    bc->code = (uint8_t *)platform_alloc(cp);
    if (bc->code) memcpy(bc->code, stub_code, cp);
    bc->code_len = cp;

    bc->constants = (CL_Obj *)platform_alloc(cc * sizeof(CL_Obj));
    if (bc->constants) {
        for (i = 0; i < cc; i++)
            bc->constants[i] = stub_consts[i];
    }
    bc->n_constants = cc;
    bc->arity = 0;
    bc->n_locals = 0;
    bc->n_upvalues = 0;
    bc->name = CL_NIL;

    bc_obj = CL_PTR_TO_OBJ(bc);
    result = cl_vm_eval(bc_obj);

    /* Free the temporary code/constants (bc itself is in the GC arena) */
    platform_free(bc->code);
    platform_free(bc->constants);
    bc->code = NULL;
    bc->constants = NULL;

    return result;
}

static uint16_t read_u16(uint8_t *code, uint32_t *ip)
{
    uint16_t val = (code[*ip] << 8) | code[*ip + 1];
    *ip += 2;
    return val;
}

static int16_t read_i16(uint8_t *code, uint32_t *ip)
{
    return (int16_t)read_u16(code, ip);
}

/* Call a built-in C function */
static CL_Obj call_builtin(CL_Function *func, CL_Obj *args, int nargs)
{
    if (nargs < func->min_args) {
        cl_error(CL_ERR_ARGS, "%s: too few arguments (got %d, need %d)",
                 CL_NULL_P(func->name) ? "?" : cl_symbol_name(func->name),
                 nargs, func->min_args);
    }
    if (func->max_args >= 0 && nargs > func->max_args) {
        cl_error(CL_ERR_ARGS, "%s: too many arguments (got %d, max %d)",
                 CL_NULL_P(func->name) ? "?" : cl_symbol_name(func->name),
                 nargs, func->max_args);
    }
    return func->func(args, nargs);
}

CL_Obj cl_vm_eval(CL_Obj bytecode_obj)
{
    CL_Bytecode *bc;
    CL_Frame *frame;
    uint8_t *code;
    CL_Obj *constants;
    uint32_t ip;
    int base_fp;

    if (CL_NULL_P(bytecode_obj)) return CL_NIL;

    /* Handle both bytecode and closure */
    if (CL_CLOSURE_P(bytecode_obj)) {
        CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(bytecode_obj);
        bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
    } else {
        bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bytecode_obj);
    }

    if (!bc || !bc->code) return CL_NIL;

    /* Set up initial frame */
    base_fp = cl_vm.fp;
    frame = &cl_vm.frames[cl_vm.fp++];
    frame->bytecode = bytecode_obj;
    frame->code = bc->code;
    frame->constants = bc->constants;
    frame->ip = 0;
    frame->bp = cl_vm.sp;
    frame->n_locals = bc->n_locals;

    /* Allocate space for locals */
    {
        int i;
        for (i = 0; i < bc->n_locals; i++)
            cl_vm_push(CL_NIL);
    }

    code = frame->code;
    constants = frame->constants;
    ip = 0;

    /* Main dispatch loop */
    for (;;) {
        uint8_t op = code[ip++];

        switch (op) {
        case OP_HALT: {
            CL_Obj result = (cl_vm.sp > (int)(frame->bp + frame->n_locals))
                            ? cl_vm_pop() : CL_NIL;
            /* Restore stack to before locals */
            cl_vm.sp = frame->bp;
            cl_vm.fp = base_fp;
            return result;
        }

        case OP_CONST: {
            uint16_t idx = read_u16(code, &ip);
            cl_vm_push(constants[idx]);
            break;
        }

        case OP_NIL:
            cl_vm_push(CL_NIL);
            break;

        case OP_T:
            cl_vm_push(SYM_T);
            break;

        case OP_LOAD: {
            uint8_t slot = code[ip++];
            cl_vm_push(cl_vm.stack[frame->bp + slot]);
            break;
        }

        case OP_STORE: {
            uint8_t slot = code[ip++];
            cl_vm.stack[frame->bp + slot] = cl_vm.stack[cl_vm.sp - 1];
            break;
        }

        case OP_GLOAD: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj sym = constants[idx];
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            if (s->value == CL_UNBOUND)
                cl_error(CL_ERR_UNBOUND, "Unbound variable: %s",
                         cl_symbol_name(sym));
            cl_vm_push(s->value);
            break;
        }

        case OP_GSTORE: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj sym = constants[idx];
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            s->value = cl_vm.stack[cl_vm.sp - 1];
            break;
        }

        case OP_FLOAD: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj sym = constants[idx];
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            if (s->function == CL_UNBOUND) {
                /* Fall back to value slot (for defun storing in value) */
                if (s->value == CL_UNBOUND)
                    cl_error(CL_ERR_UNDEFINED, "Undefined function: %s",
                             cl_symbol_name(sym));
                cl_vm_push(s->value);
            } else {
                cl_vm_push(s->function);
            }
            break;
        }

        case OP_UPVAL: {
            /* Flat upvalue access: single index into closure's upvalues[] */
            uint8_t index = code[ip++];
            if (CL_CLOSURE_P(frame->bytecode)) {
                CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                cl_vm_push(cl->upvalues[index]);
            } else {
                cl_vm_push(CL_NIL);
            }
            break;
        }

        case OP_POP:
            cl_vm_pop();
            break;

        case OP_DUP:
            cl_vm_push(cl_vm.stack[cl_vm.sp - 1]);
            break;

        case OP_CONS: {
            CL_Obj cdr_val = cl_vm_pop();
            CL_Obj car_val = cl_vm_pop();
            cl_vm_push(cl_cons(car_val, cdr_val));
            break;
        }

        case OP_CAR: {
            CL_Obj obj = cl_vm_pop();
            cl_vm_push(cl_car(obj));
            break;
        }

        case OP_CDR: {
            CL_Obj obj = cl_vm_pop();
            cl_vm_push(cl_cdr(obj));
            break;
        }

        case OP_ADD: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (!CL_FIXNUM_P(a) || !CL_FIXNUM_P(b))
                cl_error(CL_ERR_TYPE, "+: not a number");
            cl_vm_push(CL_MAKE_FIXNUM(CL_FIXNUM_VAL(a) + CL_FIXNUM_VAL(b)));
            break;
        }

        case OP_SUB: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (!CL_FIXNUM_P(a) || !CL_FIXNUM_P(b))
                cl_error(CL_ERR_TYPE, "-: not a number");
            cl_vm_push(CL_MAKE_FIXNUM(CL_FIXNUM_VAL(a) - CL_FIXNUM_VAL(b)));
            break;
        }

        case OP_MUL: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (!CL_FIXNUM_P(a) || !CL_FIXNUM_P(b))
                cl_error(CL_ERR_TYPE, "*: not a number");
            cl_vm_push(CL_MAKE_FIXNUM(CL_FIXNUM_VAL(a) * CL_FIXNUM_VAL(b)));
            break;
        }

        case OP_DIV: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (!CL_FIXNUM_P(a) || !CL_FIXNUM_P(b))
                cl_error(CL_ERR_TYPE, "/: not a number");
            if (CL_FIXNUM_VAL(b) == 0)
                cl_error(CL_ERR_DIVZERO, "Division by zero");
            cl_vm_push(CL_MAKE_FIXNUM(CL_FIXNUM_VAL(a) / CL_FIXNUM_VAL(b)));
            break;
        }

        case OP_EQ: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(a == b ? SYM_T : CL_NIL);
            break;
        }

        case OP_NUMEQ: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (!CL_FIXNUM_P(a) || !CL_FIXNUM_P(b))
                cl_error(CL_ERR_TYPE, "=: not a number");
            cl_vm_push(CL_FIXNUM_VAL(a) == CL_FIXNUM_VAL(b) ? SYM_T : CL_NIL);
            break;
        }

        case OP_LT: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (!CL_FIXNUM_P(a) || !CL_FIXNUM_P(b))
                cl_error(CL_ERR_TYPE, "<: not a number");
            cl_vm_push(CL_FIXNUM_VAL(a) < CL_FIXNUM_VAL(b) ? SYM_T : CL_NIL);
            break;
        }

        case OP_GT: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (!CL_FIXNUM_P(a) || !CL_FIXNUM_P(b))
                cl_error(CL_ERR_TYPE, ">: not a number");
            cl_vm_push(CL_FIXNUM_VAL(a) > CL_FIXNUM_VAL(b) ? SYM_T : CL_NIL);
            break;
        }

        case OP_LE: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (!CL_FIXNUM_P(a) || !CL_FIXNUM_P(b))
                cl_error(CL_ERR_TYPE, "<=: not a number");
            cl_vm_push(CL_FIXNUM_VAL(a) <= CL_FIXNUM_VAL(b) ? SYM_T : CL_NIL);
            break;
        }

        case OP_GE: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (!CL_FIXNUM_P(a) || !CL_FIXNUM_P(b))
                cl_error(CL_ERR_TYPE, ">=: not a number");
            cl_vm_push(CL_FIXNUM_VAL(a) >= CL_FIXNUM_VAL(b) ? SYM_T : CL_NIL);
            break;
        }

        case OP_NOT: {
            CL_Obj a = cl_vm_pop();
            cl_vm_push(CL_NULL_P(a) ? SYM_T : CL_NIL);
            break;
        }

        case OP_JMP: {
            int16_t offset = read_i16(code, &ip);
            ip += offset;
            break;
        }

        case OP_JNIL: {
            int16_t offset = read_i16(code, &ip);
            CL_Obj val = cl_vm_pop();
            if (CL_NULL_P(val)) ip += offset;
            break;
        }

        case OP_JTRUE: {
            int16_t offset = read_i16(code, &ip);
            CL_Obj val = cl_vm_pop();
            if (!CL_NULL_P(val)) ip += offset;
            break;
        }

        case OP_LIST: {
            uint8_t n = code[ip++];
            CL_Obj list = CL_NIL;
            int i;
            /* Build list from stack (last element is on top) */
            for (i = 0; i < n; i++) {
                list = cl_cons(cl_vm_pop(), list);
            }
            cl_vm_push(list);
            break;
        }

        case OP_CALL:
        case OP_TAILCALL: {
            uint8_t nargs = code[ip++];
            int is_tail = (op == OP_TAILCALL);
            CL_Obj *arg_base;
            CL_Obj func_obj;

            /* Stack: [func] [arg0] [arg1] ... [argN-1]
               func is below the args */
            arg_base = &cl_vm.stack[cl_vm.sp - nargs];
            func_obj = cl_vm.stack[cl_vm.sp - nargs - 1];

            if (CL_FUNCTION_P(func_obj)) {
                /* Built-in C function */
                CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func_obj);
                CL_Obj result = call_builtin(f, arg_base, nargs);
                cl_vm.sp -= (nargs + 1);
                cl_vm_push(result);
            } else if (CL_BYTECODE_P(func_obj) || CL_CLOSURE_P(func_obj)) {
                CL_Bytecode *callee_bc;
                CL_Frame *new_frame;
                int callee_arity, has_rest, i;

                if (CL_CLOSURE_P(func_obj)) {
                    CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(func_obj);
                    callee_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
                } else {
                    callee_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(func_obj);
                }

                callee_arity = callee_bc->arity & 0x7FFF;
                has_rest = callee_bc->arity & 0x8000;

                /* Check arity */
                if (!has_rest && nargs != callee_arity) {
                    cl_error(CL_ERR_ARGS, "Wrong number of arguments: expected %d, got %d",
                             callee_arity, nargs);
                }
                if (has_rest && nargs < callee_arity) {
                    cl_error(CL_ERR_ARGS, "Too few arguments: expected at least %d, got %d",
                             callee_arity, nargs);
                }

                if (is_tail && cl_vm.fp > base_fp + 1) {
                    /* Tail call: reuse current frame */
                    CL_Obj *src = arg_base;
                    cl_vm.sp = frame->bp;

                    /* Push args as new locals */
                    for (i = 0; i < nargs && i < callee_arity; i++)
                        cl_vm_push(src[i]);

                    /* Handle &rest */
                    if (has_rest) {
                        CL_Obj rest = CL_NIL;
                        int j;
                        for (j = nargs - 1; j >= callee_arity; j--)
                            rest = cl_cons(src[j], rest);
                        cl_vm_push(rest);
                    }

                    /* Fill remaining locals with NIL */
                    while (cl_vm.sp < (int)(frame->bp + callee_bc->n_locals))
                        cl_vm_push(CL_NIL);

                    frame->bytecode = func_obj;
                    frame->code = callee_bc->code;
                    frame->constants = callee_bc->constants;
                    frame->ip = 0;
                    frame->n_locals = callee_bc->n_locals;
                    code = callee_bc->code;
                    constants = callee_bc->constants;
                    ip = 0;
                } else {
                    /* Normal call: push new frame */
                    uint32_t new_bp;

                    /* Remove func_obj from under args; shift args down */
                    {
                        int j;
                        for (j = 0; j < nargs; j++)
                            cl_vm.stack[cl_vm.sp - nargs - 1 + j] =
                                cl_vm.stack[cl_vm.sp - nargs + j];
                        cl_vm.sp--;
                    }

                    new_bp = cl_vm.sp - nargs;

                    /* Handle &rest parameter */
                    if (has_rest) {
                        CL_Obj rest = CL_NIL;
                        int j;
                        for (j = nargs - 1; j >= callee_arity; j--) {
                            rest = cl_cons(cl_vm.stack[new_bp + j], rest);
                        }
                        cl_vm.sp = new_bp + callee_arity;
                        cl_vm_push(rest);
                    }

                    /* Fill remaining locals with NIL */
                    while (cl_vm.sp < (int)(new_bp + callee_bc->n_locals))
                        cl_vm_push(CL_NIL);

                    /* Save current frame state */
                    frame->ip = ip;

                    if (cl_vm.fp >= CL_VM_FRAME_SIZE)
                        cl_error(CL_ERR_OVERFLOW, "Call stack overflow");

                    new_frame = &cl_vm.frames[cl_vm.fp++];
                    new_frame->bytecode = func_obj;
                    new_frame->code = callee_bc->code;
                    new_frame->constants = callee_bc->constants;
                    new_frame->ip = 0;
                    new_frame->bp = new_bp;
                    new_frame->n_locals = callee_bc->n_locals;

                    frame = new_frame;
                    code = callee_bc->code;
                    constants = callee_bc->constants;
                    ip = 0;
                }
            } else {
                cl_error(CL_ERR_TYPE, "Not a function");
            }
            break;
        }

        case OP_RET: {
            CL_Obj result = (cl_vm.sp > (int)(frame->bp + frame->n_locals))
                            ? cl_vm_pop() : CL_NIL;

            /* Pop frame */
            cl_vm.sp = frame->bp;
            cl_vm.fp--;

            if (cl_vm.fp <= base_fp) {
                /* Returned from top-level call */
                return result;
            }

            /* Restore caller frame */
            frame = &cl_vm.frames[cl_vm.fp - 1];
            code = frame->code;
            constants = frame->constants;
            ip = frame->ip;

            cl_vm_push(result);
            break;
        }

        case OP_CLOSURE: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj tmpl = constants[idx];
            CL_Bytecode *tmpl_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(tmpl);
            int n_upvals = tmpl_bc->n_upvalues;
            CL_Closure *cl = (CL_Closure *)cl_alloc(TYPE_CLOSURE,
                sizeof(CL_Closure) + n_upvals * sizeof(CL_Obj));
            if (cl) {
                int i;
                cl->bytecode = tmpl;
                /* Read capture descriptors and populate upvalues */
                for (i = 0; i < n_upvals; i++) {
                    uint8_t is_local = code[ip++];
                    uint8_t cap_idx = code[ip++];
                    if (is_local) {
                        /* Capture from current frame's local slot */
                        cl->upvalues[i] = cl_vm.stack[frame->bp + cap_idx];
                    } else {
                        /* Capture from current closure's upvalue slot */
                        if (CL_CLOSURE_P(frame->bytecode)) {
                            CL_Closure *parent_cl = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                            cl->upvalues[i] = parent_cl->upvalues[cap_idx];
                        } else {
                            cl->upvalues[i] = CL_NIL;
                        }
                    }
                }
                cl_vm_push(CL_PTR_TO_OBJ(cl));
            } else {
                /* Skip capture descriptors even on alloc failure */
                ip += n_upvals * 2;
                cl_vm_push(CL_NIL);
            }
            break;
        }

        case OP_DEFMACRO: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj name = constants[idx];
            CL_Obj expander = cl_vm_pop();
            cl_register_macro(name, expander);
            cl_vm_push(name);
            break;
        }

        case OP_APPLY: {
            /* (apply func arglist) */
            CL_Obj arglist = cl_vm_pop();
            CL_Obj func_obj = cl_vm_pop();
            CL_Obj flat_args[64];
            int nflat = 0;
            CL_Obj result;

            /* Flatten arglist into C array */
            {
                CL_Obj a = arglist;
                while (!CL_NULL_P(a) && nflat < 64) {
                    flat_args[nflat++] = cl_car(a);
                    a = cl_cdr(a);
                }
            }

            if (CL_FUNCTION_P(func_obj)) {
                CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func_obj);
                result = call_builtin(f, flat_args, nflat);
            } else if (CL_BYTECODE_P(func_obj) || CL_CLOSURE_P(func_obj)) {
                result = cl_vm_apply(func_obj, flat_args, nflat);
            } else {
                cl_error(CL_ERR_TYPE, "APPLY: not a callable function");
                result = CL_NIL;
            }
            cl_vm_push(result);
            break;
        }

        default:
            cl_error(CL_ERR_GENERAL, "Unknown opcode: 0x%02x at ip=%u",
                     op, ip - 1);
            return CL_NIL;
        }
    }
}
