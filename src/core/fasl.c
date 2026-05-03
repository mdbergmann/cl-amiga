#include "fasl.h"
#include "mem.h"
#include "symbol.h"
#include "package.h"
#include "error.h"
#include "float.h"
#include "vm.h"
#include "compiler.h"
#include "thread.h"
#include "../platform/platform.h"
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

/* ================================================================
 * Writer
 * ================================================================ */

void cl_fasl_writer_init(CL_FaslWriter *w, uint8_t *buf, uint32_t capacity)
{
    /* Sanity check: w must be a valid heap or stack pointer.
     * Detect corruption where w points into the binary's data segment
     * (happens when a non-volatile local survives longjmp with garbage). */
    if (!w) {
        fprintf(stderr, "[FASL] BUG: cl_fasl_writer_init called with NULL writer\n");
        abort();
    }
    w->data = buf;
    w->capacity = capacity;
    w->pos = 0;
    w->error = FASL_OK;
    w->gensym_count = 0;
    w->shared_count = 0;
    w->shared_objs = NULL;  /* lazily allocated on first use */
}

void cl_fasl_write_u8(CL_FaslWriter *w, uint8_t val)
{
    if (w->error) return;
    if (w->pos + 1 > w->capacity) { w->error = FASL_ERR_OVERFLOW; return; }
    w->data[w->pos++] = val;
}

void cl_fasl_write_u16(CL_FaslWriter *w, uint16_t val)
{
    if (w->error) return;
    if (w->pos + 2 > w->capacity) { w->error = FASL_ERR_OVERFLOW; return; }
    w->data[w->pos++] = (uint8_t)(val >> 8);
    w->data[w->pos++] = (uint8_t)(val);
}

void cl_fasl_write_u32(CL_FaslWriter *w, uint32_t val)
{
    if (w->error) return;
    if (w->pos + 4 > w->capacity) { w->error = FASL_ERR_OVERFLOW; return; }
    w->data[w->pos++] = (uint8_t)(val >> 24);
    w->data[w->pos++] = (uint8_t)(val >> 16);
    w->data[w->pos++] = (uint8_t)(val >> 8);
    w->data[w->pos++] = (uint8_t)(val);
}

void cl_fasl_write_bytes(CL_FaslWriter *w, const void *data, uint32_t len)
{
    if (w->error) return;
    if (w->pos + len > w->capacity) { w->error = FASL_ERR_OVERFLOW; return; }
    memcpy(w->data + w->pos, data, len);
    w->pos += len;
}

void cl_fasl_write_header(CL_FaslWriter *w, uint32_t n_units)
{
    cl_fasl_write_u32(w, CL_FASL_MAGIC);
    cl_fasl_write_u16(w, CL_FASL_VERSION);
    cl_fasl_write_u16(w, 0);  /* flags */
    cl_fasl_write_u32(w, n_units);
}

/* --- Serialize a CL_Obj constant (iterative) ---
 *
 * Serialization is a pre-order tree walk over the constant graph.  An
 * earlier recursive implementation overflowed the C stack on deeply-nested
 * graphs (lparallel/cognate/psort.lisp, sento dispatch chains): each
 * recursion level cost ~150-300B, and inputs >4096 deep tripped a stack-
 * protector canary in bi_compile_file or crashed outright.
 *
 * The iterative driver below uses an explicit, heap-allocated worklist of
 * frames.  Each frame represents one in-progress object; it carries a
 * phase counter and a child index so a frame can yield to a child mid-way
 * and resume after the child completes.  Worklist capacity grows by
 * doubling, capped at FASL_SER_STACK_MAX_CAP frames.  The C stack stays
 * shallow and bounded regardless of input depth.
 *
 * GC safety: the serializer makes no allocations (only memcpys into the
 * pre-allocated writer buffer + read-only package lookups).  Therefore GC
 * never runs during a serialize call, and CL_Obj values held on the
 * worklist do not need explicit GC protection — they remain reachable
 * from their parents which are themselves on the worklist or the C stack.
 */

/* Worklist sizing: 256 initial capacity (doubles on demand), hard-capped
 * at 2M frames (~24 MB at 12B/frame including padding).  No realistic
 * input should ever come close — this only guards against runaway
 * graphs (e.g. accidental cycles in non-shared types). */
#define FASL_SER_STACK_INIT_CAP 256
#define FASL_SER_STACK_MAX_CAP  (1u << 21)

/* Frame phase tags.  Leaf types finish in PHASE_START with no children.
 * Composite types write their header in PHASE_START, push children in
 * reverse, set phase to PHASE_DONE, and yield (return 0).  When children
 * drain and the parent frame becomes top again, PHASE_DONE returns 1
 * so the driver pops it.  This dance is needed because the driver pops
 * the top frame on "done", and after pushing children the parent is no
 * longer on top — only the eventual re-entry at PHASE_DONE pops correctly.
 * BYTECODE/CLOSURE/STRUCT use additional phases to interleave header
 * bytes between children. */
#define PHASE_START          0  /* dispatch on type, write header, push children */
#define PHASE_BC_AFTER_TAG   1  /* code prefix written next */
#define PHASE_BC_CONSTANTS   2  /* constants iteration */
#define PHASE_BC_METADATA    3  /* metadata bytes */
#define PHASE_BC_KEY_SYMS    4  /* key_syms iteration */
#define PHASE_BC_POSTLUDE    5  /* key_slots, source info, line map; push name */
#define PHASE_CLOSURE_AFTER_BC    0xF0  /* write n_upvalues, transition to NEXT_UPVAL */
#define PHASE_STRUCT_AFTER_TYPEDESC 0xF1 /* write n_slots, transition to NEXT_SLOT */
#define PHASE_CONS_NEXT_CAR       0xF2  /* iterating CDR chain inline */
#define PHASE_VECTOR_NEXT         0xF3  /* push one element per step (index = cursor) */
#define PHASE_STRUCT_NEXT_SLOT    0xF4  /* push one slot per step */
#define PHASE_CLOSURE_NEXT_UPVAL  0xF5  /* push one upvalue per step */
#define PHASE_DONE          0xFF  /* sentinel: return 1, frame is popped */

#ifdef DEBUG_FASL
/* Forward decls for histogram state — definitions live next to the drive
 * loop further down.  Declared up here so the OBJ_REF / CONST_REF emission
 * sites in fasl_ser_step can bump the dedup-hit counters. */
static int      g_fasl_hist_enabled;
static uint32_t g_fasl_dedup_obj_ref;
static uint32_t g_fasl_dedup_const_ref;
#endif

typedef struct {
    CL_Obj   obj;
    uint32_t index;  /* child cursor for arrays (constants, slots, vector elts) */
    uint8_t  phase;
    uint8_t  pad[3];
} FaslSerFrame;

typedef struct {
    FaslSerFrame *frames;
    uint32_t      depth;
    uint32_t      capacity;
} FaslSerStack;

static int fasl_ser_stack_push(CL_FaslWriter *w, FaslSerStack *s,
                               CL_Obj obj, uint8_t phase)
{
    if (s->depth == s->capacity) {
        uint32_t new_cap = s->capacity ? s->capacity * 2 : FASL_SER_STACK_INIT_CAP;
        FaslSerFrame *nf;
        if (new_cap > FASL_SER_STACK_MAX_CAP) {
#ifdef DEBUG_FASL_DEEP
            {
                /* Diagnostic dump on hitting the worklist cap.  The cap is
                 * deliberately huge (2M frames ~= 24MB) — anything that
                 * trips it is almost certainly an undetected cycle in the
                 * constant graph, not legitimate depth.  Histogram + top
                 * frames usually identify the cyclic type.
                 *
                 * fflush after every fprintf — when stderr is redirected
                 * to a file (block-buffered) and the process crashes
                 * shortly after, unflushed buffer is lost.  This
                 * diagnostic exists *because* something is wrong, so
                 * paying for the flushes is fine. */
                uint32_t i;
                uint32_t type_counts[64] = {0};
                int top_t = CL_HEAP_P(obj) ? CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) : -1;
                int parent_t = -1;
                CL_Obj parent_obj = CL_NIL;
                if (s->depth > 0) {
                    parent_obj = s->frames[s->depth - 1].obj;
                    if (CL_HEAP_P(parent_obj))
                        parent_t = CL_HDR_TYPE(CL_OBJ_TO_PTR(parent_obj));
                }
                fprintf(stderr,
                  "[FASL] hit FASL_SER_STACK_MAX_CAP=%u depth=%u\n"
                  "[FASL]   pushing  obj=0x%08x phase=%u type=%d\n"
                  "[FASL]   top-of-stack obj=0x%08x type=%d\n",
                  FASL_SER_STACK_MAX_CAP, s->depth,
                  (unsigned)obj, phase, top_t,
                  (unsigned)parent_obj, parent_t);
                fflush(stderr);
                if (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_STRUCT) {
                    CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
                    fprintf(stderr, "[FASL] pushed obj is STRUCT type_desc=0x%08x n_slots=%u\n",
                            (unsigned)st->type_desc, (unsigned)st->n_slots);
                    fflush(stderr);
                    if (CL_HEAP_P(st->type_desc) &&
                        CL_HDR_TYPE(CL_OBJ_TO_PTR(st->type_desc)) == TYPE_SYMBOL) {
                        CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(st->type_desc);
                        CL_String *nm = (CL_String *)CL_OBJ_TO_PTR(sym->name);
                        fprintf(stderr, "[FASL] type_desc symbol name=%.*s\n",
                                (int)nm->length, nm->data);
                        fflush(stderr);
                    }
                }
                for (i = 0; i < s->depth; i++) {
                    CL_Obj fobj = s->frames[i].obj;
                    if (CL_HEAP_P(fobj)) {
                        uint8_t t = CL_HDR_TYPE(CL_OBJ_TO_PTR(fobj));
                        if (t < 64) type_counts[t]++;
                    } else {
                        type_counts[63]++;
                    }
                }
                fprintf(stderr, "[FASL] frame type histogram (depth=%u):\n", s->depth);
                for (i = 0; i < 64; i++) {
                    if (type_counts[i] > 0)
                        fprintf(stderr, "  type %u: %u frames\n", i, type_counts[i]);
                }
                fflush(stderr);
                fprintf(stderr, "[FASL] top 20 frames:\n");
                for (i = (s->depth >= 20 ? s->depth - 20 : 0); i < s->depth; i++) {
                    CL_Obj fobj = s->frames[i].obj;
                    int t = CL_HEAP_P(fobj) ? CL_HDR_TYPE(CL_OBJ_TO_PTR(fobj)) : -1;
                    fprintf(stderr, "  [%u] obj=0x%08x phase=%u type=%d",
                            i, (unsigned)fobj, s->frames[i].phase, t);
                    if (t == TYPE_STRUCT) {
                        CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(fobj);
                        fprintf(stderr, " td=0x%08x n_slots=%u idx=%u",
                                (unsigned)st->type_desc, st->n_slots, s->frames[i].index);
                    }
                    fprintf(stderr, "\n");
                }
                fflush(stderr);
            }
#endif
            w->error = FASL_ERR_TOO_DEEP;
            return 0;
        }
        nf = (FaslSerFrame *)platform_alloc(new_cap * sizeof(FaslSerFrame));
        if (!nf) { w->error = FASL_ERR_OVERFLOW; return 0; }
        if (s->frames) {
            memcpy(nf, s->frames, s->depth * sizeof(FaslSerFrame));
            platform_free(s->frames);
        }
        s->frames = nf;
        s->capacity = new_cap;
    }
    s->frames[s->depth].obj    = obj;
    s->frames[s->depth].phase  = phase;
    s->frames[s->depth].index  = 0;
    s->frames[s->depth].pad[0] = 0;
    s->frames[s->depth].pad[1] = 0;
    s->frames[s->depth].pad[2] = 0;
    s->depth++;
    return 1;
}

/* Step the topmost frame.  Returns 1 if the frame is done (caller pops),
 * 0 if it pushed children (frame stays on stack, children processed first). */
static int fasl_ser_step(CL_FaslWriter *w, FaslSerStack *s)
{
    /* Cache frame fields locally — push() may reallocate s->frames. */
    uint32_t      idx   = s->depth - 1;
    FaslSerFrame *f     = &s->frames[idx];
    CL_Obj        obj   = f->obj;
    uint8_t       phase = f->phase;

    /* Sentinel: composite frame's children have drained, pop it. */
    if (phase == PHASE_DONE) return 1;

    /* PHASE_START handles all immediate / leaf cases inline and dispatches
     * heap-object types.  BYTECODE may transition to its phase chain. */
    if (phase == PHASE_START) {
        /* Immediates first */
        if (CL_NULL_P(obj))   { cl_fasl_write_u8(w, FASL_TAG_NIL);     return 1; }
        if (obj == CL_T)      { cl_fasl_write_u8(w, FASL_TAG_T);       return 1; }
        if (obj == CL_UNBOUND){ cl_fasl_write_u8(w, FASL_TAG_UNBOUND); return 1; }
        if (CL_FIXNUM_P(obj)) {
            cl_fasl_write_u8(w, FASL_TAG_FIXNUM);
            cl_fasl_write_u32(w, obj);
            return 1;
        }
        if (CL_CHAR_P(obj)) {
            cl_fasl_write_u8(w, FASL_TAG_CHARACTER);
            cl_fasl_write_u32(w, obj);
            return 1;
        }
        if (!CL_HEAP_P(obj)) {
            cl_fasl_write_u8(w, FASL_TAG_FIXNUM);
            cl_fasl_write_u32(w, obj);
            return 1;
        }

        /* Shared-object dedup for closures, bytecodes, structs, symbols,
         * and locks.  If found, emit OBJ_REF and we're done.  If new,
         * register and emit OBJ_DEF, then fall through to the type-specific
         * case.
         *
         * STRUCT is included because cyclic CLOS metaobject graphs (generic
         * function ↔ method ↔ method-function-closure ↔ gf, slot definitions
         * cross-referencing their class, etc.) hit the writer through
         * compile-time constants and would otherwise walk indefinitely until
         * the buffer fills.
         *
         * SYMBOL is included because the same package-qualified symbol name
         * tends to recur dozens-to-thousands of times across a unit's
         * constant graph (slot names, class names, GF names, lambda-list
         * keywords).  Without dedup each reference re-emits the package
         * name + symbol name (~29 B avg); with dedup the second reference
         * costs 3 B.  Gensyms still hit FASL_TAG_GENSYM_DEF/REF in the
         * SYMBOL body case below — wrapping them in OBJ_DEF adds 3 B on
         * first emission but they are negligible by count.
         *
         * LOCK is included so that multiple references to the same
         * compile-time lock (e.g. a struct slot reachable from several
         * places in the constant graph) all resolve to the same
         * fresh-at-load-time lock object.  Without dedup, two slots that
         * shared a lock at compile time would each get an independent
         * fresh lock at load time and synchronization would be split
         * across them.
         *
         * Reader's OBJ_DEF/REF handler is type-agnostic — no reader-side
         * change needed. */
        {
            uint8_t htype = CL_HDR_TYPE(CL_OBJ_TO_PTR(obj));
            if (htype == TYPE_CLOSURE || htype == TYPE_BYTECODE ||
                htype == TYPE_STRUCT  || htype == TYPE_SYMBOL  ||
                htype == TYPE_LOCK) {
                if (!w->shared_objs) {
                    w->shared_objs = (CL_Obj *)platform_alloc(
                        FASL_MAX_SHARED * sizeof(CL_Obj));
                }
                if (w->shared_objs) {
                    uint16_t si;
                    for (si = 0; si < w->shared_count; si++) {
                        if (w->shared_objs[si] == obj) {
                            cl_fasl_write_u8(w, FASL_TAG_OBJ_REF);
                            cl_fasl_write_u16(w, si);
#ifdef DEBUG_FASL
                            if (g_fasl_hist_enabled) g_fasl_dedup_obj_ref++;
#endif
                            return 1;
                        }
                    }
                    if (w->shared_count < FASL_MAX_SHARED) {
                        w->shared_objs[w->shared_count] = obj;
                        cl_fasl_write_u8(w, FASL_TAG_OBJ_DEF);
                        cl_fasl_write_u16(w, w->shared_count);
                        w->shared_count++;
                    }
                }
            }
        }

        switch (CL_HDR_TYPE(CL_OBJ_TO_PTR(obj))) {
        case TYPE_SYMBOL: {
            CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(obj);
            CL_String *name = (CL_String *)CL_OBJ_TO_PTR(sym->name);
            if (CL_NULL_P(sym->package)) {
                uint16_t gi;
                for (gi = 0; gi < w->gensym_count; gi++) {
                    if (w->gensym_objs[gi] == obj) {
                        cl_fasl_write_u8(w, FASL_TAG_GENSYM_REF);
                        cl_fasl_write_u16(w, gi);
                        return 1;
                    }
                }
                if (w->gensym_count < FASL_MAX_GENSYMS) {
                    w->gensym_objs[w->gensym_count] = obj;
                    cl_fasl_write_u8(w, FASL_TAG_GENSYM_DEF);
                    cl_fasl_write_u16(w, w->gensym_count);
                    w->gensym_count++;
                } else {
                    cl_fasl_write_u8(w, FASL_TAG_SYMBOL);
                    cl_fasl_write_u16(w, 0);
                }
                cl_fasl_write_u16(w, (uint16_t)name->length);
                cl_fasl_write_bytes(w, name->data, name->length);
                return 1;
            }
            cl_fasl_write_u8(w, FASL_TAG_SYMBOL);
            {
                CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(sym->package);
                CL_String  *pname = (CL_String *)CL_OBJ_TO_PTR(pkg->name);
                if (pkg == (CL_Package *)CL_OBJ_TO_PTR(
                        cl_find_package("KEYWORD", 7))) {
                    cl_fasl_write_u16(w, 0xFFFF);
                } else {
                    cl_fasl_write_u16(w, (uint16_t)pname->length);
                    cl_fasl_write_bytes(w, pname->data, pname->length);
                }
            }
            cl_fasl_write_u16(w, (uint16_t)name->length);
            cl_fasl_write_bytes(w, name->data, name->length);
            return 1;
        }

        case TYPE_STRING: {
            CL_String *str = (CL_String *)CL_OBJ_TO_PTR(obj);
            cl_fasl_write_u8(w, FASL_TAG_STRING);
            cl_fasl_write_u32(w, str->length);
            cl_fasl_write_bytes(w, str->data, str->length);
            return 1;
        }

#ifdef CL_WIDE_STRINGS
        case TYPE_WIDE_STRING: {
            CL_WideString *ws = (CL_WideString *)CL_OBJ_TO_PTR(obj);
            uint32_t i;
            cl_fasl_write_u8(w, FASL_TAG_WIDE_STRING);
            cl_fasl_write_u32(w, ws->length);
            for (i = 0; i < ws->length; i++)
                cl_fasl_write_u32(w, ws->data[i]);
            return 1;
        }
#endif

        case TYPE_CONS: {
            /* Walk the CDR spine inline using the frame's obj as a cursor:
             * write a CONS tag, push the CAR, advance to PHASE_CONS_NEXT_CAR.
             * After the CAR completes, that phase consumes the next CDR cell
             * (writing another tag) until the spine ends, then pushes the
             * final CDR (NIL or atom).  Peak worklist depth stays O(1) for
             * proper lists of any length — the prior naive push-cdr-as-frame
             * approach grew O(N), exhausting the 2M cap on long lists. */
            CL_Obj car_v = cl_car(obj);
            CL_Obj cdr_v = cl_cdr(obj);
            cl_fasl_write_u8(w, FASL_TAG_CONS);
            s->frames[idx].obj   = cdr_v;
            s->frames[idx].phase = PHASE_CONS_NEXT_CAR;
            if (!fasl_ser_stack_push(w, s, car_v, PHASE_START)) return 1;
            return 0;
        }

        case TYPE_BYTECODE:
            /* Tag here, body via phase chain.  Stay on stack. */
            cl_fasl_write_u8(w, FASL_TAG_BYTECODE);
            s->frames[idx].phase = PHASE_BC_AFTER_TAG;
            return 0;

        case TYPE_CLOSURE: {
            /* Wire format: tag | bytecode-bytes | n_upvalues | upval-bytes...
             * The n_upvalues u16 appears after the bytecode subtree, so we
             * can't push everything at once.  Push bytecode now, defer
             * n_upvalues + upvalues to PHASE_CLOSURE_AFTER_BC. */
            CL_Closure *cl_obj = (CL_Closure *)CL_OBJ_TO_PTR(obj);
            cl_fasl_write_u8(w, FASL_TAG_CLOSURE);
            s->frames[idx].phase = PHASE_CLOSURE_AFTER_BC;
            if (!fasl_ser_stack_push(w, s, cl_obj->bytecode, PHASE_START))
                return 1;
            return 0;
        }

        case TYPE_FUNCTION: {
            CL_Function *fn = (CL_Function *)CL_OBJ_TO_PTR(obj);
            if (!CL_NULL_P(fn->name) && CL_HEAP_P(fn->name) &&
                CL_HDR_TYPE(CL_OBJ_TO_PTR(fn->name)) == TYPE_SYMBOL) {
                cl_fasl_write_u8(w, FASL_TAG_FUNCTION);
                s->frames[idx].phase = PHASE_DONE;
                if (!fasl_ser_stack_push(w, s, fn->name, PHASE_START)) return 1;
                return 0;
            }
            cl_fasl_write_u8(w, FASL_TAG_NIL);
            return 1;
        }

        case TYPE_SINGLE_FLOAT: {
            CL_SingleFloat *sf = (CL_SingleFloat *)CL_OBJ_TO_PTR(obj);
            uint32_t bits;
            cl_fasl_write_u8(w, FASL_TAG_SINGLE_FLOAT);
            memcpy(&bits, &sf->value, 4);
            cl_fasl_write_u32(w, bits);
            return 1;
        }

        case TYPE_DOUBLE_FLOAT: {
            CL_DoubleFloat *df = (CL_DoubleFloat *)CL_OBJ_TO_PTR(obj);
            uint8_t bytes[8];
            cl_fasl_write_u8(w, FASL_TAG_DOUBLE_FLOAT);
            memcpy(bytes, &df->value, 8);
            {
                union { double d; uint8_t b[8]; } u;
                u.d = 1.0;
                if (u.b[0] == 0x3F) {
                    cl_fasl_write_bytes(w, bytes, 8);
                } else {
                    uint8_t rev[8];
                    int i;
                    for (i = 0; i < 8; i++) rev[i] = bytes[7 - i];
                    cl_fasl_write_bytes(w, rev, 8);
                }
            }
            return 1;
        }

        case TYPE_RATIO: {
            CL_Ratio *rat = (CL_Ratio *)CL_OBJ_TO_PTR(obj);
            CL_Obj num = rat->numerator;
            CL_Obj den = rat->denominator;
            cl_fasl_write_u8(w, FASL_TAG_RATIO);
            s->frames[idx].phase = PHASE_DONE;
            if (!fasl_ser_stack_push(w, s, den, PHASE_START)) return 1;
            if (!fasl_ser_stack_push(w, s, num, PHASE_START)) return 1;
            return 0;
        }

        case TYPE_COMPLEX: {
            CL_Complex *cx = (CL_Complex *)CL_OBJ_TO_PTR(obj);
            CL_Obj re = cx->realpart;
            CL_Obj im = cx->imagpart;
            cl_fasl_write_u8(w, FASL_TAG_COMPLEX);
            s->frames[idx].phase = PHASE_DONE;
            if (!fasl_ser_stack_push(w, s, im, PHASE_START)) return 1;
            if (!fasl_ser_stack_push(w, s, re, PHASE_START)) return 1;
            return 0;
        }

        case TYPE_BIGNUM: {
            CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(obj);
            uint32_t i;
            cl_fasl_write_u8(w, FASL_TAG_BIGNUM);
            cl_fasl_write_u8(w, (uint8_t)bn->sign);
            cl_fasl_write_u32(w, bn->length);
            for (i = 0; i < bn->length; i++)
                cl_fasl_write_u16(w, bn->limbs[i]);
            return 1;
        }

        case TYPE_VECTOR: {
            CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
            uint32_t len = v->length;
            cl_fasl_write_u8(w, FASL_TAG_VECTOR);
            cl_fasl_write_u32(w, len);
            if (len == 0) return 1;
            /* Push elements one-at-a-time via PHASE_VECTOR_NEXT — keeps
             * worklist depth O(1) per vector regardless of length. */
            s->frames[idx].phase = PHASE_VECTOR_NEXT;
            s->frames[idx].index = 0;
            return 0;
        }

        case TYPE_BIT_VECTOR: {
            CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(obj);
            uint32_t n_words = CL_BV_WORDS(bv->length);
            uint32_t i;
            cl_fasl_write_u8(w, FASL_TAG_BIT_VECTOR);
            cl_fasl_write_u32(w, bv->length);
            for (i = 0; i < n_words; i++)
                cl_fasl_write_u32(w, bv->data[i]);
            return 1;
        }

        case TYPE_PACKAGE: {
            CL_Package *pkg = (CL_Package *)CL_OBJ_TO_PTR(obj);
            CL_String  *pname = (CL_String *)CL_OBJ_TO_PTR(pkg->name);
            cl_fasl_write_u8(w, FASL_TAG_PACKAGE);
            cl_fasl_write_u16(w, (uint16_t)pname->length);
            cl_fasl_write_bytes(w, pname->data, pname->length);
            return 1;
        }

        case TYPE_PATHNAME: {
            CL_Pathname *pn = (CL_Pathname *)CL_OBJ_TO_PTR(obj);
            CL_Obj host = pn->host, dev = pn->device, dir = pn->directory;
            CL_Obj name = pn->name, type = pn->type, ver = pn->version;
            cl_fasl_write_u8(w, FASL_TAG_PATHNAME);
            s->frames[idx].phase = PHASE_DONE;
            if (!fasl_ser_stack_push(w, s, ver,  PHASE_START)) return 1;
            if (!fasl_ser_stack_push(w, s, type, PHASE_START)) return 1;
            if (!fasl_ser_stack_push(w, s, name, PHASE_START)) return 1;
            if (!fasl_ser_stack_push(w, s, dir,  PHASE_START)) return 1;
            if (!fasl_ser_stack_push(w, s, dev,  PHASE_START)) return 1;
            if (!fasl_ser_stack_push(w, s, host, PHASE_START)) return 1;
            return 0;
        }

        case TYPE_STRUCT: {
            /* Wire format: tag | type_desc-bytes | n_slots | slot-bytes...
             * The n_slots u32 appears after the type_desc subtree, so we
             * defer slots to PHASE_STRUCT_AFTER_TYPEDESC. */
            CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
            /* CLOS class metaobjects (STANDARD-CLASS / BUILT-IN-CLASS
             * structs) are cyclic by design: their effective-slots,
             * precedence-list, and direct-subclasses slots back-reference
             * the class.  Naive struct serialization walks the cycle
             * forever (FASL_ERR_TOO_DEEP).  Even if we broke the cycle,
             * deserializing into a *new* struct would lose identity —
             * defmethod specializers would no longer EQ the live class.
             *
             * Instead, emit a class-by-name reference that resolves via
             * (find-class 'name) at load time.  Identity is preserved
             * because find-class returns the singleton metaobject. */
            if (CL_HEAP_P(st->type_desc) &&
                CL_HDR_TYPE(CL_OBJ_TO_PTR(st->type_desc)) == TYPE_SYMBOL) {
                CL_Symbol *td_sym = (CL_Symbol *)CL_OBJ_TO_PTR(st->type_desc);
                CL_String *td_nm = (CL_String *)CL_OBJ_TO_PTR(td_sym->name);
                if ((td_nm->length == 14 &&
                     (memcmp(td_nm->data, "STANDARD-CLASS", 14) == 0 ||
                      memcmp(td_nm->data, "BUILT-IN-CLASS", 14) == 0)) ||
                    (td_nm->length == 26 &&
                     memcmp(td_nm->data, "FUNCALLABLE-STANDARD-CLASS", 26) == 0)) {
                    CL_Obj cname = st->slots[0];
                    cl_fasl_write_u8(w, FASL_TAG_CLASS_REF);
                    s->frames[idx].phase = PHASE_DONE;
                    if (!fasl_ser_stack_push(w, s, cname, PHASE_START)) return 1;
                    return 0;
                }
            }
            cl_fasl_write_u8(w, FASL_TAG_STRUCT);
            s->frames[idx].phase = PHASE_STRUCT_AFTER_TYPEDESC;
            if (!fasl_ser_stack_push(w, s, st->type_desc, PHASE_START)) return 1;
            return 0;
        }

        case TYPE_LOCK: {
            /* Wire format: tag | flags(u8) | name(obj).
             * The platform mutex itself can't be serialized — it's
             * per-process state.  Instead we record just enough metadata
             * to reconstruct an equivalent fresh lock at load time.
             * Sharing within a single FASL is preserved by OBJ_DEF/OBJ_REF
             * (TYPE_LOCK is in the shared_objs dedup set), so multiple
             * references to the *same* compile-time lock all resolve to
             * the *same* fresh-at-load lock object. */
            CL_Lock *lk = (CL_Lock *)CL_OBJ_TO_PTR(obj);
            uint8_t flags = (lk->flags & CL_LOCK_FLAG_RECURSIVE) ? 0x01 : 0x00;
            CL_Obj name = lk->name;
            cl_fasl_write_u8(w, FASL_TAG_LOCK);
            cl_fasl_write_u8(w, flags);
            s->frames[idx].phase = PHASE_DONE;
            if (!fasl_ser_stack_push(w, s, name, PHASE_START)) return 1;
            return 0;
        }

        default:
            cl_fasl_write_u8(w, FASL_TAG_NIL);
            return 1;
        }
    }

    /* CONS resumption: the most recent CAR has been serialized; obj is the
     * remaining CDR.  If still a CONS, write another tag, push next CAR,
     * stay in this phase.  Else push the final CDR and exit. */
    if (phase == PHASE_CONS_NEXT_CAR) {
        if (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_CONS) {
            CL_Obj car_v = cl_car(obj);
            CL_Obj cdr_v = cl_cdr(obj);
            cl_fasl_write_u8(w, FASL_TAG_CONS);
            s->frames[idx].obj = cdr_v;
            /* phase stays PHASE_CONS_NEXT_CAR */
            if (!fasl_ser_stack_push(w, s, car_v, PHASE_START)) return 1;
            return 0;
        }
        /* End of spine — push final cdr (NIL or atom). */
        s->frames[idx].phase = PHASE_DONE;
        if (!fasl_ser_stack_push(w, s, obj, PHASE_START)) return 1;
        return 0;
    }

    /* CLOSURE resumption: bytecode child has been serialized; now write
     * n_upvalues and transition to one-at-a-time upvalue iteration.  Use
     * the allocation-based upvalue count — safe against bc->n_upvalues
     * mismatch (can occur when bytecodes are recompiled via ASDF reload
     * while old closures are still live). */
    if (phase == PHASE_CLOSURE_AFTER_BC) {
        CL_Closure *cl_obj = (CL_Closure *)CL_OBJ_TO_PTR(obj);
        uint32_t alloc_size = CL_HDR_SIZE(cl_obj);
        uint16_t n_upvalues = (uint16_t)(
            (alloc_size - sizeof(CL_Closure)) / sizeof(CL_Obj));
        cl_fasl_write_u16(w, n_upvalues);
        if (n_upvalues == 0) return 1;
        s->frames[idx].phase = PHASE_CLOSURE_NEXT_UPVAL;
        s->frames[idx].index = 0;
        return 0;
    }

    if (phase == PHASE_CLOSURE_NEXT_UPVAL) {
        CL_Closure *cl_obj = (CL_Closure *)CL_OBJ_TO_PTR(obj);
        uint32_t alloc_size = CL_HDR_SIZE(cl_obj);
        uint16_t n_upvalues = (uint16_t)(
            (alloc_size - sizeof(CL_Closure)) / sizeof(CL_Obj));
        uint32_t k = s->frames[idx].index;
        if (k >= n_upvalues) return 1;
        s->frames[idx].index = k + 1;
        if (!fasl_ser_stack_push(w, s, cl_obj->upvalues[k], PHASE_START))
            return 1;
        return 0;
    }

    /* STRUCT resumption: type_desc child done; write n_slots and transition
     * to one-at-a-time slot iteration. */
    if (phase == PHASE_STRUCT_AFTER_TYPEDESC) {
        CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
        cl_fasl_write_u32(w, st->n_slots);
        if (st->n_slots == 0) return 1;
        s->frames[idx].phase = PHASE_STRUCT_NEXT_SLOT;
        s->frames[idx].index = 0;
        return 0;
    }

    if (phase == PHASE_STRUCT_NEXT_SLOT) {
        CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
        uint32_t k = s->frames[idx].index;
        if (k >= st->n_slots) return 1;
        s->frames[idx].index = k + 1;
        if (!fasl_ser_stack_push(w, s, st->slots[k], PHASE_START))
            return 1;
        return 0;
    }

    /* VECTOR resumption: write tag+len happens in PHASE_START; this just
     * iterates elements one at a time. */
    if (phase == PHASE_VECTOR_NEXT) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(obj);
        CL_Obj *data = cl_vector_data(v);
        uint32_t k = s->frames[idx].index;
        if (k >= v->length) return 1;
        s->frames[idx].index = k + 1;
        if (!fasl_ser_stack_push(w, s, data[k], PHASE_START))
            return 1;
        return 0;
    }

    /* BYTECODE phase chain — see PHASE_BC_* definitions above. */
    {
        CL_Bytecode *bc = (CL_Bytecode *)CL_OBJ_TO_PTR(obj);
        uint16_t i;

        switch (phase) {
        case PHASE_BC_AFTER_TAG:
            cl_fasl_write_u32(w, bc->code_len);
            cl_fasl_write_bytes(w, bc->code, bc->code_len);
            cl_fasl_write_u16(w, bc->n_constants);
            s->frames[idx].phase = PHASE_BC_CONSTANTS;
            s->frames[idx].index = 0;
            return 0;

        case PHASE_BC_CONSTANTS: {
            uint16_t k = s->frames[idx].index;
            while (k < bc->n_constants) {
                CL_Obj cst = bc->constants[k];
                int found = 0;
                if (CL_HEAP_P(cst)) {
                    uint16_t j;
                    for (j = 0; j < k; j++) {
                        if (bc->constants[j] == cst) {
                            cl_fasl_write_u8(w, FASL_TAG_CONST_REF);
                            cl_fasl_write_u16(w, j);
                            found = 1;
#ifdef DEBUG_FASL
                            if (g_fasl_hist_enabled) g_fasl_dedup_const_ref++;
#endif
                            break;
                        }
                    }
                }
                if (found) { k++; continue; }
                /* Push child, advance index, yield. */
                s->frames[idx].index = k + 1;
                if (!fasl_ser_stack_push(w, s, cst, PHASE_START)) return 1;
                return 0;
            }
            s->frames[idx].phase = PHASE_BC_METADATA;
            s->frames[idx].index = 0;
            return 0;
        }

        case PHASE_BC_METADATA:
            cl_fasl_write_u16(w, bc->arity);
            cl_fasl_write_u16(w, bc->n_locals);
            cl_fasl_write_u16(w, bc->n_upvalues);
            cl_fasl_write_u8(w, bc->n_optional);
            cl_fasl_write_u8(w, bc->flags);
            cl_fasl_write_u8(w, bc->n_keys);
            s->frames[idx].phase = PHASE_BC_KEY_SYMS;
            s->frames[idx].index = 0;
            return 0;

        case PHASE_BC_KEY_SYMS: {
            uint16_t k = s->frames[idx].index;
            if (k < bc->n_keys) {
                CL_Obj ks = bc->key_syms[k];
                s->frames[idx].index = k + 1;
                if (!fasl_ser_stack_push(w, s, ks, PHASE_START)) return 1;
                return 0;
            }
            s->frames[idx].phase = PHASE_BC_POSTLUDE;
            s->frames[idx].index = 0;
            return 0;
        }

        case PHASE_BC_POSTLUDE:
            for (i = 0; i < bc->n_keys; i++)
                cl_fasl_write_u8(w, bc->key_slots[i]);
            for (i = 0; i < bc->n_keys; i++)
                cl_fasl_write_u8(w, bc->key_suppliedp_slots[i]);
            cl_fasl_write_u16(w, bc->source_line);
            if (bc->source_file) {
                uint16_t slen = (uint16_t)strlen(bc->source_file);
                cl_fasl_write_u16(w, slen);
                cl_fasl_write_bytes(w, bc->source_file, slen);
            } else {
                cl_fasl_write_u16(w, 0);
            }
            cl_fasl_write_u16(w, bc->line_map_count);
            for (i = 0; i < bc->line_map_count; i++) {
                cl_fasl_write_u16(w, bc->line_map[i].pc);
                cl_fasl_write_u16(w, bc->line_map[i].line);
            }
            s->frames[idx].phase = PHASE_DONE;
            if (!fasl_ser_stack_push(w, s, bc->name, PHASE_START)) return 1;
            return 0;

        case PHASE_DONE:
            return 1;

        default:
            /* Unknown phase — treat as done to avoid infinite loop. */
            return 1;
        }
    }
}

#ifdef DEBUG_FASL
/* --- Histogram bucket layout ---
 *
 * Indexed by FASL_HIST_*.  Bytes attributed via position-delta around each
 * fasl_ser_step call.  Dedup hits are bumped at the OBJ_REF / CONST_REF
 * emission sites so we can compute the bytes-saved component too. */
#define FASL_HIST_IMMEDIATE   0   /* NIL/T/UNBOUND/FIXNUM/CHARACTER */
#define FASL_HIST_SYMBOL      1
#define FASL_HIST_GENSYM      2
#define FASL_HIST_STRING      3
#define FASL_HIST_CONS        4
#define FASL_HIST_VECTOR      5
#define FASL_HIST_BIT_VECTOR  6
#define FASL_HIST_BYTECODE    7
#define FASL_HIST_CLOSURE     8
#define FASL_HIST_FUNCTION    9
#define FASL_HIST_STRUCT      10
#define FASL_HIST_FLOAT       11
#define FASL_HIST_BIGNUM      12
#define FASL_HIST_RATIO       13
#define FASL_HIST_COMPLEX     14
#define FASL_HIST_PATHNAME    15
#define FASL_HIST_CLASS_REF   16
#define FASL_HIST_OTHER       17
#define FASL_HIST_COUNT       18

static const char *cl_fasl_hist_names[FASL_HIST_COUNT] = {
    "IMMEDIATE", "SYMBOL", "GENSYM", "STRING", "CONS", "VECTOR", "BIT_VECTOR",
    "BYTECODE", "CLOSURE", "FUNCTION", "STRUCT", "FLOAT", "BIGNUM", "RATIO",
    "COMPLEX", "PATHNAME", "CLASS_REF", "OTHER"
};

static uint64_t g_fasl_hist_bytes[FASL_HIST_COUNT];
static uint32_t g_fasl_hist_count[FASL_HIST_COUNT];
/* g_fasl_dedup_obj_ref / g_fasl_dedup_const_ref / g_fasl_hist_enabled
 * are forward-declared near the top of this file so the dedup-hit emission
 * sites in fasl_ser_step can reference them. */

static int cl_fasl_classify(CL_Obj obj)
{
    if (CL_NULL_P(obj) || obj == CL_T || obj == CL_UNBOUND ||
        CL_FIXNUM_P(obj) || CL_CHAR_P(obj) || !CL_HEAP_P(obj))
        return FASL_HIST_IMMEDIATE;
    switch (CL_HDR_TYPE(CL_OBJ_TO_PTR(obj))) {
    case TYPE_SYMBOL: {
        CL_Symbol *sy = (CL_Symbol *)CL_OBJ_TO_PTR(obj);
        return CL_NULL_P(sy->package) ? FASL_HIST_GENSYM : FASL_HIST_SYMBOL;
    }
    case TYPE_STRING:        return FASL_HIST_STRING;
    case TYPE_CONS:          return FASL_HIST_CONS;
    case TYPE_VECTOR:        return FASL_HIST_VECTOR;
    case TYPE_BIT_VECTOR:    return FASL_HIST_BIT_VECTOR;
    case TYPE_BYTECODE:      return FASL_HIST_BYTECODE;
    case TYPE_CLOSURE:       return FASL_HIST_CLOSURE;
    case TYPE_FUNCTION:      return FASL_HIST_FUNCTION;
    case TYPE_STRUCT:        return FASL_HIST_STRUCT;
    case TYPE_SINGLE_FLOAT:
    case TYPE_DOUBLE_FLOAT:  return FASL_HIST_FLOAT;
    case TYPE_BIGNUM:        return FASL_HIST_BIGNUM;
    case TYPE_RATIO:         return FASL_HIST_RATIO;
    case TYPE_COMPLEX:       return FASL_HIST_COMPLEX;
    case TYPE_PATHNAME:      return FASL_HIST_PATHNAME;
    default:                 return FASL_HIST_OTHER;
    }
}

void cl_fasl_hist_begin(void)
{
    int i;
    for (i = 0; i < FASL_HIST_COUNT; i++) {
        g_fasl_hist_bytes[i] = 0;
        g_fasl_hist_count[i] = 0;
    }
    g_fasl_dedup_obj_ref = 0;
    g_fasl_dedup_const_ref = 0;
    g_fasl_hist_enabled = 1;
}

void cl_fasl_hist_end(void)
{
    g_fasl_hist_enabled = 0;
}

void cl_fasl_hist_dump(const char *label, uint32_t bytes_written)
{
    int order[FASL_HIST_COUNT];
    int i, j;
    uint64_t total_bytes = 0;
    uint32_t total_count = 0;

    for (i = 0; i < FASL_HIST_COUNT; i++) order[i] = i;
    /* Insertion sort by bytes desc — small N, fine. */
    for (i = 1; i < FASL_HIST_COUNT; i++) {
        int k = order[i];
        for (j = i; j > 0 && g_fasl_hist_bytes[order[j - 1]] < g_fasl_hist_bytes[k]; j--)
            order[j] = order[j - 1];
        order[j] = k;
    }
    for (i = 0; i < FASL_HIST_COUNT; i++) {
        total_bytes += g_fasl_hist_bytes[i];
        total_count += g_fasl_hist_count[i];
    }
    fprintf(stderr, "[FASL hist] %s — bytes_written=%u total_attributed=%llu objs=%u\n",
            label, bytes_written,
            (unsigned long long)total_bytes, total_count);
    for (i = 0; i < FASL_HIST_COUNT; i++) {
        int b = order[i];
        if (g_fasl_hist_bytes[b] == 0) continue;
        fprintf(stderr, "  %-12s bytes=%10llu  count=%9u  avg=%5llu\n",
                cl_fasl_hist_names[b],
                (unsigned long long)g_fasl_hist_bytes[b],
                g_fasl_hist_count[b],
                g_fasl_hist_count[b]
                  ? (unsigned long long)(g_fasl_hist_bytes[b] / g_fasl_hist_count[b])
                  : 0ULL);
    }
    fprintf(stderr, "  dedup hits: OBJ_REF=%u  CONST_REF=%u\n",
            g_fasl_dedup_obj_ref, g_fasl_dedup_const_ref);
    fflush(stderr);
}
#endif

static void fasl_serialize_drive(CL_FaslWriter *w, FaslSerStack *s)
{
    while (s->depth > 0 && !w->error) {
#ifdef DEBUG_FASL
        CL_Obj   cur_obj = s->frames[s->depth - 1].obj;
        uint32_t pre_pos = w->pos;
        int      done    = fasl_ser_step(w, s);
        if (g_fasl_hist_enabled) {
            int bucket = cl_fasl_classify(cur_obj);
            uint32_t delta = w->pos - pre_pos;
            g_fasl_hist_bytes[bucket] += delta;
            /* Count once per leaf or per composite-frame completion (PHASE_DONE) */
            if (done) g_fasl_hist_count[bucket]++;
        }
        if (done) s->depth--;
#else
        if (fasl_ser_step(w, s)) {
            s->depth--;
        }
#endif
    }
}

void cl_fasl_serialize_obj(CL_FaslWriter *w, CL_Obj obj)
{
    FaslSerStack s;
    s.frames = NULL;
    s.depth = 0;
    s.capacity = 0;
    if (fasl_ser_stack_push(w, &s, obj, PHASE_START))
        fasl_serialize_drive(w, &s);
    if (s.frames) platform_free(s.frames);
}

/* Kept as a no-op for ABI compatibility; the iterative serializer no
 * longer uses recursion-depth or timeout state. */
void cl_fasl_reset_serialize_count(void)
{
}

/* --- Serialize a CL_Bytecode body (no leading tag) ---
 *
 * Used by bi_compile_file to write each top-level form as a length-prefixed
 * unit.  Skips the FASL_TAG_BYTECODE byte (the unit framing has its own
 * length prefix) and the shared-object dedup wrapper. */
void cl_fasl_serialize_bytecode(CL_FaslWriter *w, CL_Obj bc_obj)
{
    FaslSerStack s;
    s.frames = NULL;
    s.depth = 0;
    s.capacity = 0;
    /* Initial frame: skip dispatch+tag, jump directly to the BC body phases. */
    if (fasl_ser_stack_push(w, &s, bc_obj, PHASE_BC_AFTER_TAG))
        fasl_serialize_drive(w, &s);
    if (s.frames) platform_free(s.frames);
}

/* ================================================================
 * Reader
 * ================================================================ */

void cl_fasl_reader_init(CL_FaslReader *r, const uint8_t *data, uint32_t size)
{
    r->data = data;
    r->size = size;
    r->pos = 0;
    r->error = FASL_OK;
    r->gensym_count = 0;
    r->shared_count = 0;
    r->shared_objs = NULL;  /* lazily allocated when OBJ_DEF tag is encountered */
    r->pending_obj_def_id = 0xFFFFFFFFu;  /* none pending */
}

/* OBJ_DEF / cycle-handling helpers — see notes in FASL_TAG_OBJ_DEF case. */
#define CL_FASL_NO_PENDING 0xFFFFFFFFu

static void cl_fasl_set_pending_obj_def(CL_FaslReader *r, uint16_t id)
{
    r->pending_obj_def_id = id;
}

/* Called by allocate-then-fill body deserializers (struct/closure/bytecode)
 * right after they allocate the shell.  Registers the shell at the pending
 * id (if any) so cyclic OBJ_REFs inside the body resolve to the shell.
 * Idempotent and safe to call when no id is pending. */
static void cl_fasl_consume_pending_obj_def(CL_FaslReader *r, CL_Obj shell)
{
    if (r->pending_obj_def_id == CL_FASL_NO_PENDING) return;
    if (r->shared_objs && r->pending_obj_def_id < FASL_MAX_SHARED) {
        uint16_t id = (uint16_t)r->pending_obj_def_id;
        r->shared_objs[id] = shell;
        if (id >= r->shared_count) r->shared_count = id + 1;
    }
    r->pending_obj_def_id = CL_FASL_NO_PENDING;
}

uint8_t cl_fasl_read_u8(CL_FaslReader *r)
{
    if (r->error) return 0;
    if (r->pos + 1 > r->size) { r->error = FASL_ERR_TRUNCATED; return 0; }
    return r->data[r->pos++];
}

uint16_t cl_fasl_read_u16(CL_FaslReader *r)
{
    uint16_t val;
    if (r->error) return 0;
    if (r->pos + 2 > r->size) { r->error = FASL_ERR_TRUNCATED; return 0; }
    val = (uint16_t)((r->data[r->pos] << 8) | r->data[r->pos + 1]);
    r->pos += 2;
    return val;
}

uint32_t cl_fasl_read_u32(CL_FaslReader *r)
{
    uint32_t val;
    if (r->error) return 0;
    if (r->pos + 4 > r->size) { r->error = FASL_ERR_TRUNCATED; return 0; }
    val = ((uint32_t)r->data[r->pos] << 24) |
          ((uint32_t)r->data[r->pos + 1] << 16) |
          ((uint32_t)r->data[r->pos + 2] << 8) |
          ((uint32_t)r->data[r->pos + 3]);
    r->pos += 4;
    return val;
}

void cl_fasl_read_bytes(CL_FaslReader *r, void *out, uint32_t len)
{
    if (r->error) return;
    if (r->pos + len > r->size) { r->error = FASL_ERR_TRUNCATED; return; }
    memcpy(out, r->data + r->pos, len);
    r->pos += len;
}

uint32_t cl_fasl_read_header(CL_FaslReader *r)
{
    uint32_t magic;
    uint16_t version;

    magic = cl_fasl_read_u32(r);
    if (r->error) return 0;
    if (magic != CL_FASL_MAGIC) { r->error = FASL_ERR_BAD_MAGIC; return 0; }

    version = cl_fasl_read_u16(r);
    if (r->error) return 0;
    if (version != CL_FASL_VERSION) { r->error = FASL_ERR_BAD_VERSION; return 0; }

    cl_fasl_read_u16(r);  /* flags — reserved */
    return cl_fasl_read_u32(r);
}

/* --- Deserialize a CL_Obj constant --- */

CL_Obj cl_fasl_deserialize_obj(CL_FaslReader *r)
{
    uint8_t tag;

    if (r->error) return CL_NIL;

    tag = cl_fasl_read_u8(r);
    if (r->error) return CL_NIL;

    switch (tag) {
    case FASL_TAG_NIL:
        return CL_NIL;

    case FASL_TAG_T:
        return CL_T;

    case FASL_TAG_UNBOUND:
        return CL_UNBOUND;

    case FASL_TAG_FIXNUM:
        return cl_fasl_read_u32(r);

    case FASL_TAG_CHARACTER:
        return cl_fasl_read_u32(r);

    case FASL_TAG_SYMBOL: {
        uint16_t pkg_len = cl_fasl_read_u16(r);
        char pkg_buf[256], sym_buf[256];
        uint16_t sym_len;
        CL_Obj pkg_obj;

        if (r->error) return CL_NIL;

        if (pkg_len == 0) {
            /* Uninterned symbol */
            sym_len = cl_fasl_read_u16(r);
            if (r->error || sym_len >= sizeof(sym_buf)) return CL_NIL;
            cl_fasl_read_bytes(r, sym_buf, sym_len);
            sym_buf[sym_len] = '\0';
            return cl_make_symbol(cl_make_string(sym_buf, sym_len));
        }

        if (pkg_len == 0xFFFF) {
            /* Keyword */
            sym_len = cl_fasl_read_u16(r);
            if (r->error || sym_len >= sizeof(sym_buf)) return CL_NIL;
            cl_fasl_read_bytes(r, sym_buf, sym_len);
            sym_buf[sym_len] = '\0';
            pkg_obj = cl_find_package("KEYWORD", 7);
            return cl_intern_in(sym_buf, sym_len, pkg_obj);
        }

        /* Named package */
        if (pkg_len >= sizeof(pkg_buf)) return CL_NIL;
        cl_fasl_read_bytes(r, pkg_buf, pkg_len);
        pkg_buf[pkg_len] = '\0';

        sym_len = cl_fasl_read_u16(r);
        if (r->error || sym_len >= sizeof(sym_buf)) return CL_NIL;
        cl_fasl_read_bytes(r, sym_buf, sym_len);
        sym_buf[sym_len] = '\0';

        pkg_obj = cl_find_package(pkg_buf, pkg_len);
        if (CL_NULL_P(pkg_obj)) {
            /* Package not found — intern in CL-USER as fallback */
            pkg_obj = cl_find_package("COMMON-LISP-USER", 16);
        }
        return cl_intern_in(sym_buf, sym_len, pkg_obj);
    }

    case FASL_TAG_GENSYM_DEF: {
        uint16_t id = cl_fasl_read_u16(r);
        uint16_t sym_len = cl_fasl_read_u16(r);
        char sym_buf[256];
        CL_Obj sym_obj;
        if (r->error || sym_len >= sizeof(sym_buf)) return CL_NIL;
        cl_fasl_read_bytes(r, sym_buf, sym_len);
        sym_buf[sym_len] = '\0';
        sym_obj = cl_make_symbol(cl_make_string(sym_buf, sym_len));
        if (id < FASL_MAX_GENSYMS) {
            r->gensym_objs[id] = sym_obj;
            if (id >= r->gensym_count)
                r->gensym_count = id + 1;
        }
        return sym_obj;
    }

    case FASL_TAG_GENSYM_REF: {
        uint16_t id = cl_fasl_read_u16(r);
        if (r->error || id >= r->gensym_count) return CL_NIL;
        return r->gensym_objs[id];
    }

    case FASL_TAG_OBJ_DEF: {
        uint16_t id = cl_fasl_read_u16(r);
        CL_Obj result;
        if (r->error) return CL_NIL;
        /* Lazily allocate shared table */
        if (!r->shared_objs) {
            r->shared_objs = (CL_Obj *)platform_alloc(FASL_MAX_SHARED * sizeof(CL_Obj));
            if (r->shared_objs)
                memset(r->shared_objs, 0, FASL_MAX_SHARED * sizeof(CL_Obj));
        }
        /* Stash the id so allocate-then-fill body deserializers (struct,
         * closure, bytecode) can register their freshly-allocated shell
         * at this id BEFORE recursing into children.  This is required
         * for cyclic graphs (e.g. CLOS gf ↔ method ↔ method-fn-closure)
         * — the writer correctly emits OBJ_REF for self-references during
         * the body walk, but if the reader registers only AFTER the
         * recursive deserialize returns, those mid-body OBJ_REFs resolve
         * to NIL — corrupting slots/upvalues with NIL where a back-
         * reference belongs.  Symptom: runtime "%STRUCT-REF: not a
         * structure" when the corrupted slot is later accessed.
         *
         * If the body deserializer doesn't consume the pending id
         * (because its type doesn't form cycles via shared_objs —
         * string, vector, cons, symbol, package, etc.), the fallback
         * branch below registers here. */
        cl_fasl_set_pending_obj_def(r, id);
        result = cl_fasl_deserialize_obj(r);
        if (r->pending_obj_def_id != CL_FASL_NO_PENDING) {
            /* Body didn't consume — register now (atomic types). */
            if (r->shared_objs && id < FASL_MAX_SHARED) {
                r->shared_objs[id] = result;
                if (id >= r->shared_count) r->shared_count = id + 1;
            }
            r->pending_obj_def_id = CL_FASL_NO_PENDING;
        }
        return result;
    }

    case FASL_TAG_CLASS_REF: {
        /* Counterpart to FASL_TAG_CLASS_REF in the writer.  The class is
         * looked up by name at load time so identity matches the live
         * metaobject (defmethod specializers EQ the runtime class). */
        CL_Obj name = cl_fasl_deserialize_obj(r);
        CL_Obj find_class_sym, fn, arg, result;
        CL_Symbol *fc_sym;
        if (r->error || CL_NULL_P(name) ||
            !CL_HEAP_P(name) ||
            CL_HDR_TYPE(CL_OBJ_TO_PTR(name)) != TYPE_SYMBOL)
            return CL_NIL;
        CL_GC_PROTECT(name);
        find_class_sym = cl_intern_in("FIND-CLASS", 10, cl_package_cl);
        fc_sym = (CL_Symbol *)CL_OBJ_TO_PTR(find_class_sym);
        fn = fc_sym->function;
        if (CL_NULL_P(fn)) {
            CL_GC_UNPROTECT(1);
            return CL_NIL;
        }
        arg = name;
        result = cl_vm_apply(fn, &arg, 1);
        CL_GC_UNPROTECT(1);
        return result;
    }

    case FASL_TAG_OBJ_REF: {
        uint16_t id = cl_fasl_read_u16(r);
        if (r->error || !r->shared_objs || id >= r->shared_count) return CL_NIL;
        return r->shared_objs[id];
    }

    case FASL_TAG_STRING: {
        uint32_t len = cl_fasl_read_u32(r);
        char *tmp;
        CL_Obj result;
        if (r->error) return CL_NIL;
        tmp = (char *)platform_alloc(len + 1);
        if (!tmp) return CL_NIL;
        cl_fasl_read_bytes(r, tmp, len);
        tmp[len] = '\0';
        result = cl_make_string(tmp, len);
        platform_free(tmp);
        return result;
    }

#ifdef CL_WIDE_STRINGS
    case FASL_TAG_WIDE_STRING: {
        uint32_t len = cl_fasl_read_u32(r);
        uint32_t *tmp;
        uint32_t i;
        CL_Obj result;
        if (r->error) return CL_NIL;
        tmp = (uint32_t *)platform_alloc(len * sizeof(uint32_t));
        if (!tmp) return CL_NIL;
        for (i = 0; i < len; i++)
            tmp[i] = cl_fasl_read_u32(r);
        result = cl_make_wide_string(tmp, len);
        platform_free(tmp);
        return result;
    }
#endif

    case FASL_TAG_CONS: {
        /* Iterative list building to avoid O(N) GC root stack depth.
         * Consecutive CONS tags are common (proper lists), so we build
         * the list with only 3 GC roots instead of one per element. */
        CL_Obj result = CL_NIL, tail = CL_NIL, car_val = CL_NIL;
        int first = 1;
        CL_GC_PROTECT(result);
        CL_GC_PROTECT(tail);
        CL_GC_PROTECT(car_val);

        do {
            if (!first)
                r->pos++;  /* consume peeked FASL_TAG_CONS */
            first = 0;

            car_val = cl_fasl_deserialize_obj(r);

            if (CL_NULL_P(result)) {
                result = cl_cons(car_val, CL_NIL);
                tail = result;
            } else {
                CL_Obj new_cell = cl_cons(car_val, CL_NIL);
                ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = new_cell;
                tail = new_cell;
            }
        } while (r->pos < r->size && r->data[r->pos] == FASL_TAG_CONS);

        /* Deserialize final cdr (NIL for proper lists, atom for dotted) */
        {
            CL_Obj final_cdr = cl_fasl_deserialize_obj(r);
            ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = final_cdr;
        }

        CL_GC_UNPROTECT(3);
        return result;
    }

    case FASL_TAG_BYTECODE:
        return cl_fasl_deserialize_bytecode(r);

    case FASL_TAG_SINGLE_FLOAT: {
        uint32_t bits = cl_fasl_read_u32(r);
        float f;
        memcpy(&f, &bits, 4);
        return cl_make_single_float(f);
    }

    case FASL_TAG_DOUBLE_FLOAT: {
        uint8_t bytes[8];
        double d;
        cl_fasl_read_bytes(r, bytes, 8);
        if (r->error) return CL_NIL;
        /* Convert from big-endian to native */
        {
            union { double d; uint8_t b[8]; } u;
            u.d = 1.0;
            if (u.b[0] == 0x3F) {
                /* Big-endian host */
                memcpy(&d, bytes, 8);
            } else {
                /* Little-endian host: reverse */
                uint8_t rev[8];
                int i;
                for (i = 0; i < 8; i++) rev[i] = bytes[7 - i];
                memcpy(&d, rev, 8);
            }
        }
        return cl_make_double_float(d);
    }

    case FASL_TAG_RATIO: {
        CL_Obj num, den;
        num = cl_fasl_deserialize_obj(r);
        CL_GC_PROTECT(num);
        den = cl_fasl_deserialize_obj(r);
        CL_GC_PROTECT(den);
        {
            CL_Obj result = cl_make_ratio(num, den);
            CL_GC_UNPROTECT(2);
            return result;
        }
    }

    case FASL_TAG_COMPLEX: {
        CL_Obj real, imag;
        real = cl_fasl_deserialize_obj(r);
        CL_GC_PROTECT(real);
        imag = cl_fasl_deserialize_obj(r);
        CL_GC_PROTECT(imag);
        {
            CL_Obj result = cl_make_complex(real, imag);
            CL_GC_UNPROTECT(2);
            return result;
        }
    }

    case FASL_TAG_BIGNUM: {
        uint8_t sign = cl_fasl_read_u8(r);
        uint32_t n_limbs = cl_fasl_read_u32(r);
        CL_Obj result;
        CL_Bignum *bn;
        uint32_t i;
        if (r->error) return CL_NIL;
        result = cl_make_bignum(n_limbs, sign);
        bn = (CL_Bignum *)CL_OBJ_TO_PTR(result);
        for (i = 0; i < n_limbs; i++)
            bn->limbs[i] = cl_fasl_read_u16(r);
        return result;
    }

    case FASL_TAG_VECTOR: {
        uint32_t len = cl_fasl_read_u32(r);
        CL_Obj result;
        CL_Vector *v;
        uint32_t i;
        if (r->error) return CL_NIL;
        result = cl_make_vector(len);
        CL_GC_PROTECT(result);
        v = (CL_Vector *)CL_OBJ_TO_PTR(result);
        for (i = 0; i < len; i++) {
            CL_Obj elt = cl_fasl_deserialize_obj(r);
            /* Re-fetch v since GC may have occurred */
            v = (CL_Vector *)CL_OBJ_TO_PTR(result);
            v->data[i] = elt;
        }
        CL_GC_UNPROTECT(1);
        return result;
    }

    case FASL_TAG_BIT_VECTOR: {
        uint32_t nbits = cl_fasl_read_u32(r);
        uint32_t n_words = CL_BV_WORDS(nbits);
        CL_Obj result;
        CL_BitVector *bv;
        uint32_t i;
        if (r->error) return CL_NIL;
        result = cl_make_bit_vector(nbits);
        bv = (CL_BitVector *)CL_OBJ_TO_PTR(result);
        for (i = 0; i < n_words; i++)
            bv->data[i] = cl_fasl_read_u32(r);
        return result;
    }

    case FASL_TAG_PATHNAME: {
        CL_Obj components[6];
        int i;
        CL_Obj result;
        for (i = 0; i < 6; i++) {
            components[i] = cl_fasl_deserialize_obj(r);
            CL_GC_PROTECT(components[i]);
        }
        result = cl_make_pathname(components[0], components[1],
                                  components[2], components[3],
                                  components[4], components[5]);
        CL_GC_UNPROTECT(6);
        return result;
    }

    case FASL_TAG_STRUCT: {
        CL_Obj type_desc;
        uint32_t pending_id_save = r->pending_obj_def_id;
        uint32_t n_slots, i;
        CL_Obj st_obj;
        /* type_desc is read first; clear pending so it doesn't apply to
         * the type_desc symbol/struct.  We restore it before consuming. */
        r->pending_obj_def_id = CL_FASL_NO_PENDING;
        type_desc = cl_fasl_deserialize_obj(r);
        n_slots = cl_fasl_read_u32(r);
        if (r->error) return CL_NIL;
        CL_GC_PROTECT(type_desc);
        st_obj = cl_make_struct(type_desc, n_slots);
        CL_GC_PROTECT(st_obj);
        /* Register shell at the OBJ_DEF id (if any) before reading slots
         * so cyclic OBJ_REFs in slots resolve to this shell. */
        r->pending_obj_def_id = pending_id_save;
        cl_fasl_consume_pending_obj_def(r, st_obj);
        {
            CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(st_obj);
            for (i = 0; i < n_slots; i++) {
                CL_Obj val = cl_fasl_deserialize_obj(r);
                /* Re-fetch pointer — GC may have moved during deserialization */
                st = (CL_Struct *)CL_OBJ_TO_PTR(st_obj);
                st->slots[i] = val;
            }
        }
        CL_GC_UNPROTECT(2);
        return st_obj;
    }

    case FASL_TAG_CLOSURE: {
        CL_Obj bc_val;
        uint16_t n_upvals;
        uint32_t pending_id_save = r->pending_obj_def_id;
        CL_Closure *cl;
        CL_Obj result;
        uint16_t i;
        /* Clear pending while reading the bytecode child; restore before
         * consuming so the OBJ_DEF id applies to this closure shell. */
        r->pending_obj_def_id = CL_FASL_NO_PENDING;
        bc_val = cl_fasl_deserialize_obj(r);
        n_upvals = cl_fasl_read_u16(r);
        if (r->error) return CL_NIL;
        CL_GC_PROTECT(bc_val);
        cl = (CL_Closure *)cl_alloc(TYPE_CLOSURE,
            sizeof(CL_Closure) + n_upvals * sizeof(CL_Obj));
        if (!cl) { CL_GC_UNPROTECT(1); return CL_NIL; }
        cl->bytecode = bc_val;
        result = CL_PTR_TO_OBJ(cl);
        CL_GC_PROTECT(result);
        /* Register shell at the OBJ_DEF id before reading upvalues. */
        r->pending_obj_def_id = pending_id_save;
        cl_fasl_consume_pending_obj_def(r, result);
        for (i = 0; i < n_upvals; i++) {
            CL_Obj uv = cl_fasl_deserialize_obj(r);
            cl = (CL_Closure *)CL_OBJ_TO_PTR(result); /* refresh */
            cl->upvalues[i] = uv;
        }
        CL_GC_UNPROTECT(2);
        return result;
    }

    case FASL_TAG_PACKAGE: {
        uint16_t name_len = cl_fasl_read_u16(r);
        char pkg_buf[256];
        if (r->error || name_len >= sizeof(pkg_buf)) return CL_NIL;
        cl_fasl_read_bytes(r, pkg_buf, name_len);
        pkg_buf[name_len] = '\0';
        return cl_find_package(pkg_buf, name_len);
    }

    case FASL_TAG_LOCK: {
        /* Counterpart to FASL_TAG_LOCK in the writer.  We can't restore
         * the original platform mutex (per-process state) — instead we
         * allocate a fresh, equivalent lock with the same name and
         * recursive-ness.  Sharing within this FASL is preserved by the
         * surrounding OBJ_DEF/OBJ_REF wrapper. */
        uint8_t flags = cl_fasl_read_u8(r);
        CL_Obj name = cl_fasl_deserialize_obj(r);
        CL_Obj lock_obj;
        if (r->error) return CL_NIL;
        CL_GC_PROTECT(name);
        lock_obj = cl_lock_alloc_obj((flags & 0x01) ? 1 : 0, name, "FASL");
        CL_GC_UNPROTECT(1);
        return lock_obj;
    }

    case FASL_TAG_FUNCTION: {
        /* C builtin — deserialize name symbol and look up its function */
        CL_Obj name_sym = cl_fasl_deserialize_obj(r);
        if (r->error || CL_NULL_P(name_sym)) return CL_NIL;
        if (CL_HEAP_P(name_sym) &&
            CL_HDR_TYPE(CL_OBJ_TO_PTR(name_sym)) == TYPE_SYMBOL) {
            CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(name_sym);
            if (!CL_NULL_P(sym->function))
                return sym->function;
        }
        return CL_NIL;
    }

    default:
        r->error = FASL_ERR_BAD_TAG;
        return CL_NIL;
    }
}

/* --- Deserialize a CL_Bytecode --- */

CL_Obj cl_fasl_deserialize_bytecode(CL_FaslReader *r)
{
    CL_Bytecode *bc;
    CL_Obj bc_obj;
    uint32_t code_len;
    uint16_t n_consts, i;
    uint16_t source_file_len;

    if (r->error) return CL_NIL;

    /* Allocate bytecode on heap */
    bc = (CL_Bytecode *)cl_alloc(TYPE_BYTECODE, sizeof(CL_Bytecode));
    if (!bc) return CL_NIL;
    bc_obj = CL_PTR_TO_OBJ(bc);
    CL_GC_PROTECT(bc_obj);

    /* Register shell at OBJ_DEF id (if any) before reading constants —
     * a closure inside our constant pool may OBJ_REF this bytecode. */
    cl_fasl_consume_pending_obj_def(r, bc_obj);

    /* Zero out all fields */
    bc->code = NULL;
    bc->constants = NULL;
    bc->code_len = 0;
    bc->n_constants = 0;
    bc->arity = 0;
    bc->n_locals = 0;
    bc->n_upvalues = 0;
    bc->name = CL_NIL;
    bc->n_optional = 0;
    bc->flags = 0;
    bc->n_keys = 0;
    bc->key_syms = NULL;
    bc->key_slots = NULL;
    bc->key_suppliedp_slots = NULL;
    bc->line_map = NULL;
    bc->line_map_count = 0;
    bc->source_line = 0;
    bc->source_file = NULL;

    /* Code */
    code_len = cl_fasl_read_u32(r);
    if (r->error) { CL_GC_UNPROTECT(1); return CL_NIL; }

    bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj); /* refresh after potential GC */
    bc->code = (uint8_t *)platform_alloc(code_len);
    if (!bc->code) { CL_GC_UNPROTECT(1); return CL_NIL; }
    cl_fasl_read_bytes(r, bc->code, code_len);
    bc->code_len = code_len;

    /* Constants */
    n_consts = cl_fasl_read_u16(r);
    if (r->error) { CL_GC_UNPROTECT(1); return CL_NIL; }

    bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
    if (n_consts > 0) {
        bc->constants = (CL_Obj *)platform_alloc(n_consts * sizeof(CL_Obj));
        if (!bc->constants) { CL_GC_UNPROTECT(1); return CL_NIL; }
        bc->n_constants = n_consts;
        /* Initialize to NIL so GC doesn't see garbage */
        for (i = 0; i < n_consts; i++)
            bc->constants[i] = CL_NIL;
    }

    for (i = 0; i < n_consts; i++) {
        CL_Obj val;
        /* Check for back-reference to earlier constant (dedup) */
        if (!r->error && r->pos < r->size &&
            r->data[r->pos] == FASL_TAG_CONST_REF) {
            uint16_t ref_idx;
            cl_fasl_read_u8(r);  /* consume tag */
            ref_idx = cl_fasl_read_u16(r);
            if (r->error) { CL_GC_UNPROTECT(1); return CL_NIL; }
            bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
            bc->constants[i] = bc->constants[ref_idx];
            continue;
        }
        val = cl_fasl_deserialize_obj(r);
        if (r->error) { CL_GC_UNPROTECT(1); return CL_NIL; }
        bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj); /* refresh */
        bc->constants[i] = val;
    }

    /* Metadata */
    bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
    bc->arity = cl_fasl_read_u16(r);
    bc->n_locals = cl_fasl_read_u16(r);
    bc->n_upvalues = cl_fasl_read_u16(r);
    bc->n_optional = cl_fasl_read_u8(r);
    bc->flags = cl_fasl_read_u8(r);
    bc->n_keys = cl_fasl_read_u8(r);

    /* Key params */
    if (bc->n_keys > 0) {
        uint8_t nk = bc->n_keys;
        bc->key_syms = (CL_Obj *)platform_alloc(nk * sizeof(CL_Obj));
        bc->key_slots = (uint8_t *)platform_alloc(nk);
        bc->key_suppliedp_slots = (uint8_t *)platform_alloc(nk);
        if (!bc->key_syms || !bc->key_slots || !bc->key_suppliedp_slots) {
            CL_GC_UNPROTECT(1);
            return CL_NIL;
        }
        /* Initialize key_syms to NIL */
        for (i = 0; i < nk; i++)
            bc->key_syms[i] = CL_NIL;

        for (i = 0; i < nk; i++) {
            CL_Obj ksym = cl_fasl_deserialize_obj(r);
            bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
            bc->key_syms[i] = ksym;
        }
        bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
        for (i = 0; i < nk; i++)
            bc->key_slots[i] = cl_fasl_read_u8(r);
        for (i = 0; i < nk; i++)
            bc->key_suppliedp_slots[i] = cl_fasl_read_u8(r);
    }

    /* Source info */
    bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
    bc->source_line = cl_fasl_read_u16(r);
    source_file_len = cl_fasl_read_u16(r);
    if (source_file_len > 0) {
        char *sf = (char *)platform_alloc(source_file_len + 1);
        if (sf) {
            cl_fasl_read_bytes(r, sf, source_file_len);
            sf[source_file_len] = '\0';
            bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
            bc->source_file = sf;
        }
    }

    /* Line map */
    {
        uint16_t lm_count;
        bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
        lm_count = cl_fasl_read_u16(r);
        if (lm_count > 0) {
            bc->line_map = (CL_LineEntry *)platform_alloc(
                lm_count * sizeof(CL_LineEntry));
            bc->line_map_count = lm_count;
            if (bc->line_map) {
                for (i = 0; i < lm_count; i++) {
                    bc->line_map[i].pc = cl_fasl_read_u16(r);
                    bc->line_map[i].line = cl_fasl_read_u16(r);
                }
            }
        }
    }

    /* Name */
    {
        CL_Obj name = cl_fasl_deserialize_obj(r);
        bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
        bc->name = name;
    }

    CL_GC_UNPROTECT(1);
    return bc_obj;
}

/* ================================================================
 * High-level FASL load
 * ================================================================ */

CL_Obj cl_fasl_load(const uint8_t *data, uint32_t size)
{
    CL_FaslReader r;
    uint32_t n_units, i;

    cl_fasl_reader_init(&r, data, size);
    n_units = cl_fasl_read_header(&r);
    if (r.error) {
        switch (r.error) {
        case FASL_ERR_BAD_MAGIC:
            cl_error(CL_ERR_GENERAL, "FASL: invalid magic number");
            break;
        case FASL_ERR_BAD_VERSION:
            cl_error(CL_ERR_GENERAL, "FASL: unsupported version");
            break;
        default:
            cl_error(CL_ERR_GENERAL, "FASL: header read error");
            break;
        }
        return CL_NIL;
    }

    for (i = 0; i < n_units; i++) {
        CL_Obj bc_obj;
        cl_fasl_read_u32(&r); /* skip unit length */

        if (r.error) {
            cl_error(CL_ERR_GENERAL, "FASL: truncated unit header");
            return CL_NIL;
        }

        bc_obj = cl_fasl_deserialize_bytecode(&r);
        if (r.error) {
            cl_error(CL_ERR_GENERAL, "FASL: error deserializing unit");
            return CL_NIL;
        }

        if (!CL_NULL_P(bc_obj))
            cl_vm_eval(bc_obj);
    }

    if (r.shared_objs) platform_free(r.shared_objs);
    return CL_T;
}
