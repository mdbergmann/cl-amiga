#ifndef CL_PRINTER_H
#define CL_PRINTER_H

#include "types.h"

/*
 * Object printer.
 *
 * cl_write_to_stream() is the core — it honors all *print-* variables
 * as currently bound.  prin1/princ/print temporarily bind *print-escape*
 * and delegate to write.
 */

/* Snapshot of the printer's leak-prone per-thread flags, embedded in
 * CL_ErrorFrame and CL_NLXFrame.  A longjmp/THROW out of a user
 * pprint-dispatch function, print-object hook, or stream error abandons the
 * printing C frames without their epilogues; nothing else ever repairs these:
 *   - pr_pprint_dispatch_active stuck at 1 silently disables the user's
 *     pprint dispatch table forever,
 *   - a leaked pr_inprog_top makes later prints of the same objects emit
 *     "#<...>" re-entrancy markers,
 *   - a leaked pr_depth accumulates toward the hard recursion cap in nested
 *     buffer prints,
 *   - a leaked pr_circle_active reuses a stale circle table (wrong #n#/#n=).
 * Save at frame push, restore on the longjmp landing (mirrors gc_root_mark). */
typedef struct {
    int32_t depth;
    int32_t inprog_top;
    int32_t dispatch_active;
    int32_t circle_active;
} CL_PrinterState;

CL_PrinterState cl_printer_state_save(void);
void cl_printer_state_restore(CL_PrinterState s);
/* Reset to idle — for the outermost (REPL) unwind where every print in
 * flight has been abandoned. */
void cl_printer_state_reset(void);

/* Core: print object honoring current *print-* bindings */
void cl_write_to_stream(CL_Obj obj, CL_Obj stream);

/* Print object to a stream (prin1 style: *print-escape* = T) */
void cl_prin1_to_stream(CL_Obj obj, CL_Obj stream);

/* Print object to a stream (princ style: *print-escape* = NIL) */
void cl_princ_to_stream(CL_Obj obj, CL_Obj stream);

/* Print newline + object + space to a stream */
void cl_print_to_stream(CL_Obj obj, CL_Obj stream);

/* Print to *standard-output* (convenience wrappers) */
void cl_prin1(CL_Obj obj);
void cl_princ(CL_Obj obj);
void cl_print(CL_Obj obj);

/* Print to a C buffer (returns chars written).
 * Used internally by C code (vm.c, debugger.c, etc.) */
int cl_prin1_to_string(CL_Obj obj, char *buf, int bufsize);
int cl_princ_to_string(CL_Obj obj, char *buf, int bufsize);

void cl_printer_init(void);

/* Pretty-printing API */
int32_t cl_pp_get_column(void);        /* current output column */
int32_t cl_pp_get_right_margin(void);  /* effective right margin */
void cl_pp_newline_indent(void);       /* emit newline + indent */
void cl_pp_set_indent(int32_t n);      /* set top of indent stack */
void cl_pp_push_block(int32_t start_col); /* push indent + block_start */
void cl_pp_pop_block(void);            /* pop indent + block_start */

#endif /* CL_PRINTER_H */
