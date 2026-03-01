#include "printer.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <string.h>

/* Output mode */
static int escape_mode = 1;   /* 1 = prin1, 0 = princ */

/* Output targets */
static int to_buffer = 0;
static char *out_buf = NULL;
static int out_pos = 0;
static int out_size = 0;

static void out_char(int ch)
{
    if (to_buffer) {
        if (out_pos < out_size - 1)
            out_buf[out_pos++] = (char)ch;
    } else {
        char c[2] = { (char)ch, '\0' };
        platform_write_string(c);
    }
}

static void out_str(const char *s)
{
    while (*s) out_char(*s++);
}

static void out_int(int32_t val)
{
    char buf[16];
    sprintf(buf, "%d", (int)val);
    out_str(buf);
}

static void print_obj(CL_Obj obj);

static void print_list(CL_Obj obj)
{
    out_char('(');
    print_obj(cl_car(obj));
    obj = cl_cdr(obj);

    while (CL_CONS_P(obj)) {
        out_char(' ');
        print_obj(cl_car(obj));
        obj = cl_cdr(obj);
    }

    if (!CL_NULL_P(obj)) {
        out_str(" . ");
        print_obj(obj);
    }

    out_char(')');
}

static void print_string(CL_Obj obj)
{
    CL_String *s = (CL_String *)CL_OBJ_TO_PTR(obj);
    if (escape_mode) {
        uint32_t i;
        out_char('"');
        for (i = 0; i < s->length; i++) {
            char ch = s->data[i];
            if (ch == '"' || ch == '\\') out_char('\\');
            if (ch == '\n') { out_char('\\'); out_char('n'); continue; }
            if (ch == '\t') { out_char('\\'); out_char('t'); continue; }
            out_char(ch);
        }
        out_char('"');
    } else {
        out_str(s->data);
    }
}

static void print_obj(CL_Obj obj)
{
    if (CL_NULL_P(obj)) {
        out_str("NIL");
        return;
    }

    if (CL_FIXNUM_P(obj)) {
        out_int(CL_FIXNUM_VAL(obj));
        return;
    }

    if (CL_CHAR_P(obj)) {
        int ch = CL_CHAR_VAL(obj);
        if (escape_mode) {
            out_str("#\\");
            switch (ch) {
            case ' ':  out_str("Space"); break;
            case '\n': out_str("Newline"); break;
            case '\t': out_str("Tab"); break;
            case '\r': out_str("Return"); break;
            default:   out_char(ch); break;
            }
        } else {
            out_char(ch);
        }
        return;
    }

    if (!CL_HEAP_P(obj)) {
        out_str("#<unknown>");
        return;
    }

    switch (CL_HDR_TYPE(CL_OBJ_TO_PTR(obj))) {
    case TYPE_CONS:
        print_list(obj);
        break;

    case TYPE_SYMBOL: {
        CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(obj);
        /* Print package prefix for keywords */
        if (sym->package == cl_package_keyword) {
            out_char(':');
        }
        out_str(cl_symbol_name(obj));
        break;
    }

    case TYPE_STRING:
        print_string(obj);
        break;

    case TYPE_FUNCTION: {
        CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(obj);
        out_str("#<FUNCTION ");
        if (!CL_NULL_P(f->name))
            out_str(cl_symbol_name(f->name));
        else
            out_str("anonymous");
        out_char('>');
        break;
    }

    case TYPE_CLOSURE: {
        CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(obj);
        CL_Bytecode *bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
        out_str("#<CLOSURE ");
        if (!CL_NULL_P(bc->name))
            out_str(cl_symbol_name(bc->name));
        else
            out_str("anonymous");
        out_char('>');
        break;
    }

    case TYPE_BYTECODE: {
        CL_Bytecode *bc = (CL_Bytecode *)CL_OBJ_TO_PTR(obj);
        out_str("#<COMPILED-FUNCTION ");
        if (!CL_NULL_P(bc->name))
            out_str(cl_symbol_name(bc->name));
        else
            out_str("anonymous");
        out_char('>');
        break;
    }

    case TYPE_VECTOR: {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
        uint32_t i;
        out_str("#(");
        for (i = 0; i < v->length; i++) {
            if (i > 0) out_char(' ');
            print_obj(v->data[i]);
        }
        out_char(')');
        break;
    }

    case TYPE_PACKAGE: {
        CL_Package *p = (CL_Package *)CL_OBJ_TO_PTR(obj);
        CL_String *name = (CL_String *)CL_OBJ_TO_PTR(p->name);
        out_str("#<PACKAGE ");
        out_str(name->data);
        out_char('>');
        break;
    }

    default:
        out_str("#<unknown>");
        break;
    }
}

/* Public API */

void cl_prin1(CL_Obj obj)
{
    escape_mode = 1;
    to_buffer = 0;
    print_obj(obj);
}

void cl_princ(CL_Obj obj)
{
    escape_mode = 0;
    to_buffer = 0;
    print_obj(obj);
}

void cl_print(CL_Obj obj)
{
    cl_prin1(obj);
    platform_write_string("\n");
}

int cl_prin1_to_string(CL_Obj obj, char *buf, int bufsize)
{
    escape_mode = 1;
    to_buffer = 1;
    out_buf = buf;
    out_pos = 0;
    out_size = bufsize;
    print_obj(obj);
    if (out_pos < out_size) out_buf[out_pos] = '\0';
    to_buffer = 0;
    return out_pos;
}

int cl_princ_to_string(CL_Obj obj, char *buf, int bufsize)
{
    escape_mode = 0;
    to_buffer = 1;
    out_buf = buf;
    out_pos = 0;
    out_size = bufsize;
    print_obj(obj);
    if (out_pos < out_size) out_buf[out_pos] = '\0';
    to_buffer = 0;
    return out_pos;
}

void cl_printer_init(void)
{
    /* Nothing needed yet */
}
