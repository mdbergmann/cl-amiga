#ifndef CL_PEEPHOLE_H
#define CL_PEEPHOLE_H

/*
 * Bytecode peephole post-pass (spec 1.8), gated on (optimize (speed >= 2)).
 * See peephole.c for the full design notes.  Fail-safe: on anything it does
 * not fully understand it leaves the bytecode untouched.
 */

#include <stdint.h>
#include "types.h"   /* CL_Obj, CL_LineEntry */

/* Functions larger than this skip the pass (the pc-keyed line map is
 * uint16_t anyway, and the temporary instruction list for a buffer this
 * size is already ~1.2MB — too much to ask of an 8MB Amiga). */
#define CL_PEEPHOLE_MAX_CODE 60000u

struct CL_Compiler_s;

/* Optimize C's emitted bytecode in place if its effective (declare
 * (optimize (speed ...))) high-water mark is >= 2.  Called from the two
 * bytecode finalization sites (compile_lambda, cl_compile_env) after the
 * final OP_RET/OP_HALT is emitted and all jumps are patched. */
void cl_peephole_optimize(struct CL_Compiler_s *c);

/* Raw engine (exposed for unit tests): rewrite CODE[0..*CODE_LEN) in place,
 * shrink-only.  CONSTANTS/N_CONSTANTS are needed to decode OP_CLOSURE's
 * variable-length capture descriptors.  LINES/N_LINES (may be NULL/NULL) is
 * the pc-keyed source line map, remapped in place.  Returns 1 if the code
 * changed, 0 if nothing applied or the pass bailed out. */
int cl_peephole_run(uint8_t *code, int *code_len,
                    const CL_Obj *constants, int n_constants,
                    CL_LineEntry *lines, int *n_lines);

#endif /* CL_PEEPHOLE_H */
