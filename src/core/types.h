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

/* Version string — used for FASL cache paths */
#define CL_VERSION_STRING "0.1"

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
    TYPE_HASHTABLE,
    TYPE_CONDITION,
    TYPE_STRUCT,
    TYPE_BIGNUM,
    TYPE_SINGLE_FLOAT,
    TYPE_DOUBLE_FLOAT,
    TYPE_RATIO,
    TYPE_COMPLEX,
    TYPE_STREAM,
    TYPE_RANDOM_STATE,
    TYPE_BIT_VECTOR,
    TYPE_PATHNAME,
    TYPE_CELL,
    TYPE_THREAD,
    TYPE_LOCK,
    TYPE_CONDVAR,
    TYPE_FOREIGN_POINTER
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
#define CL_SYM_EXPORTED 0x08
#define CL_SYM_CONSTANT 0x10

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

/* Source line map entry: maps bytecode offset to source line number */
typedef struct {
    uint16_t pc;        /* Bytecode offset */
    uint16_t line;      /* Source line number */
} CL_LineEntry;

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
    uint8_t *key_suppliedp_slots; /* Slot indices for supplied-p vars (or 0xFF=none) */
    /* Source location tracking (platform_alloc'd) */
    CL_LineEntry *line_map;  /* Array of pc→line entries, sorted by pc */
    uint16_t line_map_count; /* Number of entries */
    uint16_t source_line;    /* Line where function was defined */
    const char *source_file; /* Filename (NULL for REPL) */
} CL_Bytecode;

#define CL_BYTECODE_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_BYTECODE)

/* --- Closure (bytecode + captured upvalues) --- */

typedef struct {
    CL_Header hdr;
    CL_Obj bytecode;    /* CL_Bytecode pointer */
    CL_Obj upvalues[];  /* Captured variable values */
} CL_Closure;

#define CL_CLOSURE_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_CLOSURE)

/* --- Vector / Array --- */

/* Vector flags */
#define CL_VEC_FLAG_FILL_POINTER  0x01
#define CL_VEC_FLAG_ADJUSTABLE    0x02
#define CL_VEC_FLAG_MULTIDIM      0x04
#define CL_VEC_FLAG_DISPLACED     0x08  /* data[0] = CL_Obj ref to backing vector */
#define CL_VEC_FLAG_STRING        0x10  /* character vector (elements are CL_MAKE_CHAR) */

/* Sentinel: no fill pointer */
#define CL_NO_FILL_POINTER  0xFFFFFFFFu

typedef struct {
    CL_Header hdr;
    uint32_t length;          /* Total element count */
    uint32_t fill_pointer;    /* CL_NO_FILL_POINTER = none */
    uint8_t  flags;           /* CL_VEC_FLAG_* */
    uint8_t  rank;            /* 0 = simple 1D, >1 = multi-dim */
    uint16_t _reserved;       /* Alignment padding */
    CL_Obj data[];            /* For rank>1: dims in data[0..rank-1], elements at data[rank] */
} CL_Vector;

#define CL_VECTOR_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_VECTOR)

/* String-like vector: CL_Vector with CL_VEC_FLAG_STRING (adjustable/fill-pointer character arrays) */
#define CL_STRING_VECTOR_P(obj) \
    (CL_VECTOR_P(obj) && (((CL_Vector *)CL_OBJ_TO_PTR(obj))->flags & CL_VEC_FLAG_STRING))

/* Access helpers: data pointer (skips dimension storage for multi-dim) */
/* For displaced vectors, follow data[0] to the backing vector */
CL_Obj *cl_vector_data_fn(CL_Vector *v);
#define cl_vector_data(v)  \
    ((v)->flags & CL_VEC_FLAG_DISPLACED \
     ? cl_vector_data_fn(v) \
     : ((v)->rank > 1 ? &(v)->data[(v)->rank] : (v)->data))

/* Active length: fill pointer if present, else total length */
#define cl_vector_active_length(v) \
    ((v)->fill_pointer != CL_NO_FILL_POINTER ? (v)->fill_pointer : (v)->length)

/* --- Package --- */

typedef struct {
    CL_Header hdr;
    CL_Obj name;        /* CL_String */
    CL_Obj symbols;     /* Hash table (vector of lists) */
    CL_Obj use_list;    /* List of used packages */
    CL_Obj nicknames;   /* List of nickname strings */
    CL_Obj local_nicknames; /* CDR-10: alist ((nick-string . package) ...) */
    CL_Obj shadowing_symbols; /* List of shadowing symbols */
    uint32_t sym_count;
} CL_Package;

#define CL_PACKAGE_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_PACKAGE)

/* --- Hash Table --- */

/* Test function enum: stored in the test field */
#define CL_HT_TEST_EQ     0
#define CL_HT_TEST_EQL    1
#define CL_HT_TEST_EQUAL  2
#define CL_HT_TEST_EQUALP 3

typedef struct {
    CL_Header hdr;
    uint32_t test;          /* CL_HT_TEST_EQ/EQL/EQUAL */
    uint32_t count;         /* Number of entries */
    uint32_t bucket_count;  /* Number of buckets (always power of 2) */
    CL_Obj bucket_vec;      /* CL_NIL = use inline buckets[], else CL_Vector */
    CL_Obj buckets[];       /* Flexible array: initial buckets (used when bucket_vec==NIL) */
} CL_Hashtable;

#define CL_HASHTABLE_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_HASHTABLE)

/* --- Condition --- */

typedef struct {
    CL_Header hdr;
    CL_Obj type_name;      /* Symbol: SIMPLE-ERROR, TYPE-ERROR, etc. */
    CL_Obj slots;          /* Alist: ((slot-name . value) ...) */
    CL_Obj report_string;  /* Pre-formatted message string, or NIL */
} CL_Condition;

#define CL_CONDITION_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_CONDITION)

/* --- Structure --- */

typedef struct {
    CL_Header hdr;
    CL_Obj type_desc;      /* Symbol: struct type name */
    uint32_t n_slots;      /* Number of slots */
    CL_Obj slots[];        /* Positional slot values */
} CL_Struct;

#define CL_STRUCT_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_STRUCT)

/* --- Bignum (arbitrary-precision integer) --- */

typedef struct {
    CL_Header hdr;
    uint32_t length;    /* Number of 16-bit limbs */
    uint32_t sign;      /* 0 = positive, 1 = negative */
    uint16_t limbs[];   /* Flexible array: little-endian (limbs[0] = least significant) */
} CL_Bignum;

#define CL_BIGNUM_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_BIGNUM)
#define CL_INTEGER_P(obj) (CL_FIXNUM_P(obj) || CL_BIGNUM_P(obj))

/* --- Ratio (exact fraction p/q, always in lowest terms, q > 0) --- */

typedef struct {
    CL_Header hdr;          /* 4 bytes */
    CL_Obj numerator;       /* Integer (fixnum or bignum) */
    CL_Obj denominator;     /* Positive integer (fixnum or bignum), never 0 or 1 in normalized form */
} CL_Ratio;                 /* 12 bytes total */

#define CL_RATIO_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_RATIO)
#define CL_RATIONAL_P(obj) (CL_INTEGER_P(obj) || CL_RATIO_P(obj))

/* --- Complex number (a + bi) --- */

typedef struct {
    CL_Header hdr;          /* 4 bytes */
    CL_Obj realpart;        /* Real part (any real number) */
    CL_Obj imagpart;        /* Imaginary part (any real number) */
} CL_Complex;               /* 12 bytes total */

#define CL_COMPLEX_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_COMPLEX)

/* --- Stream --- */

/* Stream direction */
#define CL_STREAM_INPUT   1
#define CL_STREAM_OUTPUT  2
#define CL_STREAM_IO      3

/* Stream type */
#define CL_STREAM_CONSOLE 0
#define CL_STREAM_FILE    1
#define CL_STREAM_STRING  2
#define CL_STREAM_CBUF    3  /* C buffer input (outside GC arena) */
#define CL_STREAM_SYNONYM 4  /* Synonym stream: delegates to symbol's value */
#define CL_STREAM_SOCKET  5  /* TCP socket stream (bidirectional, binary) */

/* Stream flags */
#define CL_STREAM_FLAG_OPEN  0x01
#define CL_STREAM_FLAG_EOF   0x02

typedef struct {
    CL_Header hdr;
    uint32_t direction;      /* INPUT=1, OUTPUT=2, IO=3 */
    uint32_t stream_type;    /* CONSOLE=0, FILE=1, STRING=2 */
    uint32_t flags;          /* bit 0: open, bit 1: eof */
    uint32_t handle_id;      /* Index into platform file handle side table */
    CL_Obj   string_buf;     /* Source string for input streams, NIL for output */
    uint32_t position;       /* Read/write cursor */
    uint32_t out_buf_handle; /* Index into side table for growable C buffer */
    uint32_t out_buf_size;   /* Output buffer capacity */
    uint32_t out_buf_len;    /* Output buffer used length */
    int32_t  unread_char;    /* -1 if none, else pushed-back char */
    CL_Obj   element_type;   /* Symbol (CHARACTER) */
    uint32_t charpos;        /* Column position: 0 = at BOL, 0xFFFFFFFF = unknown */
} CL_Stream;

#define CL_STREAM_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_STREAM)

/* --- Random State (xorshift128) --- */

typedef struct {
    CL_Header hdr;
    uint32_t s[4];  /* xorshift128 state */
} CL_RandomState;

#define CL_RANDOM_STATE_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_RANDOM_STATE)

/* --- Bit Vector (packed bit array) --- */

typedef struct {
    CL_Header hdr;
    uint32_t length;          /* Number of bits */
    uint32_t fill_pointer;    /* CL_NO_FILL_POINTER = none */
    uint8_t  flags;           /* CL_VEC_FLAG_FILL_POINTER | CL_VEC_FLAG_ADJUSTABLE */
    uint8_t  _pad[3];
    uint32_t data[];          /* ceil(length/32) packed words, LSB-first */
} CL_BitVector;

#define CL_BIT_VECTOR_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_BIT_VECTOR)

/* Number of uint32_t words needed for n bits */
#define CL_BV_WORDS(n)  (((n) + 31) / 32)

/* Get bit i: LSB-first within each word */
#define cl_bv_get_bit(bv, i) \
    (((bv)->data[(i) / 32] >> ((i) % 32)) & 1u)

/* Set bit i to val (0 or 1) */
#define cl_bv_set_bit(bv, i, val) \
    do { \
        if (val) (bv)->data[(i) / 32] |= (1u << ((i) % 32)); \
        else     (bv)->data[(i) / 32] &= ~(1u << ((i) % 32)); \
    } while (0)

/* Active length: fill pointer if present, else total length */
#define cl_bv_active_length(bv) \
    ((bv)->fill_pointer != CL_NO_FILL_POINTER ? (bv)->fill_pointer : (bv)->length)

/* --- Pathname --- */

typedef struct {
    CL_Header hdr;
    CL_Obj host;       /* NIL or string */
    CL_Obj device;     /* NIL or string (Amiga volume/assign) */
    CL_Obj directory;  /* NIL or list: (:ABSOLUTE "d1" "d2") or (:RELATIVE ...) */
    CL_Obj name;       /* NIL or string */
    CL_Obj type;       /* NIL or string (extension, no dot) */
    CL_Obj version;    /* NIL or :NEWEST or fixnum */
} CL_Pathname;

#define CL_PATHNAME_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_PATHNAME)

/* --- Cell (heap-boxed mutable binding for closures) --- */

typedef struct {
    CL_Header hdr;
    CL_Obj value;
} CL_Cell;

#define CL_CELL_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_CELL)

/* --- Thread object (Lisp-visible wrapper for CL_Thread) --- */

typedef struct {
    CL_Header hdr;
    uint32_t thread_id;   /* Side table index -> CL_Thread* */
    CL_Obj name;          /* CL string or NIL */
} CL_ThreadObj;

#define CL_THREAD_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_THREAD)

/* --- Lock (mutex wrapper) --- */

typedef struct {
    CL_Header hdr;
    uint32_t lock_id;     /* Side table index -> void* (platform mutex) */
    CL_Obj name;          /* CL string or NIL */
} CL_Lock;

#define CL_LOCK_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_LOCK)

/* --- Condition variable wrapper --- */

typedef struct {
    CL_Header hdr;
    uint32_t condvar_id;  /* Side table index -> void* (platform condvar) */
} CL_CondVar;

#define CL_CONDVAR_P(obj) (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_CONDVAR)

/* --- Foreign Pointer (wraps a raw machine address, opaque to GC) --- */

#define CL_FPTR_FLAG_OWNED   0x01  /* We allocated this, should free on cleanup */
#define CL_FPTR_FLAG_CHIP    0x02  /* Amiga chip memory (MEMF_CHIP) */

typedef struct {
    CL_Header hdr;
    uint32_t  address;    /* Raw 32-bit machine address (Amiga) or side-table handle (POSIX) */
    uint32_t  size;       /* Allocated size in bytes (0 = unknown/external) */
    uint8_t   flags;      /* CL_FPTR_FLAG_* */
    uint8_t   _pad[3];
} CL_ForeignPtr;           /* 16 bytes */

#define CL_FOREIGN_POINTER_P(obj) \
    (CL_HEAP_P(obj) && CL_HDR_TYPE(CL_OBJ_TO_PTR(obj)) == TYPE_FOREIGN_POINTER)

/* --- Convenience accessors --- */

CL_Obj cl_car(CL_Obj obj);
CL_Obj cl_cdr(CL_Obj obj);
CL_Obj cl_cons(CL_Obj car, CL_Obj cdr);

/* Type name for printing/errors */
const char *cl_type_name(CL_Obj obj);

/* Check if obj is of type type_spec (same as CL typep) */
int cl_typep(CL_Obj obj, CL_Obj type_spec);

/* Initialize type system (sets up CL_T, etc.) */
void cl_types_init(void);

#endif /* CL_TYPES_H */
