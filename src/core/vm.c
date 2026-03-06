#include "vm.h"
#include "opcodes.h"
#include "symbol.h"
#include "mem.h"
#include "bignum.h"
#include "ratio.h"
#include "float.h"
#include "error.h"
#include "compiler.h"
#include "printer.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

CL_VM cl_vm;

/* Multiple values */
CL_Obj cl_mv_values[CL_MAX_MV];
int cl_mv_count = 1;

/* Trace */
int cl_trace_depth = 0;
int cl_trace_count = 0;

/* Backtrace */
char cl_backtrace_buf[CL_BACKTRACE_BUF_SIZE];

/* NLX stack */
CL_NLXFrame cl_nlx_stack[CL_MAX_NLX_FRAMES];
int cl_nlx_top = 0;

/* Dynamic binding stack */
CL_DynBinding cl_dyn_stack[CL_MAX_DYN_BINDINGS];
int cl_dyn_top = 0;

/* Handler binding stack */
CL_HandlerBinding cl_handler_stack[CL_MAX_HANDLER_BINDINGS];
int cl_handler_top = 0;

/* Restart binding stack */
CL_RestartBinding cl_restart_stack[CL_MAX_RESTART_BINDINGS];
int cl_restart_top = 0;

void cl_dynbind_restore_to(int mark)
{
    while (cl_dyn_top > mark) {
        cl_dyn_top--;
        {
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(cl_dyn_stack[cl_dyn_top].symbol);
            s->value = cl_dyn_stack[cl_dyn_top].old_value;
        }
    }
}

/* Pending throw state */
int cl_pending_throw = 0;
CL_Obj cl_pending_tag = 0;
CL_Obj cl_pending_value = 0;
int cl_pending_error_code = 0;
char cl_pending_error_msg[512];

void cl_vm_init(uint32_t stack_size, int frame_size)
{
    if (stack_size == 0) stack_size = CL_VM_STACK_SIZE;
    if (frame_size == 0) frame_size = CL_VM_FRAME_SIZE;

    cl_vm.stack = (CL_Obj *)platform_alloc(stack_size * sizeof(CL_Obj));
    cl_vm.stack_size = stack_size;
    cl_vm.frames = (CL_Frame *)platform_alloc(frame_size * sizeof(CL_Frame));
    cl_vm.frame_size = frame_size;

    cl_vm.sp = 0;
    cl_vm.fp = 0;
    cl_nlx_top = 0;
    cl_dyn_top = 0;
    cl_handler_top = 0;
    cl_restart_top = 0;
    cl_pending_throw = 0;
    cl_mv_count = 1;
    cl_trace_depth = 0;
}

void cl_vm_shutdown(void)
{
    if (cl_vm.stack) {
        platform_free(cl_vm.stack);
        cl_vm.stack = NULL;
    }
    if (cl_vm.frames) {
        platform_free(cl_vm.frames);
        cl_vm.frames = NULL;
    }
}

void cl_vm_push(CL_Obj val)
{
    if (cl_vm.sp >= (int)cl_vm.stack_size)
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
    uint8_t stub_code[1024];
    CL_Obj stub_consts[256];
    int cp = 0, cc = 0, i;
    CL_Bytecode *bc;
    CL_Obj bc_obj, result;

    /* Clamp nargs to 255 (OP_CALL u8 limit) */
    if (nargs > 255) nargs = 255;

    /* Constant 0 = function */
    stub_consts[cc] = func;
    stub_code[cp++] = OP_CONST;
    stub_code[cp++] = (uint8_t)(cc >> 8);
    stub_code[cp++] = (uint8_t)cc;
    cc++;

    /* Constants 1..nargs = arguments */
    for (i = 0; i < nargs; i++) {
        stub_consts[cc] = args[i];
        stub_code[cp++] = OP_CONST;
        stub_code[cp++] = (uint8_t)(cc >> 8);
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
    bc->n_optional = 0;
    bc->flags = 0;
    bc->n_keys = 0;
    bc->key_syms = NULL;
    bc->key_slots = NULL;

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

/* --- Trace helpers --- */

static CL_Obj get_func_name(CL_Obj func_obj)
{
    if (CL_FUNCTION_P(func_obj)) {
        return ((CL_Function *)CL_OBJ_TO_PTR(func_obj))->name;
    } else if (CL_CLOSURE_P(func_obj)) {
        CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(func_obj);
        return ((CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode))->name;
    } else if (CL_BYTECODE_P(func_obj)) {
        return ((CL_Bytecode *)CL_OBJ_TO_PTR(func_obj))->name;
    }
    return CL_NIL;
}

static int is_func_traced(CL_Obj func_obj)
{
    CL_Obj name;
    if (cl_trace_count <= 0) return 0;
    name = get_func_name(func_obj);
    if (!CL_NULL_P(name) && CL_SYMBOL_P(name)) {
        return (((CL_Symbol *)CL_OBJ_TO_PTR(name))->flags & CL_SYM_TRACED) != 0;
    }
    return 0;
}

static void trace_print_entry(CL_Obj name_sym, CL_Obj *args, int nargs)
{
    char buf[128];
    int i;
    for (i = 0; i < cl_trace_depth; i++)
        platform_write_string("  ");
    snprintf(buf, sizeof(buf), "%d: (", cl_trace_depth);
    platform_write_string(buf);
    platform_write_string(cl_symbol_name(name_sym));
    for (i = 0; i < nargs; i++) {
        platform_write_string(" ");
        cl_prin1_to_string(args[i], buf, sizeof(buf));
        platform_write_string(buf);
    }
    platform_write_string(")\n");
}

static void trace_print_exit(CL_Obj name_sym, CL_Obj result)
{
    char buf[128];
    int i;
    for (i = 0; i < cl_trace_depth; i++)
        platform_write_string("  ");
    snprintf(buf, sizeof(buf), "%d: ", cl_trace_depth);
    platform_write_string(buf);
    platform_write_string(cl_symbol_name(name_sym));
    platform_write_string(" returned ");
    cl_prin1_to_string(result, buf, sizeof(buf));
    platform_write_string(buf);
    platform_write_string("\n");
}

/* --- Backtrace capture --- */

/* Look up source line for a given IP in a bytecode's line map.
 * Returns 0 if no mapping found. Uses binary-like scan (entries sorted by pc). */
static int lookup_source_line(CL_Bytecode *bc, uint32_t ip)
{
    int i, best_line = 0;
    if (!bc->line_map || bc->line_map_count == 0) return 0;
    for (i = 0; i < bc->line_map_count; i++) {
        if (bc->line_map[i].pc <= ip)
            best_line = bc->line_map[i].line;
        else
            break;
    }
    return best_line;
}

static CL_Bytecode *get_frame_bytecode(CL_Frame *f)
{
    CL_Obj bc_obj = f->bytecode;
    if (CL_BYTECODE_P(bc_obj))
        return (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
    if (CL_CLOSURE_P(bc_obj)) {
        CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(bc_obj);
        if (CL_BYTECODE_P(cl->bytecode))
            return (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
    }
    return NULL;
}

void cl_capture_backtrace(void)
{
    int i, pos = 0;
    int max_show = 20;
    int depth = 0;

    cl_backtrace_buf[0] = '\0';
    if (cl_vm.fp <= 0) return;

    for (i = cl_vm.fp - 1; i >= 0 && depth < max_show; i--, depth++) {
        CL_Frame *f = &cl_vm.frames[i];
        CL_Obj name = get_func_name(f->bytecode);
        CL_Bytecode *bc = get_frame_bytecode(f);
        int line = 0;
        const char *file = NULL;
        int n;

        if (bc) {
            line = lookup_source_line(bc, f->ip);
            file = bc->source_file;
        }

        n = snprintf(cl_backtrace_buf + pos,
                     CL_BACKTRACE_BUF_SIZE - pos, "  %d: ", depth);
        pos += n;
        if (pos >= CL_BACKTRACE_BUF_SIZE - 1) break;

        if (!CL_NULL_P(name) && CL_SYMBOL_P(name)) {
            if (file && line > 0) {
                n = snprintf(cl_backtrace_buf + pos,
                             CL_BACKTRACE_BUF_SIZE - pos,
                             "%s (%s:%d)\n", cl_symbol_name(name), file, line);
            } else if (line > 0) {
                n = snprintf(cl_backtrace_buf + pos,
                             CL_BACKTRACE_BUF_SIZE - pos,
                             "%s (line %d)\n", cl_symbol_name(name), line);
            } else {
                n = snprintf(cl_backtrace_buf + pos,
                             CL_BACKTRACE_BUF_SIZE - pos,
                             "%s\n", cl_symbol_name(name));
            }
        } else {
            if (file && line > 0) {
                n = snprintf(cl_backtrace_buf + pos,
                             CL_BACKTRACE_BUF_SIZE - pos,
                             "<anonymous> (%s:%d)\n", file, line);
            } else if (line > 0) {
                n = snprintf(cl_backtrace_buf + pos,
                             CL_BACKTRACE_BUF_SIZE - pos,
                             "<anonymous> (line %d)\n", line);
            } else {
                n = snprintf(cl_backtrace_buf + pos,
                             CL_BACKTRACE_BUF_SIZE - pos,
                             "<anonymous>\n");
            }
        }
        pos += n;
        if (pos >= CL_BACKTRACE_BUF_SIZE - 1) break;
    }

    if (cl_vm.fp > max_show) {
        snprintf(cl_backtrace_buf + pos,
                 CL_BACKTRACE_BUF_SIZE - pos,
                 "  ... %d more frames\n", cl_vm.fp - max_show);
    }
}

/* Call a built-in C function */
static CL_Obj call_builtin(CL_Function *func, CL_Obj *args, int nargs)
{
    CL_Obj result;
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
    cl_mv_count = 1;  /* default; bi_values/bi_values_list may override */
    result = func->func(args, nargs);
    cl_mv_values[0] = result;  /* primary always in buffer */
    return result;
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
    frame->nargs = 0;

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
            cl_mv_count = 1;
            break;
        }

        case OP_NIL:
            cl_vm_push(CL_NIL);
            cl_mv_count = 1;
            break;

        case OP_T:
            cl_vm_push(SYM_T);
            cl_mv_count = 1;
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
            cl_mv_count = 1;
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
            if (s->function != CL_UNBOUND) {
                cl_vm_push(s->function);
            } else if (s->value != CL_UNBOUND) {
                /* Fall back to value slot (for labels/flet value bindings) */
                cl_vm_push(s->value);
            } else {
                cl_error(CL_ERR_UNDEFINED, "Undefined function: %s",
                         cl_symbol_name(sym));
            }
            cl_mv_count = 1;
            break;
        }

        case OP_FSTORE: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj sym = constants[idx];
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            s->function = cl_vm.stack[cl_vm.sp - 1];
            break;
        }

        case OP_MAKE_CELL: {
            CL_Obj val = cl_vm_pop();
            cl_vm_push(cl_make_cell(val));
            break;
        }

        case OP_CELL_REF: {
            CL_Obj cell_obj = cl_vm_pop();
            CL_Cell *cell = (CL_Cell *)CL_OBJ_TO_PTR(cell_obj);
            cl_vm_push(cell->value);
            break;
        }

        case OP_CELL_SET_LOCAL: {
            uint8_t slot = code[ip++];
            CL_Obj cell_obj = cl_vm.stack[frame->bp + slot];
            CL_Cell *cell = (CL_Cell *)CL_OBJ_TO_PTR(cell_obj);
            cell->value = cl_vm.stack[cl_vm.sp - 1]; /* peek */
            break;
        }

        case OP_CELL_SET_UPVAL: {
            uint8_t index = code[ip++];
            CL_Obj cell_obj;
            CL_Cell *cell;
            if (CL_CLOSURE_P(frame->bytecode)) {
                CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                cell_obj = cl->upvalues[index];
            } else {
                break; /* shouldn't happen */
            }
            cell = (CL_Cell *)CL_OBJ_TO_PTR(cell_obj);
            cell->value = cl_vm.stack[cl_vm.sp - 1]; /* peek */
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
            cl_mv_count = 1;
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
            cl_mv_count = 1;
            break;
        }

        case OP_CAR: {
            CL_Obj obj = cl_vm_pop();
            cl_vm_push(cl_car(obj));
            cl_mv_count = 1;
            break;
        }

        case OP_CDR: {
            CL_Obj obj = cl_vm_pop();
            cl_vm_push(cl_cdr(obj));
            cl_mv_count = 1;
            break;
        }

        case OP_ADD: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(cl_arith_add(a, b));
            cl_mv_count = 1;
            break;
        }

        case OP_SUB: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(cl_arith_sub(a, b));
            cl_mv_count = 1;
            break;
        }

        case OP_MUL: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(cl_arith_mul(a, b));
            cl_mv_count = 1;
            break;
        }

        case OP_DIV: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (CL_FLOATP(a) || CL_FLOATP(b))
                cl_vm_push(cl_float_div(a, b));
            else
                cl_vm_push(cl_ratio_div(a, b));
            cl_mv_count = 1;
            break;
        }

        case OP_EQ: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(a == b ? SYM_T : CL_NIL);
            cl_mv_count = 1;
            break;
        }

        case OP_NUMEQ: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(cl_arith_compare(a, b) == 0 ? SYM_T : CL_NIL);
            cl_mv_count = 1;
            break;
        }

        case OP_LT: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(cl_arith_compare(a, b) < 0 ? SYM_T : CL_NIL);
            cl_mv_count = 1;
            break;
        }

        case OP_GT: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(cl_arith_compare(a, b) > 0 ? SYM_T : CL_NIL);
            cl_mv_count = 1;
            break;
        }

        case OP_LE: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (!CL_FIXNUM_P(a) || !CL_FIXNUM_P(b))
                cl_error(CL_ERR_TYPE, "<=: not a number");
            cl_vm_push(CL_FIXNUM_VAL(a) <= CL_FIXNUM_VAL(b) ? SYM_T : CL_NIL);
            cl_mv_count = 1;
            break;
        }

        case OP_GE: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (!CL_FIXNUM_P(a) || !CL_FIXNUM_P(b))
                cl_error(CL_ERR_TYPE, ">=: not a number");
            cl_vm_push(CL_FIXNUM_VAL(a) >= CL_FIXNUM_VAL(b) ? SYM_T : CL_NIL);
            cl_mv_count = 1;
            break;
        }

        case OP_NOT: {
            CL_Obj a = cl_vm_pop();
            cl_vm_push(CL_NULL_P(a) ? SYM_T : CL_NIL);
            cl_mv_count = 1;
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
            cl_mv_count = 1;
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
                int traced = is_func_traced(func_obj);
                CL_Obj result;
                if (traced) {
                    trace_print_entry(f->name, arg_base, nargs);
                    cl_trace_depth++;
                }
                result = call_builtin(f, arg_base, nargs);
                if (traced) {
                    cl_trace_depth--;
                    trace_print_exit(f->name, result);
                }
                cl_vm.sp -= (nargs + 1);
                cl_vm_push(result);
            } else if (CL_BYTECODE_P(func_obj) || CL_CLOSURE_P(func_obj)) {
                CL_Bytecode *callee_bc;
                CL_Frame *new_frame;
                int callee_arity, has_rest, n_opt, has_key, i;
                int min_args, max_args;

                if (CL_CLOSURE_P(func_obj)) {
                    CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(func_obj);
                    callee_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
                } else {
                    callee_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(func_obj);
                }

                callee_arity = callee_bc->arity & 0x7FFF;
                has_rest = callee_bc->arity & 0x8000;
                n_opt = callee_bc->n_optional;
                has_key = callee_bc->flags & 1;

                /* Arity check */
                min_args = callee_arity;
                max_args = (has_rest || has_key)
                           ? 255 : callee_arity + n_opt;

                if (nargs < min_args) {
                    cl_error(CL_ERR_ARGS, "Too few arguments: expected %s%d, got %d",
                             (n_opt || has_rest || has_key) ? "at least " : "",
                             min_args, nargs);
                }
                if (nargs > max_args) {
                    cl_error(CL_ERR_ARGS, "Too many arguments: expected %s%d, got %d",
                             n_opt ? "at most " : "",
                             max_args, nargs);
                }

                if (is_tail && cl_vm.fp > base_fp + 1) {
                    CL_Obj *src = arg_base;
                    CL_Obj extra_args[256];
                    int n_extra = 0;
                    int n_positional = callee_arity + n_opt;

                    /* Trace: adjust depth for tail call transition */
                    if (cl_trace_count > 0) {
                        CL_Obj cur_name = get_func_name(frame->bytecode);
                        if (!CL_NULL_P(cur_name) && CL_SYMBOL_P(cur_name) &&
                            (((CL_Symbol *)CL_OBJ_TO_PTR(cur_name))->flags & CL_SYM_TRACED))
                            cl_trace_depth--;
                        if (is_func_traced(func_obj)) {
                            trace_print_entry(callee_bc->name, arg_base, nargs);
                            cl_trace_depth++;
                        }
                    }

                    cl_vm.sp = frame->bp;

                    /* Push positional args */
                    for (i = 0; i < nargs && i < n_positional; i++)
                        cl_vm_push(src[i]);

                    /* Fill missing optionals with NIL */
                    while (cl_vm.sp < (int)(frame->bp + n_positional))
                        cl_vm_push(CL_NIL);

                    /* Save extra args for keyword processing */
                    if (has_key || has_rest) {
                        for (i = n_positional; i < nargs; i++) {
                            if (n_extra < 256) extra_args[n_extra++] = src[i];
                        }
                    }

                    /* Handle &rest */
                    if (has_rest) {
                        CL_Obj rest = CL_NIL;
                        int j;
                        for (j = n_extra - 1; j >= 0; j--)
                            rest = cl_cons(extra_args[j], rest);
                        cl_vm_push(rest);
                    }

                    /* Fill remaining locals with NIL (including key slots) */
                    while (cl_vm.sp < (int)(frame->bp + callee_bc->n_locals))
                        cl_vm_push(CL_NIL);

                    /* Keyword matching */
                    if (has_key) {
                        int allow = (callee_bc->flags & 2) != 0;
                        int ki;
                        /* Check for :allow-other-keys t in caller args */
                        if (!allow) {
                            for (ki = 0; ki + 1 < n_extra; ki += 2) {
                                if (extra_args[ki] == KW_ALLOW_OTHER_KEYS &&
                                    !CL_NULL_P(extra_args[ki + 1])) {
                                    allow = 1;
                                    break;
                                }
                            }
                        }
                        for (ki = 0; ki + 1 < n_extra; ki += 2) {
                            CL_Obj key = extra_args[ki];
                            CL_Obj val = extra_args[ki + 1];
                            int j, found = 0;
                            for (j = 0; j < callee_bc->n_keys; j++) {
                                if (key == callee_bc->key_syms[j]) {
                                    cl_vm.stack[frame->bp + callee_bc->key_slots[j]] = val;
                                    found = 1;
                                    break;
                                }
                            }
                            if (!found && key != KW_ALLOW_OTHER_KEYS && !allow)
                                cl_error(CL_ERR_ARGS, "Unknown keyword argument: %s",
                                         cl_symbol_name(key));
                        }
                    }

                    frame->bytecode = func_obj;
                    frame->code = callee_bc->code;
                    frame->constants = callee_bc->constants;
                    frame->ip = 0;
                    frame->n_locals = callee_bc->n_locals;
                    frame->nargs = nargs;
                    code = callee_bc->code;
                    constants = callee_bc->constants;
                    ip = 0;
                } else {
                    /* Normal call: push new frame */
                    uint32_t new_bp;
                    CL_Obj extra_args[256];
                    int n_extra = 0;
                    int n_positional = callee_arity + n_opt;

                    /* Trace: entry for normal call */
                    if (is_func_traced(func_obj))  {
                        trace_print_entry(callee_bc->name, arg_base, nargs);
                        cl_trace_depth++;
                    }

                    /* Remove func_obj from under args; shift args down */
                    {
                        int j;
                        for (j = 0; j < nargs; j++)
                            cl_vm.stack[cl_vm.sp - nargs - 1 + j] =
                                cl_vm.stack[cl_vm.sp - nargs + j];
                        cl_vm.sp--;
                    }

                    new_bp = cl_vm.sp - nargs;

                    /* Save extra args for keyword processing */
                    if (has_key || has_rest) {
                        for (i = n_positional; i < nargs; i++) {
                            if (n_extra < 256)
                                extra_args[n_extra++] = cl_vm.stack[new_bp + i];
                        }
                    }

                    /* Truncate stack to positional args */
                    if (nargs > n_positional)
                        cl_vm.sp = new_bp + n_positional;

                    /* Fill missing optionals with NIL */
                    while (cl_vm.sp < (int)(new_bp + n_positional))
                        cl_vm_push(CL_NIL);

                    /* Handle &rest parameter */
                    if (has_rest) {
                        CL_Obj rest = CL_NIL;
                        int j;
                        for (j = n_extra - 1; j >= 0; j--)
                            rest = cl_cons(extra_args[j], rest);
                        cl_vm_push(rest);
                    }

                    /* Fill remaining locals with NIL (including key slots) */
                    while (cl_vm.sp < (int)(new_bp + callee_bc->n_locals))
                        cl_vm_push(CL_NIL);

                    /* Keyword matching */
                    if (has_key) {
                        int allow = (callee_bc->flags & 2) != 0;
                        int ki;
                        /* Check for :allow-other-keys t in caller args */
                        if (!allow) {
                            for (ki = 0; ki + 1 < n_extra; ki += 2) {
                                if (extra_args[ki] == KW_ALLOW_OTHER_KEYS &&
                                    !CL_NULL_P(extra_args[ki + 1])) {
                                    allow = 1;
                                    break;
                                }
                            }
                        }
                        for (ki = 0; ki + 1 < n_extra; ki += 2) {
                            CL_Obj key = extra_args[ki];
                            CL_Obj val = extra_args[ki + 1];
                            int j, found = 0;
                            for (j = 0; j < callee_bc->n_keys; j++) {
                                if (key == callee_bc->key_syms[j]) {
                                    cl_vm.stack[new_bp + callee_bc->key_slots[j]] = val;
                                    found = 1;
                                    break;
                                }
                            }
                            if (!found && key != KW_ALLOW_OTHER_KEYS && !allow)
                                cl_error(CL_ERR_ARGS, "Unknown keyword argument: %s",
                                         cl_symbol_name(key));
                        }
                    }

                    /* Save current frame state */
                    frame->ip = ip;

                    if (cl_vm.fp >= cl_vm.frame_size)
                        cl_error(CL_ERR_OVERFLOW, "Call stack overflow");

                    new_frame = &cl_vm.frames[cl_vm.fp++];
                    new_frame->bytecode = func_obj;
                    new_frame->code = callee_bc->code;
                    new_frame->constants = callee_bc->constants;
                    new_frame->ip = 0;
                    new_frame->bp = new_bp;
                    new_frame->n_locals = callee_bc->n_locals;
                    new_frame->nargs = nargs;

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

            /* Trace: print return value */
            if (cl_trace_count > 0) {
                CL_Obj ret_name = get_func_name(frame->bytecode);
                if (!CL_NULL_P(ret_name) && CL_SYMBOL_P(ret_name) &&
                    (((CL_Symbol *)CL_OBJ_TO_PTR(ret_name))->flags & CL_SYM_TRACED)) {
                    cl_trace_depth--;
                    trace_print_exit(ret_name, result);
                }
            }

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
            cl_mv_count = 1;
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

        case OP_DEFTYPE: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj name = constants[idx];
            CL_Obj expander = cl_vm_pop();
            cl_register_type(name, expander);
            cl_vm_push(name);
            break;
        }

        case OP_HANDLER_PUSH: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj type_sym = constants[idx];
            CL_Obj handler = cl_vm_pop();

            if (cl_handler_top >= CL_MAX_HANDLER_BINDINGS)
                cl_error(CL_ERR_OVERFLOW, "Handler stack overflow");

            cl_handler_stack[cl_handler_top].type_name = type_sym;
            cl_handler_stack[cl_handler_top].handler = handler;
            cl_handler_stack[cl_handler_top].handler_mark = cl_handler_top;
            cl_handler_top++;
            break;
        }

        case OP_HANDLER_POP: {
            uint8_t count = code[ip++];
            cl_handler_top -= count;
            if (cl_handler_top < 0) cl_handler_top = 0;
            break;
        }

        case OP_RESTART_PUSH: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj name_sym = constants[idx];
            CL_Obj tag = cl_vm_pop();
            CL_Obj handler = cl_vm_pop();

            if (cl_restart_top >= CL_MAX_RESTART_BINDINGS)
                cl_error(CL_ERR_OVERFLOW, "Restart stack overflow");

            cl_restart_stack[cl_restart_top].name = name_sym;
            cl_restart_stack[cl_restart_top].handler = handler;
            cl_restart_stack[cl_restart_top].tag = tag;
            cl_restart_top++;
            break;
        }

        case OP_RESTART_POP: {
            uint8_t count = code[ip++];
            cl_restart_top -= count;
            if (cl_restart_top < 0) cl_restart_top = 0;
            break;
        }

        case OP_ASSERT_TYPE: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj type_spec = constants[idx];
            CL_Obj val = cl_vm.stack[cl_vm.sp - 1]; /* peek TOS */
            if (!cl_typep(val, type_spec)) {
                /* Build type-error condition with :datum and :expected-type */
                CL_Obj slots = CL_NIL;
                CL_Obj cond;
                CL_GC_PROTECT(slots);
                slots = cl_cons(cl_cons(KW_EXPECTED_TYPE, type_spec), slots);
                slots = cl_cons(cl_cons(KW_DATUM, val), slots);
                CL_GC_UNPROTECT(1);
                cond = cl_make_condition(SYM_TYPE_ERROR, slots, CL_NIL);
                cl_signal_condition(cond);
                /* If no handler transferred control, fall to C error */
                {
                    char buf[128];
                    char tbuf[64];
                    cl_prin1_to_string(val, buf, sizeof(buf));
                    cl_prin1_to_string(type_spec, tbuf, sizeof(tbuf));
                    cl_error(CL_ERR_TYPE, "THE: value %s is not of type %s", buf, tbuf);
                }
            }
            break;
        }

        case OP_BLOCK_PUSH: {
            /* Set up NLX block frame for return-from support */
            uint16_t tag_idx = read_u16(code, &ip);
            int16_t block_offset = read_i16(code, &ip);
            CL_Obj block_tag = constants[tag_idx];
            CL_NLXFrame *nlx;

            if (cl_nlx_top >= CL_MAX_NLX_FRAMES)
                cl_error(CL_ERR_OVERFLOW, "NLX stack overflow");

            nlx = &cl_nlx_stack[cl_nlx_top];
            nlx->type = CL_NLX_BLOCK;
            nlx->vm_sp = cl_vm.sp;
            nlx->vm_fp = cl_vm.fp;
            nlx->tag = block_tag;
            nlx->result = CL_NIL;
            nlx->catch_ip = ip;
            nlx->offset = block_offset;
            nlx->code = code;
            nlx->constants = constants;
            nlx->base_fp = base_fp;
            nlx->dyn_mark = cl_dyn_top;
            nlx->handler_mark = cl_handler_top;
            nlx->restart_mark = cl_restart_top;

            if (setjmp(nlx->buf) == 0) {
                /* Normal path: block body executes */
                cl_nlx_top++;
            } else {
                /* longjmp from return-from: restore state */
                nlx = &cl_nlx_stack[cl_nlx_top];
                cl_dynbind_restore_to(nlx->dyn_mark);
                cl_handler_top = nlx->handler_mark;
                cl_restart_top = nlx->restart_mark;
                {
                    CL_Obj block_result = nlx->result;
                    cl_vm.sp = nlx->vm_sp;
                    cl_vm.fp = nlx->vm_fp;
                    frame = &cl_vm.frames[cl_vm.fp - 1];
                    code = nlx->code;
                    constants = nlx->constants;
                    base_fp = nlx->base_fp;
                    ip = nlx->catch_ip + nlx->offset;
                    cl_mv_count = 1;
                    cl_vm_push(block_result);
                }
            }
            break;
        }

        case OP_BLOCK_POP: {
            /* Normal exit from block body — pop NLX frame */
            if (cl_nlx_top > 0)
                cl_nlx_top--;
            break;
        }

        case OP_BLOCK_RETURN: {
            /* return-from: pop value, find matching block on NLX stack, longjmp */
            uint16_t tag_idx = read_u16(code, &ip);
            CL_Obj block_tag = constants[tag_idx];
            CL_Obj value = cl_vm_pop();
            int i;

            for (i = cl_nlx_top - 1; i >= 0; i--) {
                if (cl_nlx_stack[i].type == CL_NLX_BLOCK &&
                    cl_nlx_stack[i].tag == block_tag) {
                    int j;
                    /* Check for interposing UWPROT frames */
                    for (j = cl_nlx_top - 1; j > i; j--) {
                        if (cl_nlx_stack[j].type == CL_NLX_UWPROT) {
                            cl_pending_throw = 1;
                            cl_pending_tag = block_tag;
                            cl_pending_value = value;
                            cl_nlx_top = j;
                            longjmp(cl_nlx_stack[j].buf, 1);
                        }
                    }
                    /* No interposing UWPROT — longjmp directly to block */
                    cl_nlx_stack[i].result = value;
                    cl_nlx_top = i;
                    longjmp(cl_nlx_stack[i].buf, 1);
                }
            }
            cl_error(CL_ERR_GENERAL, "RETURN-FROM: no block named %s",
                     CL_NULL_P(block_tag) ? "NIL" : cl_symbol_name(block_tag));
            break;
        }

        case OP_ARGC: {
            cl_vm_push(CL_MAKE_FIXNUM(frame->nargs));
            cl_mv_count = 1;
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

            /* Resolve symbol to its function binding */
            if (CL_SYMBOL_P(func_obj)) {
                CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(func_obj);
                func_obj = s->function;
                if (CL_NULL_P(func_obj) || func_obj == CL_UNBOUND)
                    cl_error(CL_ERR_TYPE, "APPLY: symbol has no function binding");
            }

            if (CL_FUNCTION_P(func_obj)) {
                CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func_obj);
                int traced = is_func_traced(func_obj);
                if (traced) {
                    trace_print_entry(f->name, flat_args, nflat);
                    cl_trace_depth++;
                }
                result = call_builtin(f, flat_args, nflat);
                if (traced) {
                    cl_trace_depth--;
                    trace_print_exit(f->name, result);
                }
            } else if (CL_BYTECODE_P(func_obj) || CL_CLOSURE_P(func_obj)) {
                result = cl_vm_apply(func_obj, flat_args, nflat);
            } else {
                cl_error(CL_ERR_TYPE, "APPLY: not a callable function");
                result = CL_NIL;
            }
            cl_vm_push(result);
            break;
        }

        case OP_CATCH: {
            int16_t catch_offset = read_i16(code, &ip);
            CL_Obj catch_tag = cl_vm_pop();
            CL_NLXFrame *nlx;

            if (cl_nlx_top >= CL_MAX_NLX_FRAMES)
                cl_error(CL_ERR_OVERFLOW, "NLX stack overflow");

            nlx = &cl_nlx_stack[cl_nlx_top];
            nlx->type = CL_NLX_CATCH;
            nlx->vm_sp = cl_vm.sp;
            nlx->vm_fp = cl_vm.fp;
            nlx->tag = catch_tag;
            nlx->result = CL_NIL;
            nlx->catch_ip = ip;
            nlx->offset = catch_offset;
            nlx->code = code;
            nlx->constants = constants;
            nlx->base_fp = base_fp;
            nlx->dyn_mark = cl_dyn_top;
            nlx->handler_mark = cl_handler_top;
            nlx->restart_mark = cl_restart_top;

            if (setjmp(nlx->buf) == 0) {
                /* Normal path: body executes */
                cl_nlx_top++;
            } else {
                /* longjmp from throw: restore state from NLX frame.
                 * Recompute nlx from global — the local pointer may be
                 * indeterminate after longjmp (C99 7.13.2.1). */
                nlx = &cl_nlx_stack[cl_nlx_top];
                cl_dynbind_restore_to(nlx->dyn_mark);
                cl_handler_top = nlx->handler_mark;
                cl_restart_top = nlx->restart_mark;
                {
                    CL_Obj throw_result = nlx->result;
                    cl_vm.sp = nlx->vm_sp;
                    cl_vm.fp = nlx->vm_fp;
                    frame = &cl_vm.frames[cl_vm.fp - 1];
                    code = nlx->code;
                    constants = nlx->constants;
                    base_fp = nlx->base_fp;
                    ip = nlx->catch_ip + nlx->offset;
                    cl_mv_count = 1;  /* throw delivers single value */
                    cl_vm_push(throw_result);
                }
            }
            break;
        }

        case OP_UNCATCH: {
            /* Normal exit from catch body — pop NLX frame */
            if (cl_nlx_top > 0)
                cl_nlx_top--;
            break;
        }

        case OP_UWPROT: {
            int16_t uwp_offset = read_i16(code, &ip);
            CL_NLXFrame *nlx;

            if (cl_nlx_top >= CL_MAX_NLX_FRAMES)
                cl_error(CL_ERR_OVERFLOW, "NLX stack overflow");

            nlx = &cl_nlx_stack[cl_nlx_top];
            nlx->type = CL_NLX_UWPROT;
            nlx->vm_sp = cl_vm.sp;
            nlx->vm_fp = cl_vm.fp;
            nlx->tag = CL_NIL;
            nlx->result = CL_NIL;
            nlx->catch_ip = ip;
            nlx->offset = uwp_offset;
            nlx->code = code;
            nlx->constants = constants;
            nlx->base_fp = base_fp;
            nlx->dyn_mark = cl_dyn_top;
            nlx->handler_mark = cl_handler_top;
            nlx->restart_mark = cl_restart_top;

            if (setjmp(nlx->buf) == 0) {
                /* Normal path: protected form executes */
                cl_nlx_top++;
            } else {
                /* longjmp from throw/error through UWP: restore state, jump to cleanup.
                 * Recompute nlx — local pointer may be indeterminate after longjmp. */
                nlx = &cl_nlx_stack[cl_nlx_top];
                cl_dynbind_restore_to(nlx->dyn_mark);
                cl_handler_top = nlx->handler_mark;
                cl_restart_top = nlx->restart_mark;
                cl_vm.sp = nlx->vm_sp;
                cl_vm.fp = nlx->vm_fp;
                frame = &cl_vm.frames[cl_vm.fp - 1];
                code = nlx->code;
                constants = nlx->constants;
                base_fp = nlx->base_fp;
                ip = nlx->catch_ip + nlx->offset;
            }
            break;
        }

        case OP_UWPOP: {
            /* Normal exit from protected form — pop NLX frame, clear pending */
            if (cl_nlx_top > 0)
                cl_nlx_top--;
            cl_pending_throw = 0;
            break;
        }

        case OP_UWRETHROW: {
            /* After cleanup forms: re-initiate pending throw/error if any */
            if (cl_pending_throw == 1) {
                /* Re-throw: find matching catch or block */
                CL_Obj ptag = cl_pending_tag;
                CL_Obj pval = cl_pending_value;
                int i;

                for (i = cl_nlx_top - 1; i >= 0; i--) {
                    if ((cl_nlx_stack[i].type == CL_NLX_CATCH ||
                         cl_nlx_stack[i].type == CL_NLX_BLOCK) &&
                        cl_nlx_stack[i].tag == ptag) {
                        /* Check for interposing UWPROT */
                        int j;
                        for (j = cl_nlx_top - 1; j > i; j--) {
                            if (cl_nlx_stack[j].type == CL_NLX_UWPROT) {
                                /* Jump to interposing UWPROT first */
                                cl_nlx_top = j;
                                longjmp(cl_nlx_stack[j].buf, 1);
                            }
                        }
                        /* No interposing UWPROT, go directly to target */
                        cl_pending_throw = 0;
                        cl_nlx_stack[i].result = pval;
                        cl_nlx_top = i;
                        longjmp(cl_nlx_stack[i].buf, 1);
                    }
                }
                /* No catch found — signal error */
                cl_pending_throw = 0;
                cl_error(CL_ERR_GENERAL, "No catch for tag during re-throw");
            } else if (cl_pending_throw == 2) {
                /* Re-throw error: find interposing UWPROT or error frame */
                int i;
                for (i = cl_nlx_top - 1; i >= 0; i--) {
                    if (cl_nlx_stack[i].type == CL_NLX_UWPROT) {
                        cl_nlx_top = i;
                        longjmp(cl_nlx_stack[i].buf, 1);
                    }
                }
                /* No more UWPROT — propagate to error handler.
                 * Restore VM state to before this cl_vm_eval call
                 * so subsequent evaluations start from a clean state. */
                {
                    int err_code = cl_pending_error_code;
                    cl_pending_throw = 0;
                    cl_nlx_top = 0;
                    cl_dynbind_restore_to(0);
                    cl_handler_top = 0;
                    cl_restart_top = 0;
                    cl_vm.fp = base_fp;
                    cl_vm.sp = cl_vm.frames[base_fp].bp;
                    cl_error_code = err_code;
                    strncpy(cl_error_msg, cl_pending_error_msg, sizeof(cl_error_msg) - 1);
                    cl_error_msg[sizeof(cl_error_msg) - 1] = '\0';
                    if (cl_error_frame_top > 0) {
                        cl_error_frame_top--;
                        cl_error_frames[cl_error_frame_top].active = 0;
                        longjmp(cl_error_frames[cl_error_frame_top].buf, err_code);
                    }
                    platform_write_string("FATAL ERROR: ");
                    platform_write_string(cl_error_msg);
                    platform_write_string("\n");
                    exit(1);
                }
            }
            /* cl_pending_throw == 0: nop */
            break;
        }

        case OP_MV_LOAD: {
            uint8_t index = code[ip++];
            cl_vm_push((int)index < cl_mv_count ? cl_mv_values[index] : CL_NIL);
            break;
        }

        case OP_MV_TO_LIST: {
            /* Pops primary from stack, builds list from MV buffer.
             * For single values (inline opcodes), uses the popped primary. */
            CL_Obj primary = cl_vm_pop();
            CL_Obj list = CL_NIL;
            if (cl_mv_count == 0) {
                /* (values) — no values, empty list */
            } else if (cl_mv_count == 1) {
                list = cl_cons(primary, CL_NIL);
            } else {
                int i;
                for (i = cl_mv_count - 1; i >= 0; i--)
                    list = cl_cons(cl_mv_values[i], list);
            }
            cl_vm_push(list);
            cl_mv_count = 1;
            break;
        }

        case OP_NTH_VALUE: {
            /* Stack: [index] [primary]  (primary on top)
             * Pops primary, pops index, pushes result. */
            CL_Obj primary = cl_vm_pop();
            CL_Obj idx_obj = cl_vm_pop();
            int idx;
            if (!CL_FIXNUM_P(idx_obj))
                cl_error(CL_ERR_TYPE, "NTH-VALUE: index must be a number");
            idx = CL_FIXNUM_VAL(idx_obj);
            if (idx == 0)
                cl_vm_push(primary);
            else
                cl_vm_push(idx > 0 && idx < cl_mv_count ? cl_mv_values[idx] : CL_NIL);
            cl_mv_count = 1;
            break;
        }

        case OP_DYNBIND: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj sym = constants[idx];
            CL_Obj new_val = cl_vm_pop();
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            if (cl_dyn_top >= CL_MAX_DYN_BINDINGS)
                cl_error(CL_ERR_OVERFLOW, "Dynamic binding stack overflow");
            cl_dyn_stack[cl_dyn_top].symbol = sym;
            cl_dyn_stack[cl_dyn_top].old_value = s->value;
            cl_dyn_top++;
            s->value = new_val;
            break;
        }

        case OP_DYNUNBIND: {
            uint8_t count = code[ip++];
            cl_dynbind_restore_to(cl_dyn_top - count);
            break;
        }

        case OP_RPLACA: {
            CL_Obj new_car = cl_vm_pop();
            CL_Obj cons_obj = cl_vm_pop();
            CL_Cons *cell;
            if (!CL_CONS_P(cons_obj))
                cl_error(CL_ERR_TYPE, "RPLACA: not a cons");
            cell = (CL_Cons *)CL_OBJ_TO_PTR(cons_obj);
            cell->car = new_car;
            cl_vm_push(new_car);
            cl_mv_count = 1;
            break;
        }

        case OP_RPLACD: {
            CL_Obj new_cdr = cl_vm_pop();
            CL_Obj cons_obj = cl_vm_pop();
            CL_Cons *cell;
            if (!CL_CONS_P(cons_obj))
                cl_error(CL_ERR_TYPE, "RPLACD: not a cons");
            cell = (CL_Cons *)CL_OBJ_TO_PTR(cons_obj);
            cell->cdr = new_cdr;
            cl_vm_push(new_cdr);
            cl_mv_count = 1;
            break;
        }

        case OP_ASET: {
            CL_Obj val = cl_vm_pop();
            CL_Obj idx_obj = cl_vm_pop();
            CL_Obj vec_obj = cl_vm_pop();
            int32_t idx;
            if (!CL_FIXNUM_P(idx_obj))
                cl_error(CL_ERR_TYPE, "ASET: index must be a number");
            idx = CL_FIXNUM_VAL(idx_obj);
            if (CL_BIT_VECTOR_P(vec_obj)) {
                CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(vec_obj);
                int32_t v;
                if (idx < 0 || (uint32_t)idx >= cl_bv_active_length(bv))
                    cl_error(CL_ERR_ARGS, "ASET: index %d out of range", (int)idx);
                if (!CL_FIXNUM_P(val))
                    cl_error(CL_ERR_TYPE, "ASET: value must be 0 or 1 for bit vector");
                v = CL_FIXNUM_VAL(val);
                if (v != 0 && v != 1)
                    cl_error(CL_ERR_TYPE, "ASET: value must be 0 or 1 for bit vector");
                cl_bv_set_bit(bv, (uint32_t)idx, v);
            } else if (CL_STRING_P(vec_obj)) {
                CL_String *str = (CL_String *)CL_OBJ_TO_PTR(vec_obj);
                if (idx < 0 || (uint32_t)idx >= str->length)
                    cl_error(CL_ERR_ARGS, "ASET: index %d out of range (0-%lu)",
                             (int)idx, (unsigned long)(str->length - 1));
                if (!CL_CHAR_P(val))
                    cl_error(CL_ERR_TYPE, "ASET: value must be a character for string");
                str->data[idx] = (char)CL_CHAR_VAL(val);
            } else if (CL_VECTOR_P(vec_obj)) {
                CL_Vector *vec = (CL_Vector *)CL_OBJ_TO_PTR(vec_obj);
                if (idx < 0 || (uint32_t)idx >= vec->length)
                    cl_error(CL_ERR_ARGS, "ASET: index %d out of range (0-%lu)",
                             (int)idx, (unsigned long)(vec->length - 1));
                cl_vector_data(vec)[idx] = val;
            } else {
                cl_error(CL_ERR_TYPE, "ASET: not a vector");
            }
            cl_vm_push(val);
            cl_mv_count = 1;
            break;
        }

        default:
            cl_error(CL_ERR_GENERAL, "Unknown opcode: 0x%02x at ip=%u",
                     op, ip - 1);
            return CL_NIL;
        }
    }
}
