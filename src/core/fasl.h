#ifndef CL_FASL_H
#define CL_FASL_H

/*
 * FASL (Fast Loading) — binary serialization of compiled bytecode.
 *
 * File format: big-endian binary (native for 68020).
 *
 * Header:
 *   magic:   4 bytes  "CLFA"
 *   version: 2 bytes  format version
 *   flags:   2 bytes  reserved (0)
 *   n_units: 4 bytes  number of top-level compilation units
 *
 * Per unit:
 *   bc_len:  4 bytes  length of serialized bytecode blob
 *   <serialized bytecode blob>
 *
 * Bytecode blob contains: opcodes, constant pool, arity, locals,
 * upvalues, optional/key info, source location, line map, name.
 *
 * Constants are type-tagged and serialized recursively.
 */

#include "types.h"
#include "../platform/platform.h"

#define CL_FASL_MAGIC    0x434C4641  /* "CLFA" */
#define CL_FASL_VERSION  22  /* v22: (defvar name) with no init-form now emits a runtime (%MARK-SPECIAL 'name) call so the special proclamation works for variables loaded from a precompiled FASL (the compile-time CL_SYM_SPECIAL flag does not survive into a fresh load — bare defvars were non-special on load, breaking dynamic LET binding e.g. serapeum DEFPLACE). Changes the bytecode emitted for every no-init defvar; bumping forces stale cached FASLs to be recompiled. v21: defconstant emits a runtime (%MARK-CONSTANT 'name) call so CONSTANTP works for FASL-loaded constants. */

/* Serialized constant type tags */
#define FASL_TAG_NIL         0x00
#define FASL_TAG_T           0x01
#define FASL_TAG_FIXNUM      0x02
#define FASL_TAG_CHARACTER   0x03
#define FASL_TAG_SYMBOL      0x04
#define FASL_TAG_STRING      0x05
#define FASL_TAG_CONS        0x06
#define FASL_TAG_BYTECODE    0x07
#define FASL_TAG_SINGLE_FLOAT 0x08
#define FASL_TAG_DOUBLE_FLOAT 0x09
#define FASL_TAG_RATIO       0x0A
#define FASL_TAG_BIGNUM      0x0B
#define FASL_TAG_VECTOR      0x0C
#define FASL_TAG_UNBOUND     0x0E
#define FASL_TAG_BIT_VECTOR  0x0F
#define FASL_TAG_PATHNAME    0x10
#define FASL_TAG_CLOSURE     0x11
#define FASL_TAG_FUNCTION    0x12
#define FASL_TAG_CONST_REF   0x13  /* u16 index: back-reference to earlier constant in same bytecode */
#define FASL_TAG_GENSYM_DEF  0x14  /* u16 id, u16 name_len, bytes: define new uninterned symbol */
#define FASL_TAG_GENSYM_REF  0x15  /* u16 id: reference to previously defined gensym */
#define FASL_TAG_PACKAGE     0x16  /* u16 name_len, bytes: package lookup by name */
#define FASL_TAG_COMPLEX     0x17  /* realpart, imagpart: complex number */
#ifdef CL_WIDE_STRINGS
#define FASL_TAG_WIDE_STRING 0x18  /* u32 length, u32[] codepoints */
#endif
#define FASL_TAG_STRUCT      0x19  /* type_desc(sym), u32 n_slots, slots... */
#define FASL_TAG_OBJ_DEF    0x1A  /* u16 id: define shared object (followed by object data) */
#define FASL_TAG_OBJ_REF    0x1B  /* u16 id: back-reference to previously defined shared object */
#define FASL_TAG_CLASS_REF  0x1C  /* class-name(symbol): (find-class name) at load time.
                                     CLOS class metaobjects (STANDARD-CLASS/BUILT-IN-CLASS
                                     structs) are cyclic by design — preserves identity
                                     and avoids re-creating broken stand-ins. */
#define FASL_TAG_LOAD_FORM  0x1E  /* creation-form(obj), init-form(obj):
                                     a literal object whose class has a user
                                     MAKE-LOAD-FORM method.  Reconstructed at
                                     load time by evaluating the creation form
                                     to build the object, registering it at the
                                     enclosing OBJ_DEF id (so the init form's
                                     OBJ_REF self-reference resolves to it),
                                     then evaluating the init form for effect.
                                     The forms are plain data (cons trees);
                                     they are compiled+evaluated at load. */
#define FASL_TAG_LOCK       0x1D  /* u8 flags (bit 0 = recursive), name(obj):
                                     fresh-at-load-time mutex.  Identity is not
                                     preserved across host processes (the platform
                                     mutex is per-process state), but multiple
                                     references within one FASL share via OBJ_DEF/REF
                                     so a struct slot and its callers all see the
                                     same fresh lock after load. */
#define FASL_TAG_MD_ARRAY   0x1F  /* u32 length, u8 rank, u32[rank] dims, then
                                     `length` elements: a multidimensional array
                                     (rank>1).  FASL_TAG_VECTOR stays the compact
                                     rank<=1 form; only true multi-dim arrays use
                                     this tag, so the rank/dims are preserved
                                     across compile-file instead of collapsing to
                                     a flat 1-D vector. */

/* Error codes for FASL operations */
#define FASL_OK              0
#define FASL_ERR_OVERFLOW    -1   /* Buffer too small */
#define FASL_ERR_TRUNCATED   -2   /* Unexpected end of data */
#define FASL_ERR_BAD_MAGIC   -3   /* Invalid magic number */
#define FASL_ERR_BAD_VERSION -4   /* Unsupported version */
#define FASL_ERR_BAD_TAG     -5   /* Unknown constant type tag */
#define FASL_ERR_TOO_DEEP   -6   /* Object graph too deep for C stack */

/* Max uninterned symbols tracked per FASL file (for gensym dedup) */
#define FASL_MAX_GENSYMS 1024

/* Max shared objects tracked per unit (for cycle/sharing detection).
 * Capped at uint16_t max because the wire format encodes IDs as u16.
 * Keeping a generous limit avoids re-serializing the same closure many
 * times when a single top-level form (e.g. a CLOS dispatch table) builds
 * thousands of structurally-shared closures — without dedup, the FASL
 * unit balloons exponentially. */
#define FASL_MAX_SHARED 65535

/* --- Serialization buffer --- */

typedef struct {
    uint8_t *data;
    uint32_t capacity;
    uint32_t pos;
    int error;          /* FASL_OK or error code */
    /* Uninterned symbol dedup table */
    CL_Obj   gensym_objs[FASL_MAX_GENSYMS];  /* original CL_Obj values */
    uint16_t gensym_count;
    /* Shared object dedup table (cycle/sharing detection for closures, bytecodes,
     * structs, symbols, locks, and conses).  shared_objs is indexed by id and
     * holds the original CL_Obj.  shared_hash maps obj -> id+1 (0 = empty slot)
     * via open-addressing linear probing — keeps writer-side dedup O(1) per
     * lookup even when shared_count grows into the tens of thousands (cons
     * dedup makes that realistic).  Both tables lazily allocated. */
    CL_Obj  *shared_objs;      /* heap-allocated, FASL_MAX_SHARED entries */
    uint16_t shared_count;
    uint16_t pad_;             /* explicit pad: keep 32-bit struct layout stable */
    uint16_t *shared_hash;     /* hash slots; size shared_hash_cap (power of two) */
    uint32_t shared_hash_cap;  /* 0 until allocated; always a power of two when set */
} CL_FaslWriter;

/* --- Deserialization buffer --- */

typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t pos;
    int error;
    /* Uninterned symbol dedup table */
    CL_Obj   gensym_objs[FASL_MAX_GENSYMS];  /* deserialized symbol objects */
    uint16_t gensym_count;
    /* Shared object dedup table */
    CL_Obj  *shared_objs;      /* heap-allocated, FASL_MAX_SHARED entries */
    uint16_t shared_count;
    /* OBJ_DEF id awaiting registration.  Set by FASL_TAG_OBJ_DEF before
     * recursing into the body; consumed by struct/closure/bytecode body
     * deserializers right after they allocate their shell so cyclic
     * OBJ_REFs inside the body resolve to the partially-constructed
     * object.  0xFFFFFFFF = none pending. */
    uint32_t pending_obj_def_id;
} CL_FaslReader;

/* --- Writer API --- */

void cl_fasl_writer_init(CL_FaslWriter *w, uint8_t *buf, uint32_t capacity);

/* Free heap-allocated dedup tables (shared_objs, shared_hash).  Safe to call
 * multiple times and on a freshly-initialized writer.  Does NOT free the
 * caller-supplied output buffer (`w->data`). */
void cl_fasl_writer_release(CL_FaslWriter *w);

/* Write primitives (big-endian) */
void cl_fasl_write_u8(CL_FaslWriter *w, uint8_t val);
void cl_fasl_write_u16(CL_FaslWriter *w, uint16_t val);
void cl_fasl_write_u32(CL_FaslWriter *w, uint32_t val);
void cl_fasl_write_bytes(CL_FaslWriter *w, const void *data, uint32_t len);

/* Serialize a CL_Obj constant */
void cl_fasl_serialize_obj(CL_FaslWriter *w, CL_Obj obj);

/* Serialize a CL_Bytecode object (the full blob) */
void cl_fasl_serialize_bytecode(CL_FaslWriter *w, CL_Obj bc_obj);
void cl_fasl_reset_serialize_count(void);

/* Write a complete FASL file header */
void cl_fasl_write_header(CL_FaslWriter *w, uint32_t n_units);

/* --- Reader API --- */

void cl_fasl_reader_init(CL_FaslReader *r, const uint8_t *data, uint32_t size);

/* Read primitives (big-endian) */
uint8_t  cl_fasl_read_u8(CL_FaslReader *r);
uint16_t cl_fasl_read_u16(CL_FaslReader *r);
uint32_t cl_fasl_read_u32(CL_FaslReader *r);
void     cl_fasl_read_bytes(CL_FaslReader *r, void *out, uint32_t len);

/* Deserialize a CL_Obj constant */
CL_Obj cl_fasl_deserialize_obj(CL_FaslReader *r);

/* Deserialize a CL_Bytecode object (the full blob) */
CL_Obj cl_fasl_deserialize_bytecode(CL_FaslReader *r);

/* Read and validate a FASL file header; returns n_units or sets r->error */
uint32_t cl_fasl_read_header(CL_FaslReader *r);

/* --- Active-reader registry (GC rooting of dedup tables) ---
 * A reader's gensym_objs[]/shared_objs[] hold CL_Obj references that forward
 * GENSYM_REF/OBJ_REF resolve through; they must be treated as GC roots for the
 * duration of a load so compaction forwards them.  See fasl.c for the full
 * rationale ("Undefined function: PACKAGE" corruption under heap pressure). */
void cl_fasl_reader_register(CL_FaslReader *r);
void cl_fasl_reader_unregister(CL_FaslReader *r);
void cl_fasl_gc_mark_readers(void);
void cl_fasl_gc_update_readers(void (*update_fn)(CL_Obj *));

/* --- MAKE-LOAD-FORM writer pre-pass (see FASL_TAG_LOAD_FORM) ---
 * cl_fasl_mlf_prepass walks the constant graph of the BC_COUNT bytecode
 * units in BC_VEC (a Lisp vector) and records a (creation . init) load
 * form for every literal object whose class has a user MAKE-LOAD-FORM
 * method, so the serializer can emit FASL_TAG_LOAD_FORM for them.  Call
 * it once before serializing a COMPILE-FILE run's units, and
 * cl_fasl_mlf_cleanup() once serialization (or an error path) is done.
 * The mark/update hooks GC-root the pre-pass state — the pre-pass calls
 * user code that can compact. */
void cl_fasl_mlf_prepass(CL_Obj bc_vec, int bc_count);
void cl_fasl_mlf_cleanup(void);
void cl_fasl_gc_mark_mlf(void);
void cl_fasl_gc_update_mlf(void (*update_fn)(CL_Obj *));
/* Error-unwind snapshot/restore of the active-reader count (cl_error longjmps
 * past cl_fasl_reader_unregister; the error frame restores the count). */
int  cl_fasl_reader_save_count(void);
void cl_fasl_reader_restore_count(int n);

#ifdef DEBUG_FASL
/* --- Per-unit serialization histogram (debug builds only) ---
 *
 * Diagnostic instrumentation for investigating FASL bloat.  Bytes written
 * during cl_fasl_serialize_bytecode are attributed to a per-type bucket;
 * dedup hits (OBJ_REF inter-unit, CONST_REF intra-bytecode) are counted
 * separately so bytes-saved-by-dedup is visible.
 *
 * Usage: call cl_fasl_hist_begin() before cl_fasl_serialize_bytecode() and
 * cl_fasl_hist_dump(label) after if the unit overflowed (or unconditionally
 * for blanket profiling).  cl_fasl_hist_end() turns it off. */
void cl_fasl_hist_begin(void);
void cl_fasl_hist_end(void);
void cl_fasl_hist_dump(const char *label, uint32_t bytes_written);
#endif

/* --- High-level API --- */

/* Load a FASL file from memory buffer, executing each unit.
 * Returns CL_T on success, signals error on failure. */
CL_Obj cl_fasl_load(const uint8_t *data, uint32_t size);

#endif /* CL_FASL_H */
