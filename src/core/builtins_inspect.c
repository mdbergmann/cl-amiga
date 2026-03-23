/*
 * builtins_inspect.c — Interactive INSPECT function
 *
 * (inspect object) — enters an interactive inspector allowing the user
 * to navigate into sub-components of an object.
 *
 * Per the HyperSpec, inspect is implementation-defined in its interactive
 * behavior.  This implementation provides a mini-REPL with numbered
 * components, navigation commands, and expression evaluation.
 */

#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "printer.h"
#include "stream.h"
#include "vm.h"
#include "color.h"
#include "repl.h"
#include "string_utils.h"
#include "../platform/platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Describe helper (exported from builtins_describe.c) */
extern void cl_describe_to_stream(CL_Obj obj, CL_Obj stream);

/* Struct slot names (used in builtins_describe.c too) */
extern CL_Obj cl_struct_slot_names(CL_Obj type_name);

/* --- Navigation stack --- */

#define INSPECT_MAX_DEPTH 32

typedef struct {
    CL_Obj object;
    const char *label;
} InspectFrame;

typedef struct {
    InspectFrame stack[INSPECT_MAX_DEPTH];
    int depth;
} InspectState;

/* --- Helper to register a builtin --- */

static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    s->value = fn;
}

/* --- Output helpers --- */

static void write_str(const char *str)
{
    platform_write_string(str);
}

static void write_obj_buf(CL_Obj obj)
{
    char buf[256];
    cl_prin1_to_string(obj, buf, sizeof(buf));
    write_str(buf);
}

/* --- Hash table bucket accessor (duplicated from builtins_hashtable.c) --- */

static inline CL_Obj *inspect_ht_buckets(CL_Hashtable *ht)
{
    if (!CL_NULL_P(ht->bucket_vec)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(ht->bucket_vec);
        return v->data;
    }
    return ht->buckets;
}

/* --- Component count by type --- */

/* Returns the number of navigable components for an object.
 * For types with no sub-components (leaf types), returns 0. */
int cl_inspect_component_count(CL_Obj obj)
{
    if (CL_NULL_P(obj) || CL_FIXNUM_P(obj) || CL_CHAR_P(obj))
        return 0;

    /* Use if-else instead of switch to avoid m68k-amigaos-gcc LTO
     * jump table bug (undefined .L labels during link) */
    if (CL_CONS_P(obj))
        return 2;
    if (CL_SYMBOL_P(obj))
        return 5;
    if (CL_STRING_P(obj))
        return (int)((CL_String *)CL_OBJ_TO_PTR(obj))->length;
    if (CL_VECTOR_P(obj))
        return (int)cl_vector_active_length((CL_Vector *)CL_OBJ_TO_PTR(obj));
    if (CL_STRUCT_P(obj))
        return (int)((CL_Struct *)CL_OBJ_TO_PTR(obj))->n_slots;
    if (CL_HASHTABLE_P(obj))
        return (int)((CL_Hashtable *)CL_OBJ_TO_PTR(obj))->count;
    if (CL_CONDITION_P(obj))
        return 3;
    if (CL_PATHNAME_P(obj))
        return 6;
    if (CL_RATIO_P(obj))
        return 2;
    if (CL_COMPLEX_P(obj))
        return 2;
    if (CL_CLOSURE_P(obj)) {
        CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(obj);
        int n = 1;
        if (CL_BYTECODE_P(cl->bytecode)) {
            CL_Bytecode *bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
            n += bc->n_upvalues;
        }
        return n;
    }
    if (CL_PACKAGE_P(obj))
        return 3;
    if (CL_FUNCTION_P(obj))
        return 1;
    return 0;
}

/* --- Component access by index --- */

/* Returns the component at index `idx`, and sets *label to a descriptive
 * string (static storage).  Returns CL_NIL if out of range. */
CL_Obj cl_inspect_get_component(CL_Obj obj, int idx, const char **label)
{
    static const char *sym_labels[] = {
        "Name", "Value", "Function", "Plist", "Package"
    };
    static const char *pn_labels[] = {
        "Host", "Device", "Directory", "Name", "Type", "Version"
    };
    static const char *cond_labels[] = {
        "Type-name", "Report", "Slots"
    };
    static char idx_label[32];

    *label = "?";

    if (CL_CONS_P(obj)) {
        if (idx == 0) { *label = "Car"; return cl_car(obj); }
        if (idx == 1) { *label = "Cdr"; return cl_cdr(obj); }
        return CL_NIL;
    }

    if (CL_SYMBOL_P(obj)) {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(obj);
        if (idx < 0 || idx > 4) return CL_NIL;
        *label = sym_labels[idx];
        if (idx == 0) return s->name;
        if (idx == 1) return (s->value != CL_UNBOUND) ? cl_symbol_value(obj) : CL_NIL;
        if (idx == 2) return (s->function != CL_UNBOUND) ? s->function : CL_NIL;
        if (idx == 3) return s->plist;
        if (idx == 4) return s->package;
    }

    if (CL_ANY_STRING_P(obj)) {
        if (idx < 0 || (uint32_t)idx >= cl_string_length(obj)) return CL_NIL;
        snprintf(idx_label, sizeof(idx_label), "[%d]", idx);
        *label = idx_label;
        return CL_MAKE_CHAR(cl_string_char_at(obj, (uint32_t)idx));
    }

    if (CL_VECTOR_P(obj)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
        uint32_t alen = cl_vector_active_length(v);
        if (idx < 0 || (uint32_t)idx >= alen) return CL_NIL;
        snprintf(idx_label, sizeof(idx_label), "[%d]", idx);
        *label = idx_label;
        return cl_vector_data(v)[idx];
    }

    if (CL_STRUCT_P(obj)) {
        CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
        if (idx < 0 || (uint32_t)idx >= st->n_slots) return CL_NIL;
        /* Try to get slot name */
        {
            CL_Obj names = cl_struct_slot_names(st->type_desc);
            int i = 0;
            while (!CL_NULL_P(names) && i < idx) {
                names = cl_cdr(names);
                i++;
            }
            if (!CL_NULL_P(names)) {
                /* Use the static idx_label buffer for now */
                CL_Obj name_sym = cl_car(names);
                char nbuf[64];
                cl_prin1_to_string(name_sym, nbuf, sizeof(nbuf));
                snprintf(idx_label, sizeof(idx_label), "%.30s", nbuf);
                *label = idx_label;
            } else {
                snprintf(idx_label, sizeof(idx_label), "slot-%d", idx);
                *label = idx_label;
            }
        }
        return st->slots[idx];
    }

    if (CL_HASHTABLE_P(obj)) {
        CL_Hashtable *ht = (CL_Hashtable *)CL_OBJ_TO_PTR(obj);
        CL_Obj *bkts = inspect_ht_buckets(ht);
        uint32_t count = 0;
        uint32_t i;
        for (i = 0; i < ht->bucket_count; i++) {
            CL_Obj chain = bkts[i];
            while (!CL_NULL_P(chain)) {
                if ((int)count == idx) {
                    CL_Obj pair = cl_car(chain);  /* (key . value) */
                    snprintf(idx_label, sizeof(idx_label), "entry-%d", idx);
                    *label = idx_label;
                    return pair;
                }
                count++;
                chain = cl_cdr(chain);
            }
        }
        return CL_NIL;
    }

    if (CL_CONDITION_P(obj)) {
        CL_Condition *c = (CL_Condition *)CL_OBJ_TO_PTR(obj);
        if (idx < 0 || idx > 2) return CL_NIL;
        *label = cond_labels[idx];
        if (idx == 0) return c->type_name;
        if (idx == 1) return c->report_string;
        if (idx == 2) return c->slots;
    }

    if (CL_PATHNAME_P(obj)) {
        CL_Pathname *pn = (CL_Pathname *)CL_OBJ_TO_PTR(obj);
        if (idx < 0 || idx > 5) return CL_NIL;
        *label = pn_labels[idx];
        if (idx == 0) return pn->host;
        if (idx == 1) return pn->device;
        if (idx == 2) return pn->directory;
        if (idx == 3) return pn->name;
        if (idx == 4) return pn->type;
        if (idx == 5) return pn->version;
    }

    if (CL_RATIO_P(obj)) {
        CL_Ratio *r = (CL_Ratio *)CL_OBJ_TO_PTR(obj);
        if (idx == 0) { *label = "Numerator"; return r->numerator; }
        if (idx == 1) { *label = "Denominator"; return r->denominator; }
        return CL_NIL;
    }

    if (CL_COMPLEX_P(obj)) {
        CL_Complex *c = (CL_Complex *)CL_OBJ_TO_PTR(obj);
        if (idx == 0) { *label = "Realpart"; return c->realpart; }
        if (idx == 1) { *label = "Imagpart"; return c->imagpart; }
        return CL_NIL;
    }

    if (CL_CLOSURE_P(obj)) {
        CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(obj);
        if (idx == 0) { *label = "Bytecode"; return cl->bytecode; }
        if (CL_BYTECODE_P(cl->bytecode)) {
            CL_Bytecode *bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
            int uv_idx = idx - 1;
            if (uv_idx >= 0 && uv_idx < bc->n_upvalues) {
                snprintf(idx_label, sizeof(idx_label), "upvalue-%d", uv_idx);
                *label = idx_label;
                return cl->upvalues[uv_idx];
            }
        }
        return CL_NIL;
    }

    if (CL_PACKAGE_P(obj)) {
        CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(obj);
        if (idx == 0) { *label = "Name"; return pkg->name; }
        if (idx == 1) { *label = "Nicknames"; return pkg->nicknames; }
        if (idx == 2) { *label = "Use-list"; return pkg->use_list; }
        return CL_NIL;
    }

    if (CL_FUNCTION_P(obj)) {
        CL_Function *fn = (CL_Function *)CL_OBJ_TO_PTR(obj);
        if (idx == 0) { *label = "Name"; return fn->name; }
        return CL_NIL;
    }

    return CL_NIL;
}

/* --- Display current object with components --- */

static void inspect_show(InspectState *state)
{
    CL_Obj obj = state->stack[state->depth].object;
    int ncomp, show_max, i;
    char buf[32];

    /* Header */
    cl_color_set(CL_COLOR_DIM_CYAN);
    write_str("\nInspecting: ");
    cl_color_reset();
    write_obj_buf(obj);
    write_str("  [");
    write_str(cl_type_name(obj));
    write_str("]\n");

    /* Navigation breadcrumb */
    if (state->depth > 0) {
        cl_color_set(CL_COLOR_YELLOW);
        write_str("  Path: ");
        for (i = 0; i <= state->depth; i++) {
            if (i > 0) write_str(" > ");
            write_str(state->stack[i].label);
        }
        write_str("\n");
        cl_color_reset();
    }

    /* Components */
    ncomp = cl_inspect_component_count(obj);
    if (ncomp == 0) {
        write_str("  (no navigable components)\n");
        return;
    }

    /* Cap display at 20 */
    show_max = ncomp > 20 ? 20 : ncomp;

    for (i = 0; i < show_max; i++) {
        const char *label;
        CL_Obj comp = cl_inspect_get_component(obj, i, &label);

        snprintf(buf, sizeof(buf), "  %2d: ", i);
        cl_color_set(CL_COLOR_DIM_GREEN);
        write_str(buf);
        write_str(label);
        cl_color_reset();
        write_str(" = ");
        write_obj_buf(comp);
        write_str("\n");
    }

    if (ncomp > 20) {
        snprintf(buf, sizeof(buf), "  ... (%d more)\n", ncomp - 20);
        write_str(buf);
    }
}

/* --- Help --- */

static void inspect_help(void)
{
    write_str("Inspector commands:\n");
    write_str("  <n>     navigate into component n\n");
    write_str("  u       go up one level\n");
    write_str("  r       return to root object\n");
    write_str("  d       describe current object\n");
    write_str("  p       print (prin1) current object\n");
    write_str("  s       re-display current object\n");
    write_str("  e <expr> evaluate a Lisp expression\n");
    write_str("  q       quit inspector\n");
    write_str("  h, ?    show this help\n");
}

/* --- Main interactive loop --- */

static void inspect_loop(InspectState *state)
{
    char line[256];

    inspect_show(state);
    inspect_help();

    cl_color_set(CL_COLOR_DIM_CYAN);
    write_str("Inspect> ");
    cl_color_reset();

    while (platform_read_line(line, sizeof(line))) {
        /* Skip empty lines */
        if (line[0] == '\0') {
            cl_color_set(CL_COLOR_DIM_CYAN);
            write_str("Inspect> ");
            cl_color_reset();
            continue;
        }

        /* Quit */
        if (strcmp(line, "q") == 0)
            return;

        /* Help */
        if (strcmp(line, "h") == 0 || strcmp(line, "?") == 0) {
            inspect_help();
            goto prompt;
        }

        /* Go up */
        if (strcmp(line, "u") == 0) {
            if (state->depth > 0) {
                CL_GC_UNPROTECT(1);
                state->depth--;
                inspect_show(state);
            } else {
                write_str("Already at root.\n");
            }
            goto prompt;
        }

        /* Return to root */
        if (strcmp(line, "r") == 0) {
            while (state->depth > 0) {
                CL_GC_UNPROTECT(1);
                state->depth--;
            }
            inspect_show(state);
            goto prompt;
        }

        /* Describe */
        if (strcmp(line, "d") == 0) {
            CL_Obj out = cl_symbol_value(
                cl_intern_in("*STANDARD-OUTPUT*", 17, cl_package_cl));
            cl_describe_to_stream(state->stack[state->depth].object, out);
            goto prompt;
        }

        /* Print */
        if (strcmp(line, "p") == 0) {
            write_obj_buf(state->stack[state->depth].object);
            write_str("\n");
            goto prompt;
        }

        /* Re-display */
        if (strcmp(line, "s") == 0) {
            inspect_show(state);
            goto prompt;
        }

        /* Eval expression */
        if (line[0] == 'e' && line[1] == ' ') {
            int err;
            cl_vm.sp = 0;
            cl_vm.fp = 0;

            err = CL_CATCH();
            if (err == CL_ERR_NONE) {
                CL_Obj result = cl_eval_string(line + 2);
                char buf[512];
                cl_prin1_to_string(result, buf, sizeof(buf));
                write_str(buf);
                write_str("\n");
                CL_UNCATCH();
            } else {
                CL_UNCATCH();
                cl_color_set(CL_COLOR_RED);
                write_str("Error: ");
                write_str(cl_error_msg);
                cl_color_reset();
                write_str("\n");
                cl_vm.sp = 0;
                cl_vm.fp = 0;
            }
            goto prompt;
        }

        /* Try parsing as number (component index) */
        {
            char *endp;
            long idx = strtol(line, &endp, 10);
            if (endp != line && *endp == '\0') {
                CL_Obj obj = state->stack[state->depth].object;
                int ncomp = cl_inspect_component_count(obj);

                if (idx >= 0 && idx < ncomp) {
                    const char *label;
                    CL_Obj comp = cl_inspect_get_component(obj, (int)idx, &label);

                    if (state->depth + 1 >= INSPECT_MAX_DEPTH) {
                        write_str("Maximum inspection depth reached.\n");
                    } else {
                        state->depth++;
                        state->stack[state->depth].object = comp;
                        state->stack[state->depth].label = label;
                        CL_GC_PROTECT(state->stack[state->depth].object);
                        inspect_show(state);
                    }
                } else {
                    char ebuf[64];
                    snprintf(ebuf, sizeof(ebuf),
                             "Invalid index %ld (0-%d).\n", idx, ncomp - 1);
                    write_str(ebuf);
                }
                goto prompt;
            }
        }

        /* Unknown command */
        write_str("Unknown command. Type 'h' for help.\n");

    prompt:
        cl_color_set(CL_COLOR_DIM_CYAN);
        write_str("Inspect> ");
        cl_color_reset();
    }
}

/* --- Builtin: (inspect object) --- */

static CL_Obj bi_inspect(CL_Obj *args, int n)
{
    InspectState state;
    CL_UNUSED(n);

    state.depth = 0;
    state.stack[0].object = args[0];
    state.stack[0].label = "root";
    CL_GC_PROTECT(state.stack[0].object);

    inspect_loop(&state);

    /* Unprotect any remaining frames */
    CL_GC_UNPROTECT(state.depth + 1);

    cl_mv_count = 0;
    return CL_NIL;
}

/* --- Registration --- */

void cl_builtins_inspect_init(void)
{
    defun("INSPECT", bi_inspect, 1, 1);
}
