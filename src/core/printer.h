#ifndef CL_PRINTER_H
#define CL_PRINTER_H

#include "types.h"

/*
 * Object printer (prin1/princ style).
 * prin1 = with escape chars (readable)
 * princ = without escape chars (human)
 */

/* Print object to a stream (prin1 style, readable) */
void cl_prin1_to_stream(CL_Obj obj, CL_Obj stream);

/* Print object to a stream (princ style, human-readable) */
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

#endif /* CL_PRINTER_H */
