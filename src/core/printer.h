#ifndef CL_PRINTER_H
#define CL_PRINTER_H

#include "types.h"

/*
 * Object printer (prin1/princ style).
 * prin1 = with escape chars (readable)
 * princ = without escape chars (human)
 */

/* Print object to console (prin1 style, readable) */
void cl_prin1(CL_Obj obj);

/* Print object to console (princ style, human-readable) */
void cl_princ(CL_Obj obj);

/* Print object followed by newline */
void cl_print(CL_Obj obj);

/* Print to a buffer (returns chars written) */
int cl_prin1_to_string(CL_Obj obj, char *buf, int bufsize);
int cl_princ_to_string(CL_Obj obj, char *buf, int bufsize);

void cl_printer_init(void);

#endif /* CL_PRINTER_H */
