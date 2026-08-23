/*
 * image.c — heap image save/restore (EXT:SAVE-IMAGE / --image).
 *
 * Design: specs/image-save-load.md.  The load-bearing invariant is
 * "restore set == GC root set": the compacting GC already forces every
 * C location that holds a CL_Obj across a collection to be registered
 * (global root registry, the named shared tables in gc_mark, the
 * readtable pool, per-thread state), so the image writer dumps exactly
 * that enumeration plus the raw arena payload, and the loader overwrites
 * the same enumeration in a freshly-initialized process of the SAME
 * build.  Heap references are arena-relative offsets, so the payload
 * needs no relocation; the only per-process fixups are C pointers
 * (builtin CFuncs, bytecode side buffers, OS handles), handled by one
 * linear relink walk.
 */

#include "image.h"
#include "mem.h"
#include "error.h"
#include "symbol.h"
#include "package.h"
#include "bindtab.h"      /* :SHAKE-BINDINGS sheds the binding tables */
#include "stream.h"
#include "reader.h"
#include "readtable.h"
#include "compiler.h"     /* cl_intern_source_file */
#include "vm.h"
#include "builtins.h"
#include "fasl.h"         /* CL_FASL_VERSION keys the bytecode encoding */
#include "string_utils.h"
#include "thread.h"
#include "../platform/platform.h"
#include "../platform/platform_thread.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>   /* _exit — see image_fatal */

/* thread.h maps gc_root_count to (CT->gc_root_count) for legacy call
 * sites; this file walks explicit CL_Thread pointers, so the raw struct
 * member must stay addressable (same idiom as mem.c). */
#undef gc_root_count

/* The named shared globals gc_mark marks directly (mem.c keeps the same
 * extern list).  Dumped/restored in THIS order — it is part of the image
 * format, so a new shared global means a CL_IMAGE_VERSION bump. */
extern CL_Obj cl_package_registry;
extern CL_Obj macro_table, setf_table, setf_fn_table, setf_expander_table,
              type_table, compiler_macro_table;
extern CL_Obj cl_clos_class_table;
extern CL_Obj struct_table;
extern CL_Obj condition_hierarchy;
extern CL_Obj condition_slot_table;
extern CL_Obj condition_default_initargs;
extern CL_Obj condition_slot_initforms;

extern CL_Obj cl_main_thread_lisp_obj(void);
extern CL_Obj *cl_main_thread_lisp_obj_ptr(void);
extern CL_Thread *cl_main_thread_ptr;

#define N_IMAGE_NAMED_ROOTS 13

static CL_Obj *image_named_root(int i)
{
    switch (i) {
    case 0:  return &cl_package_registry;
    case 1:  return &macro_table;
    case 2:  return &setf_table;
    case 3:  return &setf_fn_table;
    case 4:  return &setf_expander_table;
    case 5:  return &type_table;
    case 6:  return &compiler_macro_table;
    case 7:  return &cl_clos_class_table;
    case 8:  return &struct_table;
    case 9:  return &condition_hierarchy;
    case 10: return &condition_slot_table;
    case 11: return &condition_default_initargs;
    case 12: return &condition_slot_initforms;
    default: return NULL;
    }
}

/* ================================================================
 * Build fingerprint
 * ================================================================ */

static uint64_t fnv1a64(uint64_t h, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i;
    for (i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

static uint64_t fnv1a64_u32(uint64_t h, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
    return fnv1a64(h, b, 4);
}

static int image_little_endian(void)
{
    uint16_t v = 1;
    return *(const uint8_t *)&v;
}

static uint16_t image_flags(void)
{
    uint16_t f = 0;
#ifdef CL_WIDE_STRINGS
    f |= CL_IMAGE_FLAG_WIDE;
#endif
#ifdef __HAVE_68881__
    f |= CL_IMAGE_FLAG_FPU;
#endif
    if (image_little_endian())
        f |= CL_IMAGE_FLAG_LE;
    return f;
}

static void image_fingerprint(uint8_t out[CL_IMAGE_FPRINT_LEN])
{
    /* Four FNV-1a-64 lanes over different aspects of the build.  Any
     * difference in version, bytecode/FASL format, alignment, struct
     * layout, type-tag set or feature flags flips at least one lane. */
    uint64_t h1 = 1469598103934665603ull;   /* versions + formats */
    uint64_t h2 = 1469598103934665603ull;   /* heap struct sizes */
    uint64_t h3 = 1469598103934665603ull;   /* ABI + features */
    uint64_t h4;
    int i;

    h1 = fnv1a64(h1, CL_VERSION_STRING_FULL, CL_VERSION_LEN_FULL);
    h1 = fnv1a64_u32(h1, CL_FASL_VERSION);
    h1 = fnv1a64_u32(h1, CL_IMAGE_VERSION);

    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Cons));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Symbol));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_String));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Function));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Bytecode));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Closure));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Vector));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Package));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Hashtable));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Condition));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Struct));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Bignum));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Ratio));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Complex));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Stream));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_RandomState));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_BitVector));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Pathname));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Cell));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_ThreadObj));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Lock));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_CondVar));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_ForeignPtr));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_Restart));
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_ByteVector));
#ifdef CL_WIDE_STRINGS
    h2 = fnv1a64_u32(h2, (uint32_t)sizeof(CL_WideString));
#endif

    h3 = fnv1a64_u32(h3, CL_ALIGN);
    h3 = fnv1a64_u32(h3, (uint32_t)sizeof(void *));
    h3 = fnv1a64_u32(h3, (uint32_t)image_little_endian());
    h3 = fnv1a64_u32(h3, (uint32_t)CL_TYPE_MAX);
    h3 = fnv1a64_u32(h3, (uint32_t)image_flags());
#ifdef CL_GENGC
    h3 = fnv1a64_u32(h3, 1u);
#else
    h3 = fnv1a64_u32(h3, 0u);
#endif
#ifdef CL_TLAB
    h3 = fnv1a64_u32(h3, 1u);
#else
    h3 = fnv1a64_u32(h3, 0u);
#endif
#if defined(PLATFORM_MORPHOS)
    h3 = fnv1a64(h3, "morphos", 7);
#elif defined(PLATFORM_AMIGA)
    h3 = fnv1a64(h3, "amiga", 5);
#else
    h3 = fnv1a64(h3, "posix", 5);
#endif

    h4 = fnv1a64_u32(fnv1a64_u32(h1 ^ h2 ^ h3, (uint32_t)(h1 >> 32)),
                     (uint32_t)(h2 >> 32));

    for (i = 0; i < 8; i++) {
        out[i]      = (uint8_t)(h1 >> (i * 8));
        out[8 + i]  = (uint8_t)(h2 >> (i * 8));
        out[16 + i] = (uint8_t)(h3 >> (i * 8));
        out[24 + i] = (uint8_t)(h4 >> (i * 8));
    }
}

/* ================================================================
 * Module state
 * ================================================================ */

static int image_boot_roots = -1;          /* snapshot at end of C init */

/* Pending save (armed by EXT:SAVE-IMAGE, executed at a top-level safe
 * point where the main thread is at rest). */
static char image_pending_path[1024];
static int  image_pending = 0;
static int  image_pending_quit = 0;
static int  image_pending_shake = 0;

/* Staged image awaiting restore (loaded before cl_mem_init). */
static char          *image_staged_buf = NULL;
static unsigned long  image_staged_size = 0;
static CL_ImageHeader image_staged_hdr;

static int image_restored = 0;

/* Lisp surface symbols (registered as GC roots during C init, so they are
 * part of the boot-root restore set and survive into images). */
static CL_Obj SYM_SAVE_HOOKS = CL_NIL;        /* EXT:*SAVE-HOOKS* */
static CL_Obj SYM_RESTORE_HOOKS = CL_NIL;     /* EXT:*RESTORE-HOOKS* */
static CL_Obj SYM_IMAGE_RESTORED_P = CL_NIL;  /* EXT:*IMAGE-RESTORED-P* */
static CL_Obj KW_QUIT_IMG = CL_NIL;
static CL_Obj KW_SHAKE_BINDINGS = CL_NIL;

void cl_image_note_boot_roots(void)
{
    image_boot_roots = cl_gc_global_root_count();
}

int cl_image_save_pending_p(void)
{
    return image_pending;
}

int cl_image_restored_p(void)
{
    return image_restored;
}

/* ================================================================
 * Preconditions (shared by the builtin and the deferred dump)
 * ================================================================ */

/* Returns NULL when saving is allowed, else a static message describing
 * the first violated precondition. */
static const char *image_save_preconditions(void)
{
    static char msg[512];
    uint8_t *ptr, *end;

    if (image_boot_roots < 0)
        return "SAVE-IMAGE: internal error - boot root count was never "
               "snapshotted (cl_image_note_boot_roots)";

    /* 1. No live worker threads (SBCL save-lisp-and-die doctrine). */
    if (cl_thread_count > 1) {
        snprintf(msg, sizeof(msg),
                 "SAVE-IMAGE: %u worker thread(s) are still running - "
                 "JOIN-THREAD or MP:DESTROY-THREAD them first "
                 "(threads cannot survive into an image)",
                 (unsigned)(cl_thread_count - 1));
        return msg;
    }

    /* 4. No active FASL readers/writers or MAKE-LOAD-FORM pre-pass.
     * Guaranteed at a top-level safe point; asserted for defense. */
    if (cl_fasl_reader_save_count() != 0 || cl_fasl_writer_save_count() != 0 ||
        cl_fasl_mlf_active_p())
        return "SAVE-IMAGE: a FASL load/compile is still active";

    /* 2 + 3. One arena walk: open file/socket streams and owned foreign
     * pointers.  Console, string, synonym, two-way, broadcast,
     * concatenated, cbuf and closed streams are fine. */
    ptr = cl_heap.arena + CL_ALIGN;
    end = cl_heap.arena + cl_heap.bump;
    while (ptr < end) {
        uint32_t size = CL_HDR_SIZE(ptr);
        uint8_t type = (uint8_t)CL_HDR_TYPE(ptr);
        if (size == 0) break;
        if (type == TYPE_STREAM) {
            CL_Stream *st = (CL_Stream *)ptr;
            if ((st->flags & CL_STREAM_FLAG_OPEN) &&
                (st->stream_type == CL_STREAM_FILE ||
                 st->stream_type == CL_STREAM_SOCKET)) {
                if (st->stream_type == CL_STREAM_FILE &&
                    CL_STRING_P(st->string_buf)) {
                    CL_String *s = (CL_String *)CL_OBJ_TO_PTR(st->string_buf);
                    snprintf(msg, sizeof(msg),
                             "SAVE-IMAGE: file stream \"%.300s\" is still "
                             "open - CLOSE it first (OS handles cannot "
                             "survive into an image; use EXT:*SAVE-HOOKS* "
                             "to close and EXT:*RESTORE-HOOKS* to reopen)",
                             s->data);
                } else {
                    snprintf(msg, sizeof(msg),
                             "SAVE-IMAGE: an open %s stream is still live - "
                             "close it first (OS handles cannot survive "
                             "into an image; use EXT:*SAVE-HOOKS* to close "
                             "and EXT:*RESTORE-HOOKS* to reopen)",
                             st->stream_type == CL_STREAM_SOCKET ? "socket"
                                                                 : "file");
                }
                return msg;
            }
        } else if (type == TYPE_FOREIGN_POINTER) {
            CL_ForeignPtr *fp = (CL_ForeignPtr *)ptr;
            if ((fp->flags & CL_FPTR_FLAG_OWNED) && fp->address != 0)
                return "SAVE-IMAGE: an owned foreign pointer (FFI "
                       "allocation) is still live - free it first, or tear "
                       "it down in EXT:*SAVE-HOOKS* and rebuild it in "
                       "EXT:*RESTORE-HOOKS*";
        }
        ptr += size;
    }

    return NULL;
}

/* Main thread completely at rest: nothing transient holds heap state, so
 * the persistent enumeration below IS the whole session state. */
static int image_main_thread_at_rest(void)
{
    CL_Thread *t = cl_main_thread_ptr;
    return t->vm.sp == 0 && t->vm.fp == 0 && t->dyn_top == 0 &&
           t->nlx_top == 0 && t->handler_top == 0 && t->restart_top == 0 &&
           t->gc_root_count == 0 && t->saved_pending_top == 0 &&
           !t->pending_throw;
}

/* ================================================================
 * Hook lists (EXT:*SAVE-HOOKS* / EXT:*RESTORE-HOOKS*)
 * ================================================================ */

/* Funcall every designator on the list bound to SYM (most recent first,
 * i.e. list order), each in its own CL_CATCH so one erroring hook cannot
 * abort the rest.  Unlike exit hooks the list is NOT cleared: save hooks
 * serve every save of the session, restore hooks live in the image. */
static void image_run_hook_list(CL_Obj sym, const char *what)
{
    CL_Obj hooks, fn = CL_NIL;

    if (CL_NULL_P(sym)) return;
    hooks = cl_symbol_value(sym);
    if (hooks == CL_UNBOUND || !CL_CONS_P(hooks)) return;

    cl_vm.sp = 0;
    cl_vm.fp = 0;

    CL_GC_PROTECT(hooks);
    CL_GC_PROTECT(fn);
    while (CL_CONS_P(hooks)) {
        int err;
        CL_CATCH(err);
        if (err == CL_ERR_NONE) {
            fn = cl_coerce_funcdesig(cl_car(hooks), what);
            cl_vm_apply(fn, NULL, 0);
            CL_UNCATCH();
        } else if (err == CL_ERR_EXIT) {
            CL_UNCATCH();
            break;
        } else {
            char buf[256];
            CL_UNCATCH();
            snprintf(buf, sizeof(buf), "; Error in %s: %.180s\n",
                     what, cl_error_msg);
            platform_write_string(buf);
            cl_vm.sp = 0;
            cl_vm.fp = 0;
        }
        hooks = cl_cdr(hooks);
    }
    CL_GC_UNPROTECT(2);
}

void cl_image_run_restore_hooks(void)
{
    image_run_hook_list(SYM_RESTORE_HOOKS, "restore hook");
}

/* ================================================================
 * File writer
 * ================================================================ */

typedef struct {
    PlatformFile fh;
    int error;
} ImgWriter;

static void iw_bytes(ImgWriter *w, const void *data, uint32_t len)
{
    if (w->error || len == 0) return;
    if (platform_file_write_buf(w->fh, (const char *)data, len) < 0)
        w->error = 1;
}

static void iw_u8(ImgWriter *w, uint8_t v)  { iw_bytes(w, &v, 1); }
static void iw_u16(ImgWriter *w, uint16_t v) { iw_bytes(w, &v, 2); }
static void iw_u32(ImgWriter *w, uint32_t v) { iw_bytes(w, &v, 4); }

/* Count TYPE_BYTECODE objects in the (compacted, hole-free) arena. */
static uint32_t image_count_bytecodes(void)
{
    uint8_t *ptr = cl_heap.arena + CL_ALIGN;
    uint8_t *end = cl_heap.arena + cl_heap.bump;
    uint32_t n = 0;
    while (ptr < end) {
        uint32_t size = CL_HDR_SIZE(ptr);
        if (size == 0) break;
        if (CL_HDR_TYPE(ptr) == TYPE_BYTECODE) n++;
        ptr += size;
    }
    return n;
}

static void image_write_bytecode_blob(ImgWriter *w, uint32_t off,
                                      const CL_Bytecode *bc)
{
    uint32_t code_len = bc->code ? bc->code_len : 0;
    uint16_t n_const = bc->constants ? bc->n_constants : 0;
    uint8_t n_keys = (bc->key_syms && bc->key_slots) ? bc->n_keys : 0;
    uint16_t lm_count = bc->line_map ? bc->line_map_count : 0;
    uint16_t sf_len = bc->source_file
                      ? (uint16_t)strlen(bc->source_file) : 0;

    iw_u32(w, off);
    iw_u32(w, code_len);
    iw_bytes(w, bc->code, code_len);
    iw_u16(w, n_const);
    iw_bytes(w, bc->constants, (uint32_t)n_const * 4u);
    iw_u8(w, n_keys);
    iw_bytes(w, bc->key_syms, (uint32_t)n_keys * 4u);
    iw_bytes(w, bc->key_slots, n_keys);
    if (n_keys) {
        if (bc->key_suppliedp_slots) {
            iw_bytes(w, bc->key_suppliedp_slots, n_keys);
        } else {
            uint8_t none = 0xFF;
            uint8_t i;
            for (i = 0; i < n_keys; i++) iw_bytes(w, &none, 1);
        }
    }
    iw_u16(w, lm_count);
    iw_bytes(w, bc->line_map, (uint32_t)lm_count * (uint32_t)sizeof(CL_LineEntry));
    iw_u16(w, sf_len);
    iw_bytes(w, bc->source_file, sf_len);
}

static int image_write_file(const char *path)
{
    char tmp_path[1100];
    ImgWriter w;
    uint8_t fprint[CL_IMAGE_FPRINT_LEN];
    uint32_t n_blobs, n_roots, n_outbufs, h;
    int i;
    CL_Thread *mt = cl_main_thread_ptr;

    if (strlen(path) > sizeof(tmp_path) - 8) {
        platform_write_string("; SAVE-IMAGE: path too long\n");
        return -1;
    }
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    n_blobs = image_count_bytecodes();
    n_roots = (uint32_t)image_boot_roots + N_IMAGE_NAMED_ROOTS;
    n_outbufs = 0;
    for (h = cl_stream_outbuf_next_used(0); h != 0;
         h = cl_stream_outbuf_next_used(h))
        n_outbufs++;

    w.fh = platform_file_open(tmp_path, PLATFORM_FILE_WRITE);
    w.error = 0;
    if (w.fh == PLATFORM_FILE_INVALID) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "; SAVE-IMAGE: cannot create \"%.180s\"\n", tmp_path);
        platform_write_string(buf);
        return -1;
    }

    /* --- Header --- */
    image_fingerprint(fprint);
    iw_u32(&w, CL_IMAGE_MAGIC);
    iw_u16(&w, CL_IMAGE_VERSION);
    iw_u16(&w, image_flags());
    iw_bytes(&w, fprint, CL_IMAGE_FPRINT_LEN);
    iw_u32(&w, cl_heap.arena_size);
    iw_u32(&w, cl_heap.bump);
    iw_u32(&w, n_roots);
    iw_u32(&w, n_blobs);
    iw_u32(&w, (uint32_t)image_boot_roots);

    /* --- Section 1: ROOTS --- */
    for (i = 0; i < image_boot_roots; i++) {
        uint32_t v = cl_gc_global_root_get(i);
        iw_bytes(&w, &v, 4);
    }
    for (i = 0; i < N_IMAGE_NAMED_ROOTS; i++) {
        uint32_t v = *image_named_root(i);
        iw_bytes(&w, &v, 4);
    }

    /* --- Section 2: READTABLES --- */
    iw_u32(&w, cl_readtable_alloc_mask);
    iw_u32(&w, (uint32_t)sizeof(cl_readtable_pool));
    iw_bytes(&w, cl_readtable_pool, (uint32_t)sizeof(cl_readtable_pool));

    /* --- Section 3: THREAD0 --- */
    {
        uint32_t v = mt->current_package;
        uint32_t wobj = cl_main_thread_lisp_obj();
        uint32_t n_tlv = 0;
        int ti;
        iw_bytes(&w, &v, 4);
        iw_bytes(&w, &wobj, 4);
        for (ti = 0; ti < CL_TLV_TABLE_SIZE; ti++) {
            CL_Obj s = mt->tlv_table[ti].symbol;
            if (s != CL_NIL && s != CL_UNBOUND) n_tlv++;
        }
        iw_u32(&w, n_tlv);
        for (ti = 0; ti < CL_TLV_TABLE_SIZE; ti++) {
            CL_Obj s = mt->tlv_table[ti].symbol;
            if (s != CL_NIL && s != CL_UNBOUND) {
                uint32_t sv = s, vv = mt->tlv_table[ti].value;
                iw_bytes(&w, &sv, 4);
                iw_bytes(&w, &vv, 4);
            }
        }
    }

    /* --- Section 4: BLOBS (bytecode side buffers, then outbufs) --- */
    {
        uint8_t *ptr = cl_heap.arena + CL_ALIGN;
        uint8_t *end = cl_heap.arena + cl_heap.bump;
        while (ptr < end) {
            uint32_t size = CL_HDR_SIZE(ptr);
            if (size == 0) break;
            if (CL_HDR_TYPE(ptr) == TYPE_BYTECODE)
                image_write_bytecode_blob(&w,
                    (uint32_t)(ptr - cl_heap.arena), (CL_Bytecode *)ptr);
            ptr += size;
        }
    }
    iw_u32(&w, n_outbufs);
    for (h = cl_stream_outbuf_next_used(0); h != 0;
         h = cl_stream_outbuf_next_used(h)) {
        uint32_t len = cl_stream_outbuf_len(h);
        iw_u32(&w, h);
        iw_u32(&w, len);
        iw_bytes(&w, cl_stream_outbuf_data(h), len);
    }

    /* --- Section 5: ARENA --- */
    iw_bytes(&w, cl_heap.arena, cl_heap.bump);

    if (platform_file_flush(w.fh) < 0) w.error = 1;
    platform_file_close(w.fh);

    if (w.error) {
        platform_file_delete(tmp_path);
        platform_write_string("; SAVE-IMAGE: write failed (disk full?) - "
                              "image not saved, session unchanged\n");
        return -1;
    }

    /* Delete-before-rename: an in-place overwrite keeps the old datestamp
     * under FS-UAE's .uaem metadata handling (documented stale-FASL trap),
     * and AmigaDOS Rename() refuses to replace an existing object. */
    platform_file_delete(path);
    if (platform_file_rename(tmp_path, path) != 0) {
        platform_file_delete(tmp_path);
        platform_write_string("; SAVE-IMAGE: could not rename temp file "
                              "into place - image not saved\n");
        return -1;
    }
    return 0;
}

/* ================================================================
 * Save request + deferred execution
 * ================================================================ */

static int image_have_save_hooks(void)
{
    CL_Obj hooks;
    if (CL_NULL_P(SYM_SAVE_HOOKS)) return 0;
    hooks = cl_symbol_value(SYM_SAVE_HOOKS);
    return hooks != CL_UNBOUND && CL_CONS_P(hooks);
}

void cl_image_save_request(const char *path, int quit, int shake)
{
    if (!path || !path[0])
        cl_error(CL_ERR_FILE, "SAVE-IMAGE: empty pathname");
    if (strlen(path) >= sizeof(image_pending_path))
        cl_error(CL_ERR_FILE, "SAVE-IMAGE: pathname too long");

    /* With no save hooks registered, check the preconditions NOW so the
     * caller gets a synchronous, catchable error.  With hooks the check
     * must wait for the dump point: hooks exist precisely to close the
     * streams / tear down the FFI state that would otherwise abort the
     * save, and they only run once the main thread is at rest. */
    if (!image_have_save_hooks()) {
        const char *pre = image_save_preconditions();
        if (pre)
            cl_error(CL_ERR_GENERAL, "%s", pre);
    }

    strcpy(image_pending_path, path);
    image_pending_quit = quit;
    image_pending_shake = shake;
    image_pending = 1;
}

int cl_image_save_run_if_pending(void)
{
    const char *pre;
    int quit, shake;

    if (!image_pending)
        return 0;
    if (!image_main_thread_at_rest())
        return 0;   /* stays pending until an outer safe point is at rest */

    image_pending = 0;
    quit = image_pending_quit;
    shake = image_pending_shake;
    image_pending_quit = 0;
    image_pending_shake = 0;

    /* 1. Save hooks first — a hook may close the very streams that would
     * otherwise abort the save. */
    image_run_hook_list(SYM_SAVE_HOOKS, "save hook");

    /* 2. Re-verify (hooks ran user code) — including at-rest, which a
     * misbehaving hook could theoretically have broken. */
    pre = image_save_preconditions();
    if (!pre && !image_main_thread_at_rest())
        pre = "SAVE-IMAGE: a save hook left transient state behind - "
              "image not saved";
    if (pre) {
        char buf[600];
        snprintf(buf, sizeof(buf), "; %.560s\n", pre);
        platform_write_string(buf);
        return 0;
    }

    /* Late-registered roots (index >= boot_roots) are deliberately dropped
     * from the image: in the restoring process the corresponding static is
     * still NIL/unregistered and its lazy-init pattern re-derives the value
     * (cached interned symbols).  That is safe ONLY for re-derivable
     * caches, so warn whenever any exist — a STATEFUL late root is a bug
     * (register it during init instead; see specs/image-save-load.md). */
    if (cl_gc_global_root_count() > image_boot_roots) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "; SAVE-IMAGE: note: %d GC root(s) were registered after "
                 "boot and are not saved (re-derived lazily at restore)\n",
                 cl_gc_global_root_count() - image_boot_roots);
        platform_write_string(buf);
#ifdef DEBUG_GC
        {
            int li;
            for (li = image_boot_roots; li < cl_gc_global_root_count(); li++) {
                snprintf(buf, sizeof(buf), ";   late root #%d: %s:%d\n", li,
                         cl_gc_global_root_file(li) ? cl_gc_global_root_file(li)
                                                    : "?",
                         cl_gc_global_root_line(li));
                platform_write_string(buf);
            }
        }
#endif
    }

    /* 2b. :SHAKE-BINDINGS — shed the demand-interned binding tables
     * (specs/raw-bindings-footprint.md, Phase 3).  After the save hooks, so
     * a hook that REQUIREd another raw module is covered too, and before
     * the dump GC below, which is what actually reclaims the blobs.
     *
     * This closes those packages permanently — in the image and in this
     * session (a failed write afterwards does not put them back; re-loading
     * the module's FASL does).  Say so: silently losing names would be the
     * worst possible failure mode to debug from a restored image. */
    if (shake) {
        uint32_t bytes = 0;
        uint32_t count = cl_bindtab_shed_all(&bytes);
        if (count) {
            char buf[320];
            snprintf(buf, sizeof(buf),
                     "; SAVE-IMAGE: shed %u binding table%s (%u KB) - the image "
                     "and this session now resolve only the names already "
                     "referenced in those packages\n",
                     (unsigned)count, count == 1 ? "" : "s",
                     (unsigned)((bytes + 1023) >> 10));
            platform_write_string(buf);
        }
    }

    /* 3. Dump the minimal, hole-free heap: full GC, then compaction
     * (guarantees free_list == 0 and a dense [CL_ALIGN, bump) payload). */
    cl_gc();
    cl_gc_compact();

    /* 4. Offset-keyed reader diagnostics are stale by definition in the
     * next process (also cleared at restore — belt and braces). */
    {
        int i;
        for (i = 0; i < CL_SRCLOC_SIZE; i++)
            cl_srcloc_table[i].cons_obj = CL_NIL;
    }

    /* 5. Write temp + rename. */
    if (image_write_file(image_pending_path) == 0) {
        char buf[400];
        snprintf(buf, sizeof(buf),
                 "; Image saved to \"%.300s\" (%u KB heap)\n",
                 image_pending_path,
                 (unsigned)(cl_heap.bump >> 10));
        platform_write_string(buf);
        return quit;
    }
    return 0;
}

/* ================================================================
 * Restore: staging (before cl_mem_init)
 * ================================================================ */

typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t pos;
    int error;
} ImgReader;

static int ir_need(ImgReader *r, uint32_t n)
{
    if (r->error || r->size - r->pos < n || r->pos + n < r->pos) {
        r->error = 1;
        return 0;
    }
    return 1;
}

static void ir_bytes(ImgReader *r, void *out, uint32_t n)
{
    if (!ir_need(r, n)) { if (out) memset(out, 0, n); return; }
    if (out) memcpy(out, r->data + r->pos, n);
    r->pos += n;
}

static uint8_t  ir_u8(ImgReader *r)  { uint8_t v = 0;  ir_bytes(r, &v, 1); return v; }
static uint16_t ir_u16(ImgReader *r) { uint16_t v = 0; ir_bytes(r, &v, 2); return v; }
static uint32_t ir_u32(ImgReader *r) { uint32_t v = 0; ir_bytes(r, &v, 4); return v; }

static const uint8_t *ir_ptr(ImgReader *r, uint32_t n)
{
    const uint8_t *p;
    if (!ir_need(r, n)) return NULL;
    p = r->data + r->pos;
    r->pos += n;
    return p;
}

static int image_parse_header(ImgReader *r, CL_ImageHeader *h)
{
    h->magic = ir_u32(r);
    h->version = ir_u16(r);
    h->flags = ir_u16(r);
    ir_bytes(r, h->fingerprint, CL_IMAGE_FPRINT_LEN);
    h->arena_size = ir_u32(r);
    h->bump = ir_u32(r);
    h->n_roots = ir_u32(r);
    h->n_blobs = ir_u32(r);
    h->boot_roots = ir_u32(r);
    return !r->error;
}

static void image_stage_fail(int quiet, const char *fmt, const char *arg)
{
    if (!quiet) {
        char buf[512];
        snprintf(buf, sizeof(buf), fmt, arg ? arg : "");
        platform_write_string(buf);
    }
}

int cl_image_stage(const char *path, int quiet)
{
    ImgReader r;
    uint8_t fprint[CL_IMAGE_FPRINT_LEN];

    cl_image_discard_staged();

    image_staged_buf = platform_file_read(path, &image_staged_size);
    if (!image_staged_buf) {
        image_stage_fail(quiet, "; --image: cannot read \"%.300s\"\n", path);
        return -1;
    }

    r.data = (const uint8_t *)image_staged_buf;
    r.size = (uint32_t)image_staged_size;
    r.pos = 0;
    r.error = 0;

    if (image_staged_size < CL_IMAGE_HEADER_BYTES ||
        !image_parse_header(&r, &image_staged_hdr) ||
        image_staged_hdr.magic != CL_IMAGE_MAGIC) {
        image_stage_fail(quiet,
            "; --image: \"%.300s\" is not a clamiga heap image (or was "
            "written with a different byte order)\n", path);
        cl_image_discard_staged();
        return -1;
    }
    if (image_staged_hdr.version != CL_IMAGE_VERSION) {
        image_stage_fail(quiet,
            "; --image: \"%.300s\" uses a different image format version - "
            "it was written by a different build of clamiga; delete it or "
            "rebuild it with EXT:SAVE-IMAGE\n", path);
        cl_image_discard_staged();
        return -1;
    }
    image_fingerprint(fprint);
    if (memcmp(fprint, image_staged_hdr.fingerprint,
               CL_IMAGE_FPRINT_LEN) != 0) {
        image_stage_fail(quiet,
            "; --image: \"%.300s\" was written by a different build of "
            "clamiga (version/feature/layout fingerprint mismatch) - "
            "delete it or rebuild it with EXT:SAVE-IMAGE\n", path);
        cl_image_discard_staged();
        return -1;
    }
    if (image_staged_hdr.bump < CL_ALIGN ||
        (image_staged_hdr.bump & (CL_ALIGN - 1)) != 0 ||
        image_staged_hdr.n_roots !=
            image_staged_hdr.boot_roots + N_IMAGE_NAMED_ROOTS) {
        image_stage_fail(quiet,
            "; --image: \"%.300s\" is structurally corrupt\n", path);
        cl_image_discard_staged();
        return -1;
    }
    return 0;
}

uint32_t cl_image_staged_bump(void)
{
    return image_staged_buf ? image_staged_hdr.bump : 0;
}

int cl_image_staged_p(void)
{
    return image_staged_buf != NULL;
}

void cl_image_discard_staged(void)
{
    if (image_staged_buf) {
        platform_free(image_staged_buf);
        image_staged_buf = NULL;
        image_staged_size = 0;
    }
}

/* ================================================================
 * Restore: the relink walk
 * ================================================================ */

/* Fatal-but-clean abort once the arena has been overwritten: there is no
 * heap to fall back to — print exactly what went wrong and exit. */
static void image_fatal(const char *fmt, unsigned a, unsigned b)
{
    char buf[512];
    platform_write_string(
        "FATAL: --image restore failed after the heap was replaced "
        "(corrupt image): ");
    snprintf(buf, sizeof(buf), fmt, a, b);
    platform_write_string(buf);
    platform_write_string("\nDelete the image file and boot normally.\n");
    fflush(NULL);
#if defined(PLATFORM_AMIGA) && !defined(PLATFORM_MORPHOS)
    /* m68k AmigaOS: exit()'s post-main stdio teardown hangs the Shell —
     * same reason main.c ends with _exit() there. */
    _exit(1);
#else
    /* MorphOS (and host): route through cl_fatal_exit() so
     * cl_thread_restore_main_tls() runs first — cl_thread_init() has
     * already overwritten tc_UserData by the time this path can be
     * reached, and a plain exit() here would hit the same -noixemul
     * crt0 post-main-teardown freeze cl_thread_restore_main_tls()
     * exists to prevent. */
    cl_fatal_exit(1);
#endif
}

/* Relink one restored CL_Function through the builtin registry.  Returns
 * 0 on success, -1 when the stale stub had to be installed. */
static int image_relink_function(CL_Function *fn, char *namebuf,
                                 uint32_t namebuf_len)
{
    CL_Symbol *sym;
    CL_Package *pkg;
    CL_String *sname, *pname;
    CL_CFunc cf;

    namebuf[0] = '\0';
    if (!CL_SYMBOL_P(fn->name)) goto stale;
    sym = (CL_Symbol *)CL_OBJ_TO_PTR(fn->name);
    if (!CL_STRING_P(sym->name)) goto stale;
    sname = (CL_String *)CL_OBJ_TO_PTR(sym->name);
    snprintf(namebuf, namebuf_len, "%.*s",
             (int)(sname->length > 200 ? 200 : sname->length), sname->data);
    if (!CL_PACKAGE_P(sym->package)) goto stale;
    pkg = (CL_Package *)CL_OBJ_TO_PTR(sym->package);
    if (!CL_STRING_P(pkg->name)) goto stale;
    pname = (CL_String *)CL_OBJ_TO_PTR(pkg->name);

    cf = cl_builtin_registry_lookup(pname->data, pname->length,
                                    sname->data, sname->length);
    if (!cf) goto stale;
    fn->func = cf;
    return 0;

stale:
    fn->func = bi_stale_builtin;
    return -1;
}

int cl_image_restore_staged(void)
{
    ImgReader r;
    CL_ImageHeader *h = &image_staged_hdr;
    const uint8_t *roots_sec, *arena_sec;
    uint32_t rt_mask, rt_bytes;
    const uint8_t *rt_pool;
    CL_Obj th_package, th_wrapper;
    uint32_t th_ntlv;
    const uint8_t *tlv_sec;
    uint32_t blob_sec_pos, outbuf_sec_pos;
    uint32_t i;
    CL_Thread *mt = cl_main_thread_ptr;
    int stale_count = 0;
    char stale_first[256];

    if (!image_staged_buf) return -1;

    /* ---- Pre-arena verification: everything checked BEFORE any byte of
     * the current heap is touched, so the caller can still fall back. ---- */

    if ((uint32_t)cl_gc_global_root_count() != h->boot_roots) {
        char buf[300];
        snprintf(buf, sizeof(buf),
                 "; --image: boot-root count mismatch (image %u, this "
                 "process %d) - the image was written by a different build "
                 "of clamiga; delete it or rebuild it with EXT:SAVE-IMAGE\n",
                 (unsigned)h->boot_roots, cl_gc_global_root_count());
        platform_write_string(buf);
        return -1;
    }
    if (h->bump > cl_heap.arena_size) {
        char buf[300];
        snprintf(buf, sizeof(buf),
                 "; --image: image heap (%u KB) exceeds the arena (%u KB) - "
                 "start clamiga with a larger --heap\n",
                 (unsigned)(h->bump >> 10),
                 (unsigned)(cl_heap.arena_size >> 10));
        platform_write_string(buf);
        return -1;
    }

    r.data = (const uint8_t *)image_staged_buf;
    r.size = (uint32_t)image_staged_size;
    r.pos = CL_IMAGE_HEADER_BYTES;
    r.error = 0;

    /* Section 1: ROOTS */
    roots_sec = ir_ptr(&r, h->n_roots * 4u);

    /* Section 2: READTABLES */
    rt_mask = ir_u32(&r);
    rt_bytes = ir_u32(&r);
    rt_pool = ir_ptr(&r, rt_bytes);
    if (r.error || rt_bytes != (uint32_t)sizeof(cl_readtable_pool)) {
        platform_write_string(
            "; --image: readtable section mismatch - the image was written "
            "by a different build of clamiga\n");
        return -1;
    }

    /* Section 3: THREAD0 */
    ir_bytes(&r, &th_package, 4);
    ir_bytes(&r, &th_wrapper, 4);
    th_ntlv = ir_u32(&r);
    if (th_ntlv > CL_TLV_TABLE_SIZE) {
        platform_write_string("; --image: corrupt THREAD0 section\n");
        return -1;
    }
    tlv_sec = ir_ptr(&r, th_ntlv * 8u);

    /* Section 4: BLOBS — structural scan (bounds only; attach comes after
     * the arena is in place). */
    blob_sec_pos = r.pos;
    for (i = 0; i < h->n_blobs && !r.error; i++) {
        uint32_t code_len, sf_len;
        uint16_t n_const, lm_count;
        uint8_t n_keys;
        (void)ir_u32(&r);               /* bc_offset */
        code_len = ir_u32(&r);
        (void)ir_ptr(&r, code_len);
        n_const = ir_u16(&r);
        (void)ir_ptr(&r, (uint32_t)n_const * 4u);
        n_keys = ir_u8(&r);
        (void)ir_ptr(&r, (uint32_t)n_keys * 6u);
        lm_count = ir_u16(&r);
        (void)ir_ptr(&r, (uint32_t)lm_count * (uint32_t)sizeof(CL_LineEntry));
        sf_len = ir_u16(&r);
        (void)ir_ptr(&r, sf_len);
    }
    outbuf_sec_pos = r.pos;
    {
        uint32_t n_outbufs = ir_u32(&r);
        for (i = 0; i < n_outbufs && !r.error; i++) {
            uint32_t len;
            (void)ir_u32(&r);           /* handle */
            len = ir_u32(&r);
            (void)ir_ptr(&r, len);
        }
    }

    /* Section 5: ARENA — must be exactly the trailing h->bump bytes. */
    arena_sec = ir_ptr(&r, h->bump);
    if (r.error || !roots_sec || !arena_sec || r.pos != r.size) {
        platform_write_string(
            "; --image: file is truncated or structurally corrupt - "
            "delete it or rebuild it with EXT:SAVE-IMAGE\n");
        return -1;
    }

    /* ---- Point of no return: replace the heap. ---- */

    memcpy(cl_heap.arena, arena_sec, h->bump);
    cl_mem_adopt_image_begin(h->bump);

    /* The virgin boot's per-thread Lisp references (mv_values, reader/
     * printer state, ...) are stale offsets into the heap just replaced;
     * gc_mark_thread_roots walks them unconditionally. */
    cl_thread_reset_lisp_state(mt);

    /* Roots: registered globals by index, then the named shared tables. */
    for (i = 0; i < h->boot_roots; i++) {
        CL_Obj v;
        memcpy(&v, roots_sec + i * 4u, 4);
        cl_gc_global_root_set((int)i, v);
    }
    for (i = 0; i < N_IMAGE_NAMED_ROOTS; i++) {
        CL_Obj v;
        memcpy(&v, roots_sec + (h->boot_roots + i) * 4u, 4);
        *image_named_root((int)i) = v;
    }

    /* Readtable pool: syntax bytes, case modes and the CL_Obj macro/
     * dispatch arrays (arena offsets — valid as-is). */
    memcpy(cl_readtable_pool, rt_pool, sizeof(cl_readtable_pool));
    cl_readtable_alloc_mask = rt_mask;

    /* THREAD0: main thread's persistent slots. */
    mt->current_package = th_package;
    for (i = 0; i < th_ntlv; i++) {
        CL_Obj s, v;
        memcpy(&s, tlv_sec + i * 8u, 4);
        memcpy(&v, tlv_sec + i * 8u + 4, 4);
        cl_tlv_set(mt, s, v);
    }
    *cl_main_thread_lisp_obj_ptr() = th_wrapper;

    /* Outbufs: re-materialize BEFORE the walk so stream relinking can
     * verify each live string-output-stream's handle resolves. */
    {
        ImgReader ob;
        uint32_t n_outbufs;
        ob.data = r.data;
        ob.size = r.size;
        ob.pos = outbuf_sec_pos;
        ob.error = 0;
        n_outbufs = ir_u32(&ob);
        for (i = 0; i < n_outbufs; i++) {
            uint32_t hd = ir_u32(&ob);
            uint32_t len = ir_u32(&ob);
            const uint8_t *data = ir_ptr(&ob, len);
            if (ob.error ||
                cl_stream_outbuf_install_at(hd, (const char *)data, len) != 0)
                image_fatal("outbuf slot %u could not be re-materialized "
                            "(len %u)", (unsigned)hd, (unsigned)len);
        }
    }

    /* ---- The relink walk: one linear pass, same shape as sweep.  Also
     * the integrity check — any malformed header aborts cleanly. ---- */
    {
        ImgReader br;
        uint32_t blob_idx = 0;
        uint8_t *ptr = cl_heap.arena + CL_ALIGN;
        uint8_t *end = cl_heap.arena + h->bump;
        char namebuf[256];

        br.data = r.data;
        br.size = r.size;
        br.pos = blob_sec_pos;
        br.error = 0;
        stale_first[0] = '\0';

        while (ptr < end) {
            uint32_t off = (uint32_t)(ptr - cl_heap.arena);
            uint32_t size = CL_HDR_SIZE(ptr);
            uint8_t type = (uint8_t)CL_HDR_TYPE(ptr);

            if (size == 0 || (size & (CL_ALIGN - 1)) != 0 ||
                off + size > h->bump || type > CL_TYPE_MAX)
                image_fatal("bad object header 0x%08x at offset 0x%08x",
                            (unsigned)CL_HDR(ptr), (unsigned)off);

            switch (type) {
            case TYPE_FUNCTION:
                if (image_relink_function((CL_Function *)ptr, namebuf,
                                          sizeof(namebuf)) != 0) {
                    stale_count++;
                    if (stale_first[0] == '\0')
                        snprintf(stale_first, sizeof(stale_first), "%.200s",
                                 namebuf[0] ? namebuf : "<anonymous>");
                }
                break;

            case TYPE_BYTECODE: {
                CL_Bytecode *bc = (CL_Bytecode *)ptr;
                uint32_t b_off, code_len, sf_len;
                uint16_t n_const, lm_count;
                uint8_t n_keys;
                const uint8_t *p;

                if (blob_idx >= h->n_blobs)
                    image_fatal("bytecode at offset 0x%08x has no side-"
                                "buffer blob (%u blobs consumed)",
                                (unsigned)off, (unsigned)blob_idx);
                b_off = ir_u32(&br);
                if (b_off != off)
                    image_fatal("blob/bytecode offset mismatch: blob "
                                "0x%08x vs object 0x%08x",
                                (unsigned)b_off, (unsigned)off);
                blob_idx++;

                code_len = ir_u32(&br);
                p = ir_ptr(&br, code_len);
                if (code_len) {
                    bc->code = (uint8_t *)platform_alloc(code_len);
                    if (!bc->code)
                        image_fatal("out of memory attaching %u bytecode "
                                    "bytes at 0x%08x", (unsigned)code_len,
                                    (unsigned)off);
                    memcpy(bc->code, p, code_len);
                } else {
                    bc->code = NULL;
                }
                bc->code_len = code_len;

                n_const = ir_u16(&br);
                p = ir_ptr(&br, (uint32_t)n_const * 4u);
                if (n_const) {
                    bc->constants =
                        (CL_Obj *)platform_alloc((uint32_t)n_const * 4u);
                    if (!bc->constants)
                        image_fatal("out of memory attaching %u constants "
                                    "at 0x%08x", (unsigned)n_const,
                                    (unsigned)off);
                    memcpy(bc->constants, p, (uint32_t)n_const * 4u);
                } else {
                    bc->constants = NULL;
                }
                bc->n_constants = n_const;

                n_keys = ir_u8(&br);
                p = ir_ptr(&br, (uint32_t)n_keys * 6u);
                if (n_keys) {
                    bc->key_syms =
                        (CL_Obj *)platform_alloc((uint32_t)n_keys * 4u);
                    bc->key_slots = (uint8_t *)platform_alloc(n_keys);
                    bc->key_suppliedp_slots =
                        (uint8_t *)platform_alloc(n_keys);
                    if (!bc->key_syms || !bc->key_slots ||
                        !bc->key_suppliedp_slots)
                        image_fatal("out of memory attaching %u key params "
                                    "at 0x%08x", (unsigned)n_keys,
                                    (unsigned)off);
                    memcpy(bc->key_syms, p, (uint32_t)n_keys * 4u);
                    memcpy(bc->key_slots, p + (uint32_t)n_keys * 4u, n_keys);
                    memcpy(bc->key_suppliedp_slots,
                           p + (uint32_t)n_keys * 5u, n_keys);
                } else {
                    bc->key_syms = NULL;
                    bc->key_slots = NULL;
                    bc->key_suppliedp_slots = NULL;
                }
                bc->n_keys = n_keys;

                lm_count = ir_u16(&br);
                p = ir_ptr(&br, (uint32_t)lm_count *
                                 (uint32_t)sizeof(CL_LineEntry));
                if (lm_count) {
                    bc->line_map = (CL_LineEntry *)platform_alloc(
                        (uint32_t)lm_count * (uint32_t)sizeof(CL_LineEntry));
                    if (!bc->line_map)
                        image_fatal("out of memory attaching line map at "
                                    "0x%08x", (unsigned)off, 0);
                    memcpy(bc->line_map, p,
                           (uint32_t)lm_count * (uint32_t)sizeof(CL_LineEntry));
                } else {
                    bc->line_map = NULL;
                }
                bc->line_map_count = lm_count;

                sf_len = ir_u16(&br);
                p = ir_ptr(&br, sf_len);
                if (sf_len && sf_len < sizeof(namebuf)) {
                    memcpy(namebuf, p, sf_len);
                    namebuf[sf_len] = '\0';
                    bc->source_file = cl_intern_source_file(namebuf);
                } else {
                    bc->source_file = NULL;
                }

                /* The JIT recompiles lazily, exactly as after a FASL load. */
                bc->native_code = NULL;
                bc->native_len = 0;
                bc->native_relocs = NULL;
                bc->native_reloc_count = 0;

                if (br.error)
                    image_fatal("blob section truncated at bytecode "
                                "0x%08x", (unsigned)off, 0);
                break;
            }

            case TYPE_STREAM: {
                CL_Stream *st = (CL_Stream *)ptr;
                switch (st->stream_type) {
                case CL_STREAM_CONSOLE:
                case CL_STREAM_SYNONYM:
                case CL_STREAM_TWO_WAY:
                case CL_STREAM_BROADCAST:
                case CL_STREAM_CONCATENATED:
                    break;   /* no OS handle involved / children are arena refs */
                case CL_STREAM_STRING:
                    if (st->out_buf_handle != 0 &&
                        !cl_stream_outbuf_data(st->out_buf_handle)) {
                        if (st->flags & CL_STREAM_FLAG_OPEN)
                            image_fatal("open string stream at 0x%08x "
                                        "references missing outbuf %u",
                                        (unsigned)off,
                                        (unsigned)st->out_buf_handle);
                        st->out_buf_handle = 0;   /* closed: drop stale handle */
                    }
                    break;
                case CL_STREAM_FILE:
                case CL_STREAM_SOCKET:
                    /* Cannot exist (save precondition).  Defensive: dead. */
                    st->flags &= ~(uint32_t)CL_STREAM_FLAG_OPEN;
                    st->flags |= CL_STREAM_FLAG_EOF;
                    st->handle_id = 0;
                    break;
                case CL_STREAM_CBUF:
                    /* Load-time C-buffer stream: the buffer died with the
                     * saving process. */
                    st->flags &= ~(uint32_t)CL_STREAM_FLAG_OPEN;
                    st->flags |= CL_STREAM_FLAG_EOF;
                    st->handle_id = 0;
                    break;
                default:
                    image_fatal("stream at 0x%08x has unknown stream_type "
                                "%u", (unsigned)off,
                                (unsigned)st->stream_type);
                }
                break;
            }

            case TYPE_LOCK: {
                CL_Lock *lk = (CL_Lock *)ptr;
                if (cl_lock_table_install_at(lk->lock_id,
                        (lk->flags & CL_LOCK_FLAG_RECURSIVE) != 0) != 0)
                    image_fatal("could not recreate lock id %u (restored "
                                "MP:LOCK)", (unsigned)lk->lock_id, 0);
                break;
            }

            case TYPE_CONDVAR: {
                CL_CondVar *cv = (CL_CondVar *)ptr;
                if (cl_condvar_table_install_at(cv->condvar_id) != 0)
                    image_fatal("could not recreate condition variable id "
                                "%u", (unsigned)cv->condvar_id, 0);
                break;
            }

            case TYPE_THREAD: {
                CL_ThreadObj *to = (CL_ThreadObj *)ptr;
                if (CL_PTR_TO_OBJ(ptr) == th_wrapper) {
                    /* The saved main-thread wrapper: re-bind to the live
                     * main thread. */
                    to->thread_id = 0;
                    to->table_gen = cl_thread_table_gen[0];
                } else {
                    /* Any other wrapper's thread died with the saving
                     * process: force the table_gen mismatch so JOIN/
                     * INTERRUPT report "thread no longer exists" — the
                     * existing stale-wrapper semantics. */
                    uint32_t id = to->thread_id;
                    if (id >= CL_MAX_THREADS) id = 0;
                    to->thread_id = id;
                    to->table_gen = cl_thread_table_gen[id] - 1u;
                }
                break;
            }

            case TYPE_FOREIGN_POINTER: {
                /* Raw machine addresses / dlopen handles from the saving
                 * process are meaningless here.  FFI deref/call paths
                 * reject a null address with a clean error; rebuild such
                 * state via EXT:*RESTORE-HOOKS*. */
                CL_ForeignPtr *fp = (CL_ForeignPtr *)ptr;
                fp->address = 0;
                fp->size = 0;
                fp->flags = 0;
                break;
            }

            default:
                /* Fully arena-contained — nothing to do. */
                break;
            }
            ptr += size;
        }

        if (blob_idx != h->n_blobs)
            image_fatal("blob count mismatch: %u blobs in file, %u "
                        "bytecodes in heap", (unsigned)h->n_blobs,
                        (unsigned)blob_idx);
    }

    if (stale_count > 0) {
        char buf[400];
        snprintf(buf, sizeof(buf),
                 "; Warning: %d builtin function(s) in the image have no "
                 "implementation in this build (first: %.200s) - calling "
                 "one will signal an error.  Rebuild the image with "
                 "EXT:SAVE-IMAGE.\n", stale_count, stale_first);
        platform_write_string(buf);
    }

    /* Re-sync the C-level main-thread record with the restored wrapper. */
    if (CL_HEAP_P(th_wrapper) &&
        CL_HDR_TYPE(CL_OBJ_TO_PTR(th_wrapper)) == TYPE_THREAD) {
        CL_ThreadObj *to = (CL_ThreadObj *)CL_OBJ_TO_PTR(th_wrapper);
        mt->name = to->name;
    }

    /* Offset-keyed reader diagnostics: stale by definition. */
    for (i = 0; i < CL_SRCLOC_SIZE; i++)
        cl_srcloc_table[i].cons_obj = CL_NIL;

    /* Normalize collector state for the adopted payload (mark bits,
     * GenGC watermark/crossing map/protection). */
    cl_mem_adopt_image_finish();

    /* EXT:*IMAGE-RESTORED-P* — set before .clamigarc runs so an rc file
     * can skip redundant loads. */
    if (CL_SYMBOL_P(SYM_IMAGE_RESTORED_P))
        ((CL_Symbol *)CL_OBJ_TO_PTR(SYM_IMAGE_RESTORED_P))->value = SYM_T;

    image_restored = 1;
    cl_image_discard_staged();
    return 0;
}

/* ================================================================
 * Lisp surface: EXT:SAVE-IMAGE + hook variables
 * ================================================================ */

/* Coerce a pathname designator (string / wide string / pathname) to a C
 * path in BUF.  Returns BUF or NULL. */
static const char *image_coerce_path(CL_Obj obj, char *buf, uint32_t buflen)
{
    if (CL_PATHNAME_P(obj)) {
        extern const char *cl_coerce_to_namestring(CL_Obj, char *, uint32_t);
        cl_coerce_to_namestring(obj, buf, buflen);
        return buf;
    }
#ifdef CL_WIDE_STRINGS
    if (CL_WIDE_STRING_P(obj)) {
        uint32_t nb = cl_wide_string_to_utf8(obj, buf, buflen - 1);
        buf[nb] = '\0';
        return buf;
    }
#endif
    if (CL_STRING_P(obj)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(obj);
        if (s->length >= buflen) return NULL;
        memcpy(buf, s->data, s->length);
        buf[s->length] = '\0';
        return buf;
    }
    return NULL;
}

static CL_Obj bi_save_image(CL_Obj *args, int nargs)
{
    char path[1024];
    int quit = 0, shake = 0;
    int i;

    if (!image_coerce_path(args[0], path, sizeof(path)))
        cl_error(CL_ERR_TYPE,
                 "SAVE-IMAGE: pathname designator expected (string or "
                 "pathname)");

    for (i = 1; i + 1 < nargs; i += 2) {
        if (args[i] == KW_QUIT_IMG)
            quit = !CL_NULL_P(args[i + 1]);
        else if (args[i] == KW_SHAKE_BINDINGS)
            shake = !CL_NULL_P(args[i + 1]);
        else
            cl_error(CL_ERR_ARGS,
                     "SAVE-IMAGE: unknown keyword argument (only :QUIT and "
                     ":SHAKE-BINDINGS are accepted)");
    }
    if (i != nargs)
        cl_error(CL_ERR_ARGS, "SAVE-IMAGE: odd number of keyword arguments");

    cl_image_save_request(path, quit, shake);

    /* The dump itself runs after this top-level form finishes, at the
     * next safe point where the main thread is at rest (spec).  Return
     * the pathname so scripts can report it. */
    return cl_make_string(path, (uint32_t)strlen(path));
}

/* Intern an EXT special variable with initial value NIL, export it, and
 * root the C handle. */
static CL_Obj image_defvar_ext(const char *name, CL_Obj *slot)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_ext);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->flags |= CL_SYM_SPECIAL;
    s->value = CL_NIL;
    *slot = sym;
    cl_gc_register_root(slot);
    cl_export_symbol(sym, cl_package_ext);
    return sym;
}

void cl_image_builtins_init(void)
{
    image_defvar_ext("*SAVE-HOOKS*", &SYM_SAVE_HOOKS);
    image_defvar_ext("*RESTORE-HOOKS*", &SYM_RESTORE_HOOKS);
    image_defvar_ext("*IMAGE-RESTORED-P*", &SYM_IMAGE_RESTORED_P);

    KW_QUIT_IMG = cl_intern_keyword("QUIT", 4);
    cl_gc_register_root(&KW_QUIT_IMG);
    KW_SHAKE_BINDINGS = cl_intern_keyword("SHAKE-BINDINGS", 14);
    cl_gc_register_root(&KW_SHAKE_BINDINGS);

    cl_register_builtin_exported("SAVE-IMAGE", bi_save_image, 1, 5,
                                 cl_package_ext);
}
