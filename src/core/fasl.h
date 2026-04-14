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
#define CL_FASL_VERSION  3

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

/* Max shared objects tracked per unit (for cycle/sharing detection) */
#define FASL_MAX_SHARED 4096

/* --- Serialization buffer --- */

typedef struct {
    uint8_t *data;
    uint32_t capacity;
    uint32_t pos;
    int error;          /* FASL_OK or error code */
    /* Uninterned symbol dedup table */
    CL_Obj   gensym_objs[FASL_MAX_GENSYMS];  /* original CL_Obj values */
    uint16_t gensym_count;
    /* Shared object dedup table (cycle/sharing detection for closures, bytecodes, etc.) */
    CL_Obj  *shared_objs;      /* heap-allocated, FASL_MAX_SHARED entries */
    uint16_t shared_count;
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
} CL_FaslReader;

/* --- Writer API --- */

void cl_fasl_writer_init(CL_FaslWriter *w, uint8_t *buf, uint32_t capacity);

/* Write primitives (big-endian) */
void cl_fasl_write_u8(CL_FaslWriter *w, uint8_t val);
void cl_fasl_write_u16(CL_FaslWriter *w, uint16_t val);
void cl_fasl_write_u32(CL_FaslWriter *w, uint32_t val);
void cl_fasl_write_bytes(CL_FaslWriter *w, const void *data, uint32_t len);

/* Serialize a CL_Obj constant */
void cl_fasl_serialize_obj(CL_FaslWriter *w, CL_Obj obj);

/* Serialize a CL_Bytecode object (the full blob) */
void cl_fasl_serialize_bytecode(CL_FaslWriter *w, CL_Obj bc_obj);

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

/* --- High-level API --- */

/* Load a FASL file from memory buffer, executing each unit.
 * Returns CL_T on success, signals error on failure. */
CL_Obj cl_fasl_load(const uint8_t *data, uint32_t size);

#endif /* CL_FASL_H */
