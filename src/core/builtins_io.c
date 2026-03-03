#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "printer.h"
#include "compiler.h"
#include "reader.h"
#include "vm.h"
#include "opcodes.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <string.h>

/* Helper to register a builtin */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    s->value = fn;
}

/* --- I/O --- */

static CL_Obj bi_print(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    cl_print(args[0]);
    return args[0];
}

static CL_Obj bi_prin1(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    cl_prin1(args[0]);
    return args[0];
}

static CL_Obj bi_princ(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    cl_princ(args[0]);
    return args[0];
}

static CL_Obj bi_terpri(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    platform_write_string("\n");
    return CL_NIL;
}

static CL_Obj bi_format(CL_Obj *args, int n)
{
    /* Minimal: (format t "string") just prints */
    CL_UNUSED(n);
    if (n >= 2 && CL_STRING_P(args[1])) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[1]);
        /* Simple ~A and ~% substitution */
        const char *p = s->data;
        int ai = 2; /* Next argument index */
        while (*p) {
            if (*p == '~' && *(p+1)) {
                p++;
                if (*p == 'A' || *p == 'a') {
                    if (ai < n) cl_princ(args[ai++]);
                } else if (*p == 'S' || *p == 's') {
                    if (ai < n) cl_prin1(args[ai++]);
                } else if (*p == '%') {
                    platform_write_string("\n");
                } else if (*p == '~') {
                    platform_write_string("~");
                }
                p++;
            } else {
                char c[2] = { *p, '\0' };
                platform_write_string(c);
                p++;
            }
        }
    }
    return CL_NIL;
}

/* --- Load --- */

static CL_Obj bi_load(CL_Obj *args, int n)
{
    CL_String *path_str;
    char *buf;
    unsigned long size;
    CL_ReadStream stream;
    CL_Obj expr, bytecode;
    const char *prev_file;
    uint16_t prev_file_id;

    CL_UNUSED(n);
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "LOAD: argument must be a string");

    path_str = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    buf = platform_file_read(path_str->data, &size);
    if (!buf)
        cl_error(CL_ERR_GENERAL, "LOAD: cannot open file");

    /* Save and set source file context */
    prev_file = cl_current_source_file;
    prev_file_id = cl_current_file_id;
    cl_current_source_file = path_str->data;
    cl_current_file_id++;

    stream.buf = buf;
    stream.pos = 0;
    stream.len = (int)size;
    stream.line = 1;

    for (;;) {
        int err;

        expr = cl_read_from_string(&stream);
        if (cl_reader_eof()) break;

        err = CL_CATCH();
        if (err == CL_ERR_NONE) {
            CL_GC_PROTECT(expr);
            bytecode = cl_compile(expr);
            CL_GC_UNPROTECT(1);
            if (!CL_NULL_P(bytecode))
                cl_vm_eval(bytecode);
            CL_UNCATCH();
        } else {
            cl_error_print();
            CL_UNCATCH();
        }
    }

    /* Restore source file context */
    cl_current_source_file = prev_file;
    cl_current_file_id = prev_file_id;

    platform_free(buf);
    return SYM_T;
}

/* --- Eval / Macroexpand --- */

static CL_Obj bi_eval(CL_Obj *args, int n)
{
    CL_Obj bytecode;
    CL_UNUSED(n);
    CL_GC_PROTECT(args[0]);
    bytecode = cl_compile(args[0]);
    CL_GC_UNPROTECT(1);
    if (CL_NULL_P(bytecode)) return CL_NIL;
    return cl_vm_eval(bytecode);
}

static CL_Obj bi_macroexpand_1(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return cl_macroexpand_1(args[0]);
}

static CL_Obj bi_macroexpand(CL_Obj *args, int n)
{
    CL_Obj form = args[0];
    CL_UNUSED(n);
    for (;;) {
        CL_Obj expanded = cl_macroexpand_1(form);
        if (expanded == form) return form;
        form = expanded;
    }
}

/* --- Throw --- */

static CL_Obj bi_throw(CL_Obj *args, int n)
{
    CL_Obj tag = args[0];
    CL_Obj value = (n > 1) ? args[1] : CL_NIL;
    int i;

    /* Scan NLX stack for matching catch */
    for (i = cl_nlx_top - 1; i >= 0; i--) {
        if (cl_nlx_stack[i].type == CL_NLX_CATCH &&
            cl_nlx_stack[i].tag == tag) {
            int j;
            /* Check for interposing UWPROT frames */
            for (j = cl_nlx_top - 1; j > i; j--) {
                if (cl_nlx_stack[j].type == CL_NLX_UWPROT) {
                    /* Set pending throw, longjmp to UWPROT */
                    cl_pending_throw = 1;
                    cl_pending_tag = tag;
                    cl_pending_value = value;
                    cl_nlx_top = j;
                    longjmp(cl_nlx_stack[j].buf, 1);
                }
            }
            /* No interposing UWPROT — go directly to catch */
            cl_nlx_stack[i].result = value;
            cl_nlx_top = i;
            longjmp(cl_nlx_stack[i].buf, 1);
        }
    }

    cl_error(CL_ERR_GENERAL, "No catch for tag");
    return CL_NIL;
}

/* --- Multiple Values --- */

static CL_Obj bi_values(CL_Obj *args, int n)
{
    int i;
    int count = n < CL_MAX_MV ? n : CL_MAX_MV;
    for (i = 0; i < count; i++)
        cl_mv_values[i] = args[i];
    cl_mv_count = count;
    return n > 0 ? args[0] : CL_NIL;
}

static CL_Obj bi_values_list(CL_Obj *args, int n)
{
    CL_Obj list = args[0];
    int count = 0;
    CL_UNUSED(n);
    while (!CL_NULL_P(list) && count < CL_MAX_MV) {
        cl_mv_values[count++] = cl_car(list);
        list = cl_cdr(list);
    }
    cl_mv_count = count;
    return count > 0 ? cl_mv_values[0] : CL_NIL;
}

/* --- Gensym --- */

static uint32_t gensym_counter = 0;

static CL_Obj bi_gensym(CL_Obj *args, int n)
{
    char buf[64];
    const char *prefix = "G";
    CL_Obj name_str, sym;
    int len;

    if (n > 0 && CL_STRING_P(args[0])) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
        prefix = s->data;
    }

    /* Manual int-to-string for vbcc compatibility */
    len = snprintf(buf, sizeof(buf), "%s%lu", prefix, (unsigned long)gensym_counter++);
    name_str = cl_make_string(buf, (uint32_t)len);
    CL_GC_PROTECT(name_str);
    sym = cl_make_symbol(name_str);  /* Uninterned — not in any package */
    CL_GC_UNPROTECT(1);
    return sym;
}

/* --- Disassemble --- */

typedef struct {
    const char *name;
    uint8_t arg_type;
} DisasmInfo;

static DisasmInfo disasm_opcode_info(uint8_t op)
{
    DisasmInfo info;
    info.name = NULL;
    info.arg_type = OP_ARG_NONE;

    switch (op) {
    case OP_CONST:      info.name = "CONST";      info.arg_type = OP_ARG_U16; break;
    case OP_LOAD:       info.name = "LOAD";       info.arg_type = OP_ARG_U8;  break;
    case OP_STORE:      info.name = "STORE";      info.arg_type = OP_ARG_U8;  break;
    case OP_GLOAD:      info.name = "GLOAD";      info.arg_type = OP_ARG_U16; break;
    case OP_GSTORE:     info.name = "GSTORE";     info.arg_type = OP_ARG_U16; break;
    case OP_UPVAL:      info.name = "UPVAL";      info.arg_type = OP_ARG_U8;  break;
    case OP_POP:        info.name = "POP";        break;
    case OP_DUP:        info.name = "DUP";        break;
    case OP_CONS:       info.name = "CONS";       break;
    case OP_CAR:        info.name = "CAR";        break;
    case OP_CDR:        info.name = "CDR";        break;
    case OP_ADD:        info.name = "ADD";        break;
    case OP_SUB:        info.name = "SUB";        break;
    case OP_MUL:        info.name = "MUL";        break;
    case OP_DIV:        info.name = "DIV";        break;
    case OP_EQ:         info.name = "EQ";         break;
    case OP_LT:         info.name = "LT";         break;
    case OP_GT:         info.name = "GT";         break;
    case OP_LE:         info.name = "LE";         break;
    case OP_GE:         info.name = "GE";         break;
    case OP_NUMEQ:      info.name = "NUMEQ";     break;
    case OP_NOT:        info.name = "NOT";        break;
    case OP_JMP:        info.name = "JMP";        info.arg_type = OP_ARG_I16; break;
    case OP_JNIL:       info.name = "JNIL";       info.arg_type = OP_ARG_I16; break;
    case OP_JTRUE:      info.name = "JTRUE";      info.arg_type = OP_ARG_I16; break;
    case OP_CALL:       info.name = "CALL";       info.arg_type = OP_ARG_U8;  break;
    case OP_TAILCALL:   info.name = "TAILCALL";   info.arg_type = OP_ARG_U8;  break;
    case OP_RET:        info.name = "RET";        break;
    case OP_CLOSURE:    info.name = "CLOSURE";    info.arg_type = OP_ARG_U16; break;
    case OP_APPLY:      info.name = "APPLY";      break;
    case OP_LIST:       info.name = "LIST";       info.arg_type = OP_ARG_U8;  break;
    case OP_NIL:        info.name = "NIL";        break;
    case OP_T:          info.name = "T";          break;
    case OP_FLOAD:      info.name = "FLOAD";      info.arg_type = OP_ARG_U16; break;
    case OP_DEFMACRO:   info.name = "DEFMACRO";   info.arg_type = OP_ARG_U16; break;
    case OP_DEFTYPE:    info.name = "DEFTYPE";    info.arg_type = OP_ARG_U16; break;
    case OP_HANDLER_PUSH: info.name = "HANDLER_PUSH"; info.arg_type = OP_ARG_U16; break;
    case OP_HANDLER_POP:  info.name = "HANDLER_POP";  info.arg_type = OP_ARG_U8;  break;
    case OP_RESTART_PUSH: info.name = "RESTART_PUSH"; info.arg_type = OP_ARG_U16; break;
    case OP_RESTART_POP:  info.name = "RESTART_POP";  info.arg_type = OP_ARG_U8;  break;
    case OP_ASSERT_TYPE:  info.name = "ASSERT_TYPE";  info.arg_type = OP_ARG_U16; break;
    case OP_ARGC:       info.name = "ARGC";       break;
    case OP_CATCH:      info.name = "CATCH";      info.arg_type = OP_ARG_I16; break;
    case OP_UNCATCH:    info.name = "UNCATCH";    break;
    case OP_UWPROT:     info.name = "UWPROT";     info.arg_type = OP_ARG_I16; break;
    case OP_UWPOP:      info.name = "UWPOP";      break;
    case OP_UWRETHROW:  info.name = "UWRETHROW";  break;
    case OP_MV_LOAD:    info.name = "MV_LOAD";    info.arg_type = OP_ARG_U8;  break;
    case OP_MV_TO_LIST: info.name = "MV_TO_LIST"; break;
    case OP_NTH_VALUE:  info.name = "NTH_VALUE";  break;
    case OP_DYNBIND:    info.name = "DYNBIND";    info.arg_type = OP_ARG_U16; break;
    case OP_DYNUNBIND:  info.name = "DYNUNBIND";  info.arg_type = OP_ARG_U8;  break;
    case OP_RPLACA:     info.name = "RPLACA";     break;
    case OP_RPLACD:     info.name = "RPLACD";     break;
    case OP_ASET:       info.name = "ASET";       break;
    case OP_HALT:       info.name = "HALT";       break;
    default: break;
    }
    return info;
}

static void disasm_bytecode(CL_Bytecode *bc)
{
    uint8_t *code = bc->code;
    uint32_t len = bc->code_len;
    uint32_t ip = 0;
    char line[256];
    char annot[128];

    /* Header */
    platform_write_string("Disassembly of ");
    if (!CL_NULL_P(bc->name)) {
        cl_prin1_to_string(bc->name, annot, sizeof(annot));
        platform_write_string(annot);
    } else {
        platform_write_string("#<anonymous>");
    }
    platform_write_string(":\n");

    /* Metadata */
    {
        int req = bc->arity & 0x7FFF;
        int has_rest = (bc->arity >> 15) & 1;
        snprintf(line, sizeof(line), "  %d required, %d optional, %d key%s%s\n",
                req, (int)bc->n_optional, (int)bc->n_keys,
                has_rest ? ", &rest" : "",
                (bc->flags & 2) ? ", &allow-other-keys" : "");
        platform_write_string(line);
        snprintf(line, sizeof(line), "  %lu locals, %lu upvalues\n",
                (unsigned long)bc->n_locals, (unsigned long)bc->n_upvalues);
        platform_write_string(line);
        snprintf(line, sizeof(line), "  %lu bytes, %lu constants\n\n",
                (unsigned long)len, (unsigned long)bc->n_constants);
        platform_write_string(line);
    }

    /* Instructions */
    while (ip < len) {
        uint32_t start_ip = ip;
        uint8_t op = code[ip++];
        DisasmInfo info = disasm_opcode_info(op);

        if (!info.name) {
            snprintf(line, sizeof(line), "  %04lu: ???          0x%02X\n",
                    (unsigned long)start_ip, (unsigned int)op);
            platform_write_string(line);
            continue;
        }

        switch (info.arg_type) {
        case OP_ARG_NONE:
            snprintf(line, sizeof(line), "  %04lu: %s\n",
                    (unsigned long)start_ip, info.name);
            platform_write_string(line);
            break;

        case OP_ARG_U8: {
            uint8_t val = code[ip++];
            snprintf(line, sizeof(line), "  %04lu: %-12s %u\n",
                    (unsigned long)start_ip, info.name, (unsigned int)val);
            platform_write_string(line);
            break;
        }

        case OP_ARG_U16: {
            uint16_t val = (uint16_t)((code[ip] << 8) | code[ip + 1]);
            ip += 2;
            annot[0] = '\0';
            if (val < bc->n_constants &&
                (op == OP_CONST || op == OP_GLOAD || op == OP_GSTORE ||
                 op == OP_FLOAD || op == OP_DEFMACRO || op == OP_DEFTYPE || op == OP_DYNBIND ||
                 op == OP_CLOSURE || op == OP_HANDLER_PUSH || op == OP_ASSERT_TYPE)) {
                cl_prin1_to_string(bc->constants[val], annot, sizeof(annot));
            }
            if (annot[0]) {
                snprintf(line, sizeof(line), "  %04lu: %-12s %-4u ; %s\n",
                        (unsigned long)start_ip, info.name,
                        (unsigned int)val, annot);
            } else {
                snprintf(line, sizeof(line), "  %04lu: %-12s %u\n",
                        (unsigned long)start_ip, info.name, (unsigned int)val);
            }
            platform_write_string(line);

            /* OP_CLOSURE: read and print capture descriptors */
            if (op == OP_CLOSURE && val < bc->n_constants) {
                CL_Obj tmpl = bc->constants[val];
                if (CL_BYTECODE_P(tmpl)) {
                    CL_Bytecode *tmpl_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(tmpl);
                    int ci;
                    for (ci = 0; ci < tmpl_bc->n_upvalues; ci++) {
                        uint8_t is_local = code[ip++];
                        uint8_t cap_idx = code[ip++];
                        snprintf(line, sizeof(line),
                                "          capture %d: %s slot %u\n",
                                ci, is_local ? "local" : "upval",
                                (unsigned int)cap_idx);
                        platform_write_string(line);
                    }
                }
            }
            break;
        }

        case OP_ARG_I16: {
            int16_t val = (int16_t)((code[ip] << 8) | code[ip + 1]);
            ip += 2;
            snprintf(line, sizeof(line), "  %04lu: %-12s %+d    ; -> %04lu\n",
                    (unsigned long)start_ip, info.name, (int)val,
                    (unsigned long)((int32_t)ip + (int32_t)val));
            platform_write_string(line);
            break;
        }
        }
    }

    /* Constants pool */
    if (bc->n_constants > 0) {
        uint16_t ci;
        platform_write_string("\nConstants:\n");
        for (ci = 0; ci < bc->n_constants; ci++) {
            cl_prin1_to_string(bc->constants[ci], annot, sizeof(annot));
            snprintf(line, sizeof(line), "  %u: %s\n",
                    (unsigned int)ci, annot);
            platform_write_string(line);
        }
    }
    platform_write_string("\n");
}

static CL_Obj bi_disassemble(CL_Obj *args, int n)
{
    CL_Obj arg = args[0];
    CL_Bytecode *bc = NULL;
    CL_UNUSED(n);

    /* Accept symbol — resolve to function binding (same fallback as OP_FLOAD) */
    if (CL_SYMBOL_P(arg)) {
        CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(arg);
        if (sym->function != CL_UNBOUND && !CL_NULL_P(sym->function))
            arg = sym->function;
        else if (sym->value != CL_UNBOUND && !CL_NULL_P(sym->value))
            arg = sym->value;
        else
            cl_error(CL_ERR_UNDEFINED, "DISASSEMBLE: no function binding");
    }

    if (CL_BYTECODE_P(arg)) {
        bc = (CL_Bytecode *)CL_OBJ_TO_PTR(arg);
    } else if (CL_CLOSURE_P(arg)) {
        CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(arg);
        if (CL_BYTECODE_P(cl->bytecode))
            bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
    } else if (CL_FUNCTION_P(arg)) {
        CL_Function *fn = (CL_Function *)CL_OBJ_TO_PTR(arg);
        char nbuf[128];
        platform_write_string("Built-in function: ");
        cl_prin1_to_string(fn->name, nbuf, sizeof(nbuf));
        platform_write_string(nbuf);
        platform_write_string("\n  (no bytecode to disassemble)\n");
        return CL_NIL;
    }

    if (!bc)
        cl_error(CL_ERR_TYPE, "DISASSEMBLE: argument must be a function or symbol");

    disasm_bytecode(bc);
    return CL_NIL;
}

/* --- Timing --- */

static CL_Obj bi_get_internal_time(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return CL_MAKE_FIXNUM((int32_t)(platform_time_ms() & 0x7FFFFFFF));
}

static CL_Obj bi_get_bytes_consed(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return CL_MAKE_FIXNUM((int32_t)(cl_heap.total_consed & 0x7FFFFFFF));
}

static CL_Obj bi_get_gc_count(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return CL_MAKE_FIXNUM((int32_t)(cl_heap.gc_count & 0x7FFFFFFF));
}

static CL_Obj bi_time_report(CL_Obj *args, int n)
{
    uint32_t start_time, end_time, elapsed;
    uint32_t start_consed, end_consed, bytes_consed;
    uint32_t start_gc, end_gc, gc_cycles;
    char buf[256];
    CL_UNUSED(n);

    start_time = (uint32_t)CL_FIXNUM_VAL(args[0]);
    start_consed = (uint32_t)CL_FIXNUM_VAL(args[1]);
    start_gc = (uint32_t)CL_FIXNUM_VAL(args[2]);

    end_time = platform_time_ms() & 0x7FFFFFFF;
    end_consed = cl_heap.total_consed & 0x7FFFFFFF;
    end_gc = cl_heap.gc_count & 0x7FFFFFFF;

    elapsed = (end_time >= start_time)
        ? (end_time - start_time)
        : ((0x7FFFFFFF - start_time) + end_time + 1);
    bytes_consed = (end_consed >= start_consed)
        ? (end_consed - start_consed)
        : ((0x7FFFFFFF - start_consed) + end_consed + 1);
    gc_cycles = (end_gc >= start_gc)
        ? (end_gc - start_gc)
        : ((0x7FFFFFFF - start_gc) + end_gc + 1);

    snprintf(buf, sizeof(buf),
             "Evaluation took %lu ms; %lu bytes consed; %lu GC cycles; %lu/%lu heap bytes used.\n",
             (unsigned long)elapsed,
             (unsigned long)bytes_consed,
             (unsigned long)gc_cycles,
             (unsigned long)cl_heap.total_allocated,
             (unsigned long)cl_heap.arena_size);
    platform_write_string(buf);
    return CL_NIL;
}

static CL_Obj bi_get_internal_real_time(CL_Obj *args, int n)
{
    CL_UNUSED(args); CL_UNUSED(n);
    return CL_MAKE_FIXNUM((int32_t)(platform_time_ms() & 0x7FFFFFFF));
}

/* --- Registration --- */

void cl_builtins_io_init(void)
{
    /* I/O */
    defun("PRINT", bi_print, 1, 1);
    defun("PRIN1", bi_prin1, 1, 1);
    defun("PRINC", bi_princ, 1, 1);
    defun("TERPRI", bi_terpri, 0, 0);
    defun("FORMAT", bi_format, 1, -1);

    /* Load / Eval */
    defun("LOAD", bi_load, 1, 1);
    defun("EVAL", bi_eval, 1, 1);
    defun("MACROEXPAND-1", bi_macroexpand_1, 1, 1);
    defun("MACROEXPAND", bi_macroexpand, 1, 1);

    /* Throw (ERROR is now in builtins_condition.c) */
    defun("THROW", bi_throw, 1, 2);

    /* Multiple values */
    defun("VALUES", bi_values, 0, -1);
    defun("VALUES-LIST", bi_values_list, 1, 1);

    /* Gensym */
    defun("GENSYM", bi_gensym, 0, 1);

    /* Debugging */
    defun("DISASSEMBLE", bi_disassemble, 1, 1);

    /* Timing (internal helpers for TIME special form) */
    defun("%GET-INTERNAL-TIME", bi_get_internal_time, 0, 0);
    defun("%GET-BYTES-CONSED", bi_get_bytes_consed, 0, 0);
    defun("%GET-GC-COUNT", bi_get_gc_count, 0, 0);
    defun("%TIME-REPORT", bi_time_report, 3, 3);
    defun("GET-INTERNAL-REAL-TIME", bi_get_internal_real_time, 0, 0);
}
