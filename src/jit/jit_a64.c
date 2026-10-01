/* jit_a64.c — the AArch64 native backend (specs/native-backend-a64.md).
 *
 * Phase 0: the plumbing without a code generator.  Every function is
 * declined (it stays interpreted), but the pieces the walker will need run
 * end to end: %JIT-COMPILE-STUB assembles a stub through asm_a64.c, the
 * code heap installs it, OP_CALL enters it through cl_jit_invoke, and the
 * sweep and the shutdown hand the memory back.
 *
 * Entry ABI (the spec's "Entry and frame layout"): one C signature for
 * every arity, the arguments left on cl_vm.stack where the caller pushed
 * them, so no trampoline is needed:
 *
 *     CL_Obj entry(CL_Thread *thr, CL_Obj *bp, uint32_t nargs, CL_Obj func)
 *
 * Native frames keep every live CL_Obj in cl_vm.stack (GC-rooted), so this
 * backend never sets jit_depth / jit_stack_top: the conservative native-
 * stack scan the m68k JIT needs does not apply.
 */

#include "jit/jit.h"

#ifdef JIT_A64

#include <stdio.h>
#include "jit/jit_backend.h"
#include "jit/asm_a64.h"
#include "jit/codebuf.h"
#include "jit/codeheap.h"
#include "core/mem.h"        /* cl_call_gen_bump */
#include "core/stream.h"     /* cl_write_cstring_to_stdout */
#include "core/thread.h"     /* cl_vm, cl_get_current_thread */
#include "platform/platform.h"

typedef CL_Obj (*a64_entry_t)(CL_Thread *thr, CL_Obj *bp, uint32_t nargs,
                              CL_Obj func);

void cl_jit_backend_init(void)
{
    cl_codeheap_init();
}

void cl_jit_backend_shutdown(void)
{
    cl_codeheap_shutdown();
}

void cl_jit_free_native(void *code)
{
    cl_codeheap_free(code);
}

/* Drop BC's native code and invalidate every call site that might cache
 * it: unhook first, then bump, then free (the order jit.c uses). */
static void drop_native(CL_Bytecode *bc)
{
    void *old = bc->native_code;
    if (bc->native_relocs) platform_free(bc->native_relocs);
    bc->native_relocs = NULL;
    bc->native_reloc_count = 0;
    bc->native_code = NULL;
    bc->native_len = 0;
    if (old) {
        cl_call_gen_bump("native code replaced");
        cl_codeheap_free(old);
    }
}

/* Phase 0 has no walker: every function stays interpreted.  REPLACE still
 * drops what the function carried, as the m68k backend does. */
void cl_jit_backend_compile(CL_Bytecode *bc, int replace)
{
    if (bc == NULL || !cl_jitc_active) return;
    if (replace) drop_native(bc);
}

CL_Obj cl_jit_invoke(CL_Obj func_obj, CL_Bytecode *bc, int nargs)
{
    CL_Obj result;
    CL_Thread *t;
    int32_t prev_nargs;
    int pushed_frame = 0;

    if (bc == NULL || bc->native_code == NULL) return CL_NIL;
    cl_jitc_invoke_count++;
    t = cl_get_current_thread();
    prev_nargs = t->jit_current_nargs;
    t->jit_current_nargs = (int32_t)nargs;

    /* The shadow frame of %JIT-SET-FRAMES, as in jit.c. */
    if (cl_jitc_shadow_frames && cl_vm.fp < cl_vm.frame_size) {
        CL_Frame *sf = &cl_vm.frames[cl_vm.fp++];
        sf->bytecode  = func_obj;
        sf->code      = bc->code;
        sf->constants = bc->constants;
        sf->ip        = 0;
        sf->bp        = (uint32_t)(cl_vm.sp - nargs);
        sf->n_locals  = nargs;
        sf->nargs     = (uint16_t)nargs;
        sf->nlx_level = cl_nlx_top;
        sf->fslot     = 0;
        pushed_frame  = 1;
    }

    result = ((a64_entry_t)bc->native_code)(t, &cl_vm.stack[cl_vm.sp - nargs],
                                            (uint32_t)nargs, func_obj);

    if (pushed_frame) cl_vm.fp--;
    t->jit_current_nargs = prev_nargs;
    return result;
}

/* %JIT-COMPILE-STUB: `mov w0, #0; ret` -- returns NIL whatever the
 * function's body would.  Proves the assembler -> code heap -> entry path
 * before there is a code generator. */
int cl_jit_emit_stub(CL_Bytecode *bc)
{
    CodeBuf cb;
    A64Asm a;
    uint8_t *code;
    uint32_t len;
    void *entry;

    if (bc == NULL) return 0;
    cb_init(&cb, 8);
    a64_asm_init(&a, &cb);
    a64_emit(&a, a64_movz(0, 0, (uint32_t)CL_NIL, 0));
    a64_emit(&a, a64_ret());
    if (!a64_finish(&a)) { a64_asm_free(&a); cb_free(&cb); return 0; }
    a64_asm_free(&a);
    code = cb_finish(&cb, &len);
    if (code == NULL) return 0;
    entry = cl_codeheap_install(code, len);
    platform_free(code);
    if (entry == NULL) return 0;

    drop_native(bc);
    bc->native_code = (uint8_t *)entry;
    bc->native_len  = len;
    cl_jitc_native_bytes += len;
    return 1;
}

/* Native-to-native direct calls (specs/jit-direct-calls.md) do not exist
 * on AArch64 yet: there is no call site to switch, so the switch reads NIL
 * whatever it is set to, and every counter stays zero. */
void cl_jit_set_direct_calls(int on)
{
    (void)on;
}

int cl_jit_direct_calls_enabled(void) { return 0; }

void cl_jit_direct_call_stats(uint32_t *out)
{
    int i;
    for (i = 0; i < CL_JIT_DS_COUNT; i++) out[i] = 0;
}

/* --- Disassembler -----------------------------------------------------
 * Decodes the forms the backend emits so far; anything else prints as
 * .word.  Same line format as the m68k one: offset, bytes, mnemonic. */

static const char *const a64_cond_names[16] = {
    "eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc",
    "hi", "ls", "ge", "lt", "gt", "le", "al", "nv"
};

static int32_t sext(uint32_t v, int bits)
{
    uint32_t m = (uint32_t)1 << (bits - 1);
    return (int32_t)((v ^ m) - m);
}

static void disasm_word(uint32_t w, uint32_t off, char *out, size_t n)
{
    char r = (w >> 31) ? 'x' : 'w';
    if (w == a64_ret()) snprintf(out, n, "ret");
    else if (w == a64_nop()) snprintf(out, n, "nop");
    else if ((w & 0x7F800000u) == 0x52800000u)
        snprintf(out, n, "movz %c%u, #0x%x, lsl #%u", r, w & 31u,
                 (w >> 5) & 0xFFFFu, ((w >> 21) & 3u) * 16);
    else if ((w & 0x7F800000u) == 0x72800000u)
        snprintf(out, n, "movk %c%u, #0x%x, lsl #%u", r, w & 31u,
                 (w >> 5) & 0xFFFFu, ((w >> 21) & 3u) * 16);
    else if ((w & 0x7F800000u) == 0x12800000u)
        snprintf(out, n, "movn %c%u, #0x%x, lsl #%u", r, w & 31u,
                 (w >> 5) & 0xFFFFu, ((w >> 21) & 3u) * 16);
    else if ((w & 0xFC000000u) == 0x14000000u || (w & 0xFC000000u) == 0x94000000u)
        snprintf(out, n, "%s %ld", (w >> 31) ? "bl" : "b",
                 (long)off + 4L * sext(w & 0x3FFFFFFu, 26));
    else if ((w & 0xFF000010u) == 0x54000000u)
        snprintf(out, n, "b.%s %ld", a64_cond_names[w & 15u],
                 (long)off + 4L * sext((w >> 5) & 0x7FFFFu, 19));
    else if ((w & 0xFFFFFC1Fu) == 0xD63F0000u)
        snprintf(out, n, "blr x%u", (w >> 5) & 31u);
    else if ((w & 0xFFFFFC1Fu) == 0xD61F0000u)
        snprintf(out, n, "br x%u", (w >> 5) & 31u);
    else
        snprintf(out, n, ".word 0x%08x", w);
}

void cl_jit_disassemble(const uint8_t *code, uint32_t len)
{
    uint32_t off;
    char mnem[64];
    char line[128];
    for (off = 0; off + 4 <= len; off += 4) {
        uint32_t w = (uint32_t)code[off] | ((uint32_t)code[off + 1] << 8)
                   | ((uint32_t)code[off + 2] << 16) | ((uint32_t)code[off + 3] << 24);
        disasm_word(w, off, mnem, sizeof mnem);
        snprintf(line, sizeof line, "  %04lu: %02X %02X %02X %02X        %s\n",
                 (unsigned long)off, code[off], code[off + 1], code[off + 2],
                 code[off + 3], mnem);
        cl_write_cstring_to_stdout(line);
    }
}

#endif /* JIT_A64 */
