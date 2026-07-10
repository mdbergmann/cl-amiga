#ifndef CL_OPCODES_H
#define CL_OPCODES_H

#include <stdint.h>

/*
 * Bytecode instruction set for the CL-Amiga VM.
 * Stack-based, byte-oriented encoding.
 *
 * Single source of truth: CL_OPCODE_LIST below is an X-macro that carries,
 * for every opcode, its byte value, mnemonic, operand encoding shape and
 * dataflow flags.  The enum, the disassembler table (builtins_io.c) and the
 * peephole optimizer's decoder (peephole.c) are all generated from it, so a
 * new opcode added here is automatically decodable everywhere — and an
 * opcode added to the enum WITHOUT correct operand/flag columns cannot
 * desynchronize the peephole pass: unknown bytes make the decoder bail out
 * and leave the bytecode untouched.
 *
 * When adding an opcode, keep the columns honest:
 *   - operand shape must match EXACTLY what the vm.c dispatch case reads
 *     (this file's old free-text comments had drifted from vm.c — e.g. the
 *     jump operands are i32, not i16; TAGBODY_GO takes one u16, not two);
 *   - flags must match what the vm.c case actually does (see below);
 *   - a new opcode also requires a CL_FASL_VERSION bump (fasl.h) and, if it
 *     can appear in JIT-compiled functions, m68k JIT walker support.
 *
 * Operand shapes:
 *   CL_OPND_NONE      no operand bytes
 *   CL_OPND_U8        one u8
 *   CL_OPND_U16       one big-endian u16
 *   CL_OPND_JREL      one big-endian i32 jump offset, relative to the first
 *                     byte AFTER the operand (cl_patch_jump convention).
 *                     For OP_CATCH/OP_UWPROT this is the NLX landing pad.
 *   CL_OPND_U16_JREL  u16 const index + i32 landing-pad offset relative to
 *                     the first byte after both operands (BLOCK_PUSH,
 *                     TAGBODY_PUSH)
 *   CL_OPND_U16_U16   two big-endian u16 (DEFSETF)
 *   CL_OPND_CLOSURE   u16 template const index + one (is_local, index) byte
 *                     pair per template upvalue — VARIABLE length; decoding
 *                     needs the constant pool to read the template's
 *                     n_upvalues
 *   CL_OPND_AMIGA     u16 + i16 + i32 + u8 (9 bytes; the i32 is a regspec
 *                     immediate, NOT a jump offset)
 *
 * Dataflow flags (ground truth: the vm.c dispatch case):
 *   CL_OPF_MVW    unconditionally writes cl_mv_count on its normal
 *                 (non-longjmp) path.  CALL/APPLY also carry it: the callee
 *                 always establishes the mv state.
 *   CL_OPF_MVR    observes multiple-values state (MV_LOAD/MV_TO_LIST/
 *                 NTH_VALUE read cl_mv_values; RET propagates it to the
 *                 caller; CALL/APPLY are conservatively marked because a
 *                 callee such as THROW captures the current mv state).
 *   CL_OPF_PURE   pushes exactly one value, pops nothing, cannot signal and
 *                 has no side effect — the only opcodes the peephole pass may
 *                 delete together with a following OP_POP.  Note OP_CAR/
 *                 OP_CDR/arithmetic are NOT pure: they signal type errors
 *                 (and that must survive at any speed).
 *   CL_OPF_UNCOND control never falls through (jump, return, longjmp, halt).
 */

typedef enum {
    CL_OPND_NONE = 0,
    CL_OPND_U8,
    CL_OPND_U16,
    CL_OPND_JREL,
    CL_OPND_U16_JREL,
    CL_OPND_U16_U16,
    CL_OPND_CLOSURE,
    CL_OPND_AMIGA
} CL_OperandKind;

#define CL_OPF_MVW    0x01
#define CL_OPF_MVR    0x02
#define CL_OPF_PURE   0x04
#define CL_OPF_UNCOND 0x08

/* X(name, value, mnemonic, operand-shape, flags) */
#define CL_OPCODE_LIST(X) \
    /* Constants and variables */ \
    X(OP_CONST,     0x01, "CONST",     CL_OPND_U16,  CL_OPF_MVW|CL_OPF_PURE) /* Push constant from pool */ \
    X(OP_LOAD,      0x02, "LOAD",      CL_OPND_U8,   CL_OPF_PURE)            /* Push local variable (no mv write!) */ \
    X(OP_STORE,     0x03, "STORE",     CL_OPND_U8,   0)                      /* locals[n] = TOS (peeks, does NOT pop) */ \
    X(OP_GLOAD,     0x04, "GLOAD",     CL_OPND_U16,  CL_OPF_MVW)             /* Push global; signals if unbound */ \
    X(OP_GSTORE,    0x05, "GSTORE",    CL_OPND_U16,  0)                      /* Store global (symbol value) */ \
    X(OP_UPVAL,     0x06, "UPVAL",     CL_OPND_U8,   CL_OPF_MVW|CL_OPF_PURE) /* Load closed-over variable */ \
    /* Stack manipulation */ \
    X(OP_POP,       0x10, "POP",       CL_OPND_NONE, 0)                      /* Discard top of stack */ \
    X(OP_DUP,       0x11, "DUP",       CL_OPND_NONE, CL_OPF_PURE)            /* Duplicate top of stack (no mv write) */ \
    /* List operations */ \
    X(OP_CONS,      0x20, "CONS",      CL_OPND_NONE, CL_OPF_MVW)             /* Cons cell from top 2 values */ \
    X(OP_CAR,       0x21, "CAR",       CL_OPND_NONE, CL_OPF_MVW)             /* Car; signals type-error on non-list */ \
    X(OP_CDR,       0x22, "CDR",       CL_OPND_NONE, CL_OPF_MVW)             /* Cdr; signals type-error on non-list */ \
    /* Arithmetic */ \
    X(OP_ADD,       0x30, "ADD",       CL_OPND_NONE, CL_OPF_MVW)             /* Addition (can signal / cons bignums) */ \
    X(OP_SUB,       0x31, "SUB",       CL_OPND_NONE, CL_OPF_MVW)             /* Subtraction */ \
    X(OP_MUL,       0x32, "MUL",       CL_OPND_NONE, CL_OPF_MVW)             /* Multiplication */ \
    X(OP_DIV,       0x33, "DIV",       CL_OPND_NONE, CL_OPF_MVW)             /* Division */ \
    /* Comparison */ \
    X(OP_EQ,        0x40, "EQ",        CL_OPND_NONE, CL_OPF_MVW)             /* Equal (eql) */ \
    X(OP_LT,        0x41, "LT",        CL_OPND_NONE, CL_OPF_MVW)             /* Less than */ \
    X(OP_GT,        0x42, "GT",        CL_OPND_NONE, CL_OPF_MVW)             /* Greater than */ \
    X(OP_LE,        0x43, "LE",        CL_OPND_NONE, CL_OPF_MVW)             /* Less or equal */ \
    X(OP_GE,        0x44, "GE",        CL_OPND_NONE, CL_OPF_MVW)             /* Greater or equal */ \
    X(OP_NUMEQ,     0x45, "NUMEQ",     CL_OPND_NONE, CL_OPF_MVW)             /* Numeric equal (=) */ \
    /* Logic */ \
    X(OP_NOT,       0x50, "NOT",       CL_OPND_NONE, CL_OPF_MVW)             /* Boolean not */ \
    /* Control flow (i32 relative offsets — see cl_patch_jump) */ \
    X(OP_JMP,       0x60, "JMP",       CL_OPND_JREL, CL_OPF_UNCOND)          /* Unconditional jump */ \
    X(OP_JNIL,      0x61, "JNIL",      CL_OPND_JREL, 0)                      /* Pop; jump if NIL */ \
    X(OP_JTRUE,     0x62, "JTRUE",     CL_OPND_JREL, 0)                      /* Pop; jump if not NIL */ \
    /* Functions */ \
    X(OP_CALL,      0x70, "CALL",      CL_OPND_U8,   CL_OPF_MVW|CL_OPF_MVR)  /* Call function with N args */ \
    X(OP_TAILCALL,  0x71, "TAILCALL",  CL_OPND_U8,   CL_OPF_MVR|CL_OPF_MVW)   /* Tail call; FALLS THROUGH for builtin callees (vm.c pushes the result and continues to the following RET) — NOT unconditional! */ \
    X(OP_RET,       0x72, "RET",       CL_OPND_NONE, CL_OPF_MVR|CL_OPF_UNCOND) /* Return; propagates mv state */ \
    X(OP_CLOSURE,   0x73, "CLOSURE",   CL_OPND_CLOSURE, CL_OPF_MVW)          /* Create closure from template */ \
    X(OP_APPLY,     0x74, "APPLY",     CL_OPND_NONE, CL_OPF_MVW|CL_OPF_MVR)  /* Apply function to arg list */ \
    /* Misc */ \
    X(OP_LIST,      0x80, "LIST",      CL_OPND_U8,   CL_OPF_MVW)             /* Build list from N stack values */ \
    X(OP_NIL,       0x81, "NIL",       CL_OPND_NONE, CL_OPF_MVW|CL_OPF_PURE) /* Push NIL */ \
    X(OP_T,         0x82, "T",         CL_OPND_NONE, CL_OPF_MVW|CL_OPF_PURE) /* Push T */ \
    X(OP_FLOAD,     0x83, "FLOAD",     CL_OPND_U16,  CL_OPF_MVW)             /* Load function binding of symbol */ \
    X(OP_DEFMACRO,  0x84, "DEFMACRO",  CL_OPND_U16,  0)                      /* Register TOS as macro expander */ \
    X(OP_CATCH,     0x85, "CATCH",     CL_OPND_JREL, 0)                      /* Pop tag, push NLX catch frame, setjmp */ \
    X(OP_ARGC,      0x86, "ARGC",      CL_OPND_NONE, CL_OPF_MVW)             /* Push actual argument count */ \
    X(OP_UNCATCH,   0x87, "UNCATCH",   CL_OPND_NONE, 0)                      /* Pop NLX catch frame (normal exit) */ \
    X(OP_UWPROT,    0x88, "UWPROT",    CL_OPND_JREL, 0)                      /* Push NLX uwprot frame, setjmp */ \
    X(OP_UWPOP,     0x89, "UWPOP",     CL_OPND_NONE, 0)                      /* Pop NLX uwprot frame (normal exit) */ \
    X(OP_UWRETHROW, 0x8A, "UWRETHROW", CL_OPND_NONE, 0)                      /* Re-throw pending if any, else nop */ \
    X(OP_MV_LOAD,   0x8B, "MV_LOAD",   CL_OPND_U8,   CL_OPF_MVR)             /* Push cl_mv_values[index] or NIL */ \
    X(OP_MV_TO_LIST,0x8C, "MV_TO_LIST",CL_OPND_NONE, CL_OPF_MVR|CL_OPF_MVW)  /* Build list from MV buffer */ \
    X(OP_NTH_VALUE, 0x8D, "NTH_VALUE", CL_OPND_NONE, CL_OPF_MVR|CL_OPF_MVW)  /* Pop index, push mv_values[index] */ \
    X(OP_DYNBIND,   0x8E, "DYNBIND",   CL_OPND_U16,  0)                      /* Pop value, dynamically bind symbol */ \
    X(OP_DYNUNBIND, 0x8F, "DYNUNBIND", CL_OPND_U8,   0)                      /* Restore N dynamic bindings */ \
    /* Mutation */ \
    X(OP_RPLACA,    0x90, "RPLACA",    CL_OPND_NONE, CL_OPF_MVW)             /* Set car, push new-car */ \
    X(OP_RPLACD,    0x91, "RPLACD",    CL_OPND_NONE, CL_OPF_MVW)             /* Set cdr, push new-cdr */ \
    X(OP_ASET,      0x92, "ASET",      CL_OPND_NONE, CL_OPF_MVW)             /* vector[idx] = val, push val */ \
    X(OP_DEFTYPE,   0x93, "DEFTYPE",   CL_OPND_U16,  0)                      /* Register TOS as type expander */ \
    X(OP_HANDLER_PUSH, 0x94, "HANDLER_PUSH", CL_OPND_U16, 0)                 /* Pop handler closure, push binding */ \
    X(OP_HANDLER_POP,  0x95, "HANDLER_POP",  CL_OPND_U8,  0)                 /* Pop N handler bindings */ \
    X(OP_RESTART_PUSH, 0x96, "RESTART_PUSH", CL_OPND_U16, 0)                 /* Pop tag+closure, push restart binding */ \
    X(OP_RESTART_POP,  0x97, "RESTART_POP",  CL_OPND_U8,  0)                 /* Pop N restart bindings */ \
    X(OP_ASSERT_TYPE,  0x98, "ASSERT_TYPE",  CL_OPND_U16, 0)                 /* Peek TOS, signal type-error if mismatch */ \
    X(OP_BLOCK_PUSH,   0x99, "BLOCK_PUSH",   CL_OPND_U16_JREL, 0)            /* NLX block frame for return-from */ \
    X(OP_BLOCK_POP,    0x9A, "BLOCK_POP",    CL_OPND_NONE, 0)                /* Pop NLX block frame (normal exit) */ \
    X(OP_BLOCK_RETURN, 0x9B, "BLOCK_RETURN", CL_OPND_U16, CL_OPF_MVR|CL_OPF_UNCOND) /* Pop value, longjmp to block (carries mv) */ \
    X(OP_FSTORE,       0x9C, "FSTORE",       CL_OPND_U16, 0)                 /* Store to function binding (peek) */ \
    /* Heap-boxed cells for mutable closure bindings */ \
    X(OP_MAKE_CELL,      0x9D, "MAKE_CELL",      CL_OPND_NONE, 0)            /* Pop value, create cell, push cell */ \
    X(OP_CELL_REF,       0x9E, "CELL_REF",       CL_OPND_NONE, 0)            /* Pop cell, push cell->value */ \
    X(OP_CELL_SET_LOCAL, 0x9F, "CELL_SET_LOCAL", CL_OPND_U8,   0)            /* cell=locals[slot], cell->value=TOS */ \
    X(OP_CELL_SET_UPVAL, 0xA0, "CELL_SET_UPVAL", CL_OPND_U8,   0)            /* cell=upvalue[idx], cell->value=TOS */ \
    /* NLX tagbody/go for cross-closure go support */ \
    X(OP_TAGBODY_PUSH, 0xA1, "TAGBODY_PUSH", CL_OPND_U16_JREL, 0)            /* NLX tagbody frame; offset = dispatch pad */ \
    X(OP_TAGBODY_POP,  0xA2, "TAGBODY_POP",  CL_OPND_NONE, 0)                /* Pop NLX tagbody frame (normal exit) */ \
    X(OP_TAGBODY_GO,   0xA3, "TAGBODY_GO",   CL_OPND_U16, CL_OPF_MVR|CL_OPF_UNCOND) /* Pop tag index, longjmp to tagbody */ \
    /* PROGV — runtime dynamic binding */ \
    X(OP_PROGV_BIND,   0xA4, "PROGV_BIND",   CL_OPND_NONE, 0)                /* Pop values+symbols lists, bind all */ \
    X(OP_PROGV_UNBIND, 0xA5, "PROGV_UNBIND", CL_OPND_NONE, 0)                /* Restore bindings, push result */ \
    X(OP_DEFSETF,      0xA6, "DEFSETF",      CL_OPND_U16_U16, 0)             /* Register setf mapping */ \
    X(OP_DEFVAR,       0xA7, "DEFVAR",       CL_OPND_U16, 0)                 /* Pop value; bind if unbound; mark special */ \
    X(OP_MV_RESET,     0xA8, "MV_RESET",     CL_OPND_NONE, CL_OPF_MVW)       /* cl_mv_count = 1 */ \
    /* AmigaOS FFI fast-path — see the operand layout note below */ \
    X(OP_AMIGA_CALL,   0xA9, "AMIGA_CALL",   CL_OPND_AMIGA, CL_OPF_MVW)      /* Direct library call via regspec */ \
    /* Inline %struct-ref / %struct-set fast-path */ \
    X(OP_STRUCT_REF,   0xAA, "STRUCT_REF",   CL_OPND_U8, CL_OPF_MVW)         /* Pop obj, push obj->slots[idx] */ \
    X(OP_STRUCT_SET,   0xAB, "STRUCT_SET",   CL_OPND_U8, CL_OPF_MVW)         /* Pop val+obj, slots[idx]=val, push val */ \
    X(OP_HALT,         0xFF, "HALT",         CL_OPND_NONE, CL_OPF_UNCOND)    /* Stop VM */

/*
 * OP_AMIGA_CALL operand layout: u16 base-sym-idx, i16 offset, u32 regspec,
 * u8 nargs.  Pops `nargs` values off the stack, loads them into the
 * registers named by the low 28 bits of regspec (one nibble per arg,
 * low-to-high; index 0..13 matches D0..D7,A0..A5 — same encoding as
 * CALL-LIBRARY-FAST).  Bit 28 of regspec is the void-p flag: when set, the
 * trampoline result is discarded and NIL is pushed instead of a
 * fixnum/bignum.  (Bit 28 is chosen over bit 31 so the regspec fits inside
 * a 30-bit fixnum literal.)
 *
 * The base symbol is looked up once via cl_symbol_value() and must hold a
 * foreign-pointer to the open library base.  Emitted by the compiler when
 * it sees (amiga:%ffi-call base-sym offset regspec args...).
 *
 * On non-AmigaOS builds the dispatch signals an error — the opcode is only
 * emitted when defcfun expands on Amiga, so this path is unreachable in
 * normal use.
 *
 * OP_STRUCT_REF/OP_STRUCT_SET: pop the struct (and value for SET),
 * bounds-check against n_slots, read/write slots[idx] with idx baked into
 * the operand stream as a u8 (0..255 covers any realistic defstruct).
 * Emitted for (clamiga::%struct-ref obj <fixnum>) etc. with a constant
 * index; the full builtins remain for dynamic indices.
 */

enum CL_Opcode {
#define CL_OPCODE_ENUM_ENTRY(name, value, str, opnd, flags) name = value,
    CL_OPCODE_LIST(CL_OPCODE_ENUM_ENTRY)
#undef CL_OPCODE_ENUM_ENTRY
    CL_OPCODE_ENUM_END = 0x100  /* sentinel — not an opcode */
};

/* Per-opcode decode info derived from CL_OPCODE_LIST (table in peephole.c).
 * cl_opcode_info() returns NULL for byte values that are not opcodes. */
typedef struct {
    const char *name;     /* mnemonic, e.g. "CONST" */
    uint8_t operands;     /* CL_OperandKind */
    uint8_t flags;        /* CL_OPF_* */
} CL_OpcodeInfo;

const CL_OpcodeInfo *cl_opcode_info(uint8_t op);

#endif /* CL_OPCODES_H */
