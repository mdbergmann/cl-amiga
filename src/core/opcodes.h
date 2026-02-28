#ifndef CL_OPCODES_H
#define CL_OPCODES_H

/*
 * Bytecode instruction set for the CL-Amiga VM.
 * Stack-based, byte-oriented encoding.
 */

enum CL_Opcode {
    /* Constants and variables */
    OP_CONST     = 0x01,  /* u16: Push constant from pool */
    OP_LOAD      = 0x02,  /* u8:  Push local variable */
    OP_STORE     = 0x03,  /* u8:  Store to local variable */
    OP_GLOAD     = 0x04,  /* u16: Push global (symbol value) */
    OP_GSTORE    = 0x05,  /* u16: Store global (symbol value) */
    OP_UPVAL     = 0x06,  /* u8: Load closed-over variable (flat upvalue index) */

    /* Stack manipulation */
    OP_POP       = 0x10,  /* Discard top of stack */
    OP_DUP       = 0x11,  /* Duplicate top of stack */

    /* List operations */
    OP_CONS      = 0x20,  /* Cons cell from top 2 values */
    OP_CAR       = 0x21,  /* Car of top value */
    OP_CDR       = 0x22,  /* Cdr of top value */

    /* Arithmetic */
    OP_ADD       = 0x30,  /* Addition */
    OP_SUB       = 0x31,  /* Subtraction */
    OP_MUL       = 0x32,  /* Multiplication */
    OP_DIV       = 0x33,  /* Division */

    /* Comparison */
    OP_EQ        = 0x40,  /* Equal (eql) */
    OP_LT        = 0x41,  /* Less than */
    OP_GT        = 0x42,  /* Greater than */
    OP_LE        = 0x43,  /* Less or equal */
    OP_GE        = 0x44,  /* Greater or equal */
    OP_NUMEQ     = 0x45,  /* Numeric equal (=) */

    /* Logic */
    OP_NOT       = 0x50,  /* Boolean not */

    /* Control flow */
    OP_JMP       = 0x60,  /* i16: Unconditional jump (relative) */
    OP_JNIL      = 0x61,  /* i16: Jump if top is NIL */
    OP_JTRUE     = 0x62,  /* i16: Jump if top is not NIL */

    /* Functions */
    OP_CALL      = 0x70,  /* u8: Call function with N args */
    OP_TAILCALL  = 0x71,  /* u8: Tail call with N args */
    OP_RET       = 0x72,  /* Return from function */
    OP_CLOSURE   = 0x73,  /* u16: Create closure from template (const index) */
    OP_APPLY     = 0x74,  /* Apply function to arg list */

    /* Misc */
    OP_LIST      = 0x80,  /* u8: Build list from N stack values */
    OP_NIL       = 0x81,  /* Push NIL */
    OP_T         = 0x82,  /* Push T */
    OP_FLOAD     = 0x83,  /* u16: Load function binding of symbol */
    OP_DEFMACRO  = 0x84,  /* u16: Register TOS as macro expander for symbol */
    OP_ARGC      = 0x86,  /* Push actual argument count as fixnum */
    OP_CATCH     = 0x85,  /* i16: Pop tag, push NLX catch frame, setjmp */
    OP_UNCATCH   = 0x87,  /* Pop NLX catch frame (normal exit) */
    OP_UWPROT    = 0x88,  /* i16: Push NLX uwprot frame, setjmp */
    OP_UWPOP     = 0x89,  /* Pop NLX uwprot frame (normal exit) */
    OP_UWRETHROW = 0x8A,  /* Re-throw pending if any, else nop */

    OP_HALT      = 0xFF   /* Stop VM */
};

/* Opcode argument sizes (for disassembly, etc.) */
#define OP_ARG_NONE  0
#define OP_ARG_U8    1
#define OP_ARG_U16   2
#define OP_ARG_I16   3
#define OP_ARG_U8U8  4

#endif /* CL_OPCODES_H */
