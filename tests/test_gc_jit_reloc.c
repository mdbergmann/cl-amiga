/* Regression test for forwarding CL_Obj immediates baked into JIT
 * native code across a moving compaction.
 *
 * The m68k template JIT bakes heap CL_Obj references (symbols for
 * OP_FLOAD/OP_GLOAD, block/tagbody tags, closure templates, quoted
 * constants, the boolean CL_T, ...) directly into the generated machine
 * code as 32-bit immediate operands — and those immediates are
 * arena-relative offsets.  The compacting GC relocates the referenced
 * objects but cannot see the offsets buried in the platform_alloc'd code
 * buffer, so without help they go stale after a compaction: the next
 * execution dereferences a moved-out-from-under-it offset.  On real
 * hardware this surfaced as "OP_FLOAD: JIT call site has non-symbol
 * constant 0x........" when a GC fired mid-loop in the ratio/float
 * arithmetic stress test.
 *
 * Fix: cl_jit_compile records the byte offset of every baked heap
 * immediate in bc->native_relocs; the compactor's reference-update pass
 * (mem.c, TYPE_BYTECODE) forwards each one in place.  This test exercises
 * that pass directly — the JIT itself only runs on m68k, so we synthesise
 * the native_code + native_relocs a JIT'd OP_FLOAD would produce and
 * confirm the compactor rewrites the baked offset to the symbol's new
 * location.  (The end-to-end m68k path is covered by tests/amiga/
 * test-jit.lisp and run-tests.lisp's "rational/float compare gc-safe".)
 */
#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/reader.h"
#include "core/printer.h"
#include "core/compiler.h"
#include "core/vm.h"
#include "core/builtins.h"
#include "core/repl.h"
#include "core/thread.h"
#include "platform/platform.h"

#include <string.h>

static void setup(void)
{
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
    cl_reader_init();
    cl_printer_init();
    cl_compiler_init();
    cl_vm_init(0, 0);
    cl_builtins_init();
    cl_repl_init();
}

static void teardown(void)
{
    cl_mem_shutdown();
    platform_shutdown();
}

static void make_garbage(int n)
{
    int i;
    for (i = 0; i < n; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);
}

/* Read/write a big-endian 32-bit field in a native-code buffer — the
 * byte order m68k immediates use and the compactor's forwarding pass
 * reads/writes. */
static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
static void wr_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)(v);
}

/* --- A baked symbol immediate is forwarded when the symbol relocates --- */
TEST(jit_reloc_symbol_immediate_forwarded)
{
    CL_Bytecode *bc;
    CL_Obj bc_obj, sym;
    uint8_t *code;
    uint32_t *relocs;
    uint32_t old_off, new_off, baked;

    /* Garbage below the soon-to-be-interned symbol so reclaiming it
     * slides the symbol to a new offset on compaction. */
    make_garbage(800);

    sym = cl_intern("JIT-RELOC-SYM", 13);
    CL_GC_PROTECT(sym);
    ASSERT(CL_HEAP_P(sym));
    old_off = (uint32_t)sym;

    /* A minimal live bytecode object the compactor will visit.  Empty
     * constant pool / NIL refs keep mark + update trivial; we only care
     * about the native-code reloc path. */
    bc = (CL_Bytecode *)cl_alloc(TYPE_BYTECODE, sizeof(CL_Bytecode));
    bc_obj = CL_PTR_TO_OBJ(bc);
    CL_GC_PROTECT(bc_obj);
    bc->name = CL_NIL;
    bc->source_lambda_list = CL_NIL;
    bc->constants = NULL;
    bc->n_constants = 0;
    bc->key_syms = NULL;

    /* Synthesise what the walker emits for `OP_FLOAD <sym>`:
     * `move.l #sym,-(a7)` = opcode bytes then a 4-byte big-endian
     * immediate.  The reloc points at the immediate (opcode + 2). */
    code = (uint8_t *)platform_alloc(8);
    ASSERT(code != NULL);
    code[0] = 0x2F; code[1] = 0x3C;          /* move.l #imm,-(a7) opcode */
    wr_be32(code + 2, (uint32_t)sym);        /* the baked symbol offset  */
    code[6] = 0x4E; code[7] = 0x75;          /* rts (filler)             */

    relocs = (uint32_t *)platform_alloc(sizeof(uint32_t));
    ASSERT(relocs != NULL);
    relocs[0] = 2;                            /* offset of the immediate */

    bc->native_code        = code;
    bc->native_len         = 8;
    bc->native_relocs      = relocs;
    bc->native_reloc_count = 1;

    /* Baked immediate matches the symbol before any move. */
    ASSERT_EQ_INT((int)rd_be32(code + 2), (int)old_off);

    /* Force a moving compaction: the symbol relocates. */
    make_garbage(800);
    cl_gc();
    cl_gc_compact();
    cl_gc_compact();

    new_off = (uint32_t)sym;   /* GC-protected: holds the post-move offset */

    /* The bytecode object itself relocated too — re-derive it from the
     * protected CL_Obj before touching native_code. */
    bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);

    /* The whole point: the symbol actually moved (otherwise the test is
     * vacuous), and the compactor rewrote the baked immediate to track
     * it — not left the stale pre-move offset behind. */
    ASSERT(new_off != old_off);
    baked = rd_be32(bc->native_code + 2);
    ASSERT_EQ_INT((int)baked, (int)new_off);

    platform_free(bc->native_code);
    platform_free(bc->native_relocs);
    bc->native_code = NULL;
    bc->native_relocs = NULL;
    bc->native_reloc_count = 0;
    CL_GC_UNPROTECT(2);
}

/* --- Multiple relocs, mixed heap/non-heap-looking bytes, all tracked --- */
TEST(jit_reloc_multiple_immediates_forwarded)
{
    CL_Bytecode *bc;
    CL_Obj bc_obj, sym_a, sym_b;
    uint8_t *code;
    uint32_t *relocs;
    uint32_t old_a, old_b;

    make_garbage(600);
    sym_a = cl_intern("JIT-RELOC-A", 11);
    CL_GC_PROTECT(sym_a);
    make_garbage(600);
    sym_b = cl_intern("JIT-RELOC-B", 11);
    CL_GC_PROTECT(sym_b);
    old_a = (uint32_t)sym_a;
    old_b = (uint32_t)sym_b;

    bc = (CL_Bytecode *)cl_alloc(TYPE_BYTECODE, sizeof(CL_Bytecode));
    bc_obj = CL_PTR_TO_OBJ(bc);
    CL_GC_PROTECT(bc_obj);
    bc->name = CL_NIL;
    bc->source_lambda_list = CL_NIL;
    bc->constants = NULL;
    bc->n_constants = 0;
    bc->key_syms = NULL;

    /* Two `move.l #imm,-(a7)` instructions back to back (12 bytes). */
    code = (uint8_t *)platform_alloc(12);
    ASSERT(code != NULL);
    code[0] = 0x2F; code[1] = 0x3C;
    wr_be32(code + 2, (uint32_t)sym_a);
    code[6] = 0x2F; code[7] = 0x3C;
    wr_be32(code + 8, (uint32_t)sym_b);

    relocs = (uint32_t *)platform_alloc(2 * sizeof(uint32_t));
    ASSERT(relocs != NULL);
    relocs[0] = 2;
    relocs[1] = 8;

    bc->native_code        = code;
    bc->native_len         = 12;
    bc->native_relocs      = relocs;
    bc->native_reloc_count = 2;

    make_garbage(800);
    cl_gc();
    cl_gc_compact();
    cl_gc_compact();

    bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);   /* bytecode relocated too */
    ASSERT((uint32_t)sym_a != old_a);
    ASSERT((uint32_t)sym_b != old_b);
    ASSERT_EQ_INT((int)rd_be32(bc->native_code + 2), (int)(uint32_t)sym_a);
    ASSERT_EQ_INT((int)rd_be32(bc->native_code + 8), (int)(uint32_t)sym_b);

    platform_free(bc->native_code);
    platform_free(bc->native_relocs);
    bc->native_code = NULL;
    bc->native_relocs = NULL;
    bc->native_reloc_count = 0;
    CL_GC_UNPROTECT(3);
}

int main(void)
{
    setup();

    RUN(jit_reloc_symbol_immediate_forwarded);
    RUN(jit_reloc_multiple_immediates_forwarded);

    teardown();
    REPORT();
}
