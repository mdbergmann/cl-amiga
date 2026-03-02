#ifndef CL_TYPES_H
#define CL_TYPES_H

/*
 * CL-Amiga tagged pointer object representation (32-bit)
 *
 * Bit layout of CL_Obj (uint32_t):
 *   xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx1  = Fixnum (31-bit signed, tag bit 0)
 *   pppppppppppppppppppppppppppppp00  = Heap offset (4-byte aligned arena-relative)
 *   cccccccccccccccccccccccc00001010  = Character (24-bit, tag 0x0A)
 *   00000000000000000000000000000000  = NIL (offset 0)
 *
 * Heap pointers are stored as byte offsets from the arena base.
 * This allows the same 32-bit representation to work on both
 * 32-bit Amiga targets and 64-bit development hosts.
 */

#include <stdint.h>
#include <stddef.h>

typedef uint32_t CL_Obj;

/* Arena base pointer — needed for offset↔pointer conversion */
extern uint8_t *cl_arena_base;

/* NIL and T */
#define CL_NIL  ((CL_Obj)0)
extern CL_Obj CL_T;  /* Pre-allocated symbol, set during init */

/* Tag masks */
#define CL_TAG_FIXNUM   0x01
#define CL_TAG_CHAR     0x0A
#define CL_TAG_MASK_LO2 0x03

/* --- Fixnum (31-bit signed integer, tag bit 0) --- */

#define CL_FIXNUM_P(obj)    ((obj) & CL_TAG_FIXNUM)
#define CL_MAKE_FIXNUM(n)   ((CL_Obj)(((uint32_t)(int32_t)(n) << 1) | CL_TAG_FIXNUM))
#define CL_FIXNUM_VAL(obj)  ((int32_t)(obj) >> 1)

#define CL_FIXNUM_MAX  ((int32_t)0x3FFFFFFF)   /*  1073741823 */
#define CL_FIXNUM_MIN  ((int32_t)0xC0000000)   /* -1073741824 */

/* --- Character (24-bit value, tag byte 0x0A) --- */

#define CL_CHAR_P(obj)      (((obj) & 0xFF) == CL_TAG_CHAR)
#define CL_MAKE_CHAR(c)     ((CL_Obj)(((uint32_t)(c) << 8) | CL_TAG_CHAR))
#define CL_CHAR_VAL(obj)    ((int)((obj) >> 8))

/* --- Heap pointer (low 2 bits = 00, arena-relative offset) --- */

#define CL_HEAP_P(obj)      ((obj) != CL_NIL && !CL_FIXNUM_P(obj) && !CL_CHAR_P(obj))
#define CL_OBJ_TO_PTR(obj)  ((void *)(cl_arena_base + (obj)))
#define CL_PTR_TO_OBJ(ptr)  ((CL_Obj)((uint8_t *)(ptr) - cl_arena_base))

/* --- NIL checks --- */

#define CL_NULL_P(obj)      ((obj) == CL_NIL)

/* --- Heap object header --- */

/*
 * Header word layout: [type:8][gc_mark:1][size:23]
 * size = total allocation size in bytes (including header)
 */

#define CL_HDR_TYPE_SHIFT   24
#define CL_HDR_MARK_BIT     (1u << 23)
#define CL_HDR_SIZE_MASK    0x007FFFFFu

typedef struct {
    uint32_t header;
} CL_Header;

/* Heap object type tags */
enum CL_ObjType {
    TYPE_CONS = 0,
    TYPE_SYMBOL,
    TYPE_STRING,
    TYPE_FUNCTION,
    TYPE_CLOSURE,
    TYPE_BYTECODE,
    TYPE_VECTOR,
    TYPE_PACKAGE,
    TYPE_HASHTABLE
};

/* Header access macros */
#define CL_HDR(ptr)         (((CL_Header *)(ptr))->header)
#define CL_HDR_TYPE(ptr)    ((CL_HDR(ptr) >> CL_HDR_TYPE_SHIFT) & 0xFF)
#define CL_HDR_SIZE(ptr)    (CL_HDR(ptr) & CL_HDR_SIZE_MASK)
#define CL_HDR_MARKED(ptr)  (CL_HDR(ptr) & CL_HDR_MARK_BIT)

#define CL_HDR_SET_MARK(ptr)   (CL_HDR(ptr) |= CL_HDR_MARK_BIT)
#define CL_HDR_CLR_MARK(ptr)   (CL_HDR(ptr) &= ~CL_HDR_MARK_BIT)

#define CL_MAKE_HDR(type, size) \
    (((uint32_t)(type) << CL_HDR_TYPE_SHIFT) | ((uint32_t)(size) & CL_HDR_SIZE_MASK))

/* --- Cons cell --- */

typedef struct {
    CL_Header hdr;
    CL_Obj car;
    CL_Obj cdr;
} CL_Cons;

#define CL_CONS_P(obj)  (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_CONS)

/* --- Symbol --- */

typedef struct {
    CL_Header hdr;
    CL_Obj name;      /* CL_String pointer */
    CL_Obj value;     /* Global value (unbound marker if unset) */
    CL_Obj function;  /* Function binding */
    CL_Obj plist;     /* Property list */
    CL_Obj package;   /* Home package */
    uint32_t hash;    /* Cached name hash */
    uint32_t flags;   /* bit 0: IS_SPECIAL (declared by defvar/defparameter) */
} CL_Symbol;

#define CL_SYM_SPECIAL  0x01
#define CL_SYM_INLINE   0x02
#define CL_SYM_TRACED   0x04

#define CL_SYMBOL_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_SYMBOL)

#define CL_UNBOUND  ((CL_Obj)0xFFFFFFFF)  /* Sentinel for unbound variables */

/* --- String --- */

typedef struct {
    CL_Header hdr;
    uint32_t length;   /* Character count */
    char data[];       /* Flexible array member (C99) */
} CL_String;

#define CL_STRING_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_STRING)

/* --- Function (built-in C function) --- */

typedef CL_Obj (*CL_CFunc)(CL_Obj *args, int nargs);

typedef struct {
    CL_Header hdr;
    CL_CFunc func;
    CL_Obj name;       /* Symbol */
    int min_args;
    int max_args;       /* -1 for variadic */
} CL_Function;

#define CL_FUNCTION_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_FUNCTION)

/* --- Bytecode (compiled function template) --- */

typedef struct {
    CL_Header hdr;
    uint8_t *code;      /* Bytecode array */
    CL_Obj *constants;  /* Constants pool */
    uint32_t code_len;
    uint16_t n_constants;
    uint16_t arity;     /* bits 0-14: required params, bit 15: has_rest */
    uint16_t n_locals;
    uint16_t n_upvalues;
    CL_Obj name;        /* Symbol or NIL */
    uint8_t n_optional; /* Number of &optional params */
    uint8_t flags;      /* bit 0: has_key, bit 1: allow_other_keys */
    uint8_t n_keys;     /* Number of &key params */
    CL_Obj *key_syms;   /* Keyword symbols array (platform_alloc'd) */
    uint8_t *key_slots; /* Slot indices for each key param (platform_alloc'd) */
} CL_Bytecode;

#define CL_BYTECODE_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_BYTECODE)

/* --- Closure (bytecode + captured upvalues) --- */

typedef struct {
    CL_Header hdr;
    CL_Obj bytecode;    /* CL_Bytecode pointer */
    CL_Obj upvalues[];  /* Captured variable values */
} CL_Closure;

#define CL_CLOSURE_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_CLOSURE)

/* --- Vector --- */

typedef struct {
    CL_Header hdr;
    uint32_t length;
    CL_Obj data[];
} CL_Vector;

#define CL_VECTOR_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_VECTOR)

/* --- Package --- */

typedef struct {
    CL_Header hdr;
    CL_Obj name;        /* CL_String */
    CL_Obj symbols;     /* Hash table (vector of lists) */
    CL_Obj use_list;    /* List of used packages */
    uint32_t sym_count;
} CL_Package;

#define CL_PACKAGE_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_PACKAGE)

/* --- Hash Table --- */

/* Test function enum: stored in the test field */
#define CL_HT_TEST_EQ    0
#define CL_HT_TEST_EQL   1
#define CL_HT_TEST_EQUAL 2

typedef struct {
    CL_Header hdr;
    uint32_t test;          /* CL_HT_TEST_EQ/EQL/EQUAL */
    uint32_t count;         /* Number of entries */
    uint32_t bucket_count;  /* Number of buckets */
    CL_Obj buckets[];       /* Flexible array: each is a list of (key . value) pairs */
} CL_Hashtable;

#define CL_HASHTABLE_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_HASHTABLE)

/* --- Convenience accessors --- */

CL_Obj cl_car(CL_Obj obj);
CL_Obj cl_cdr(CL_Obj obj);
CL_Obj cl_cons(CL_Obj car, CL_Obj cdr);

/* Type name for printing/errors */
const char *cl_type_name(CL_Obj obj);

/* Initialize type system (sets up CL_T, etc.) */
void cl_types_init(void);

#endif /* CL_TYPES_H */
