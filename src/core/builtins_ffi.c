/*
 * builtins_ffi.c — FFI (Foreign Function Interface) package builtins.
 *
 * Platform-independent foreign memory access: foreign pointers,
 * peek/poke, alloc/free, string conversion.
 */

#include "builtins.h"
#include "types.h"
#include "mem.h"
#include "error.h"
#include "symbol.h"
#include "package.h"
#include "bignum.h"
#include "float.h"
#include "string_utils.h"
#include "vm.h"
#include "../platform/platform.h"
#include "../platform/platform_thread.h"
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Integer <-> 64-bit C value conversion
 *
 * Foreign addresses and 64-bit integer arguments/results don't fit in a
 * 30-bit fixnum, so they round-trip through bignums (16-bit little-endian
 * limbs).  These helpers centralise that so every accessor agrees.
 * ================================================================ */

/* CL integer (fixnum or bignum) -> uint64_t (low 64 bits, two's complement). */
static uint64_t ffi_obj_to_u64(CL_Obj o)
{
    if (CL_FIXNUM_P(o))
        return (uint64_t)(int64_t)CL_FIXNUM_VAL(o);
    if (CL_BIGNUM_P(o)) {
        CL_Bignum *b = (CL_Bignum *)CL_OBJ_TO_PTR(o);
        uint64_t v = 0;
        uint32_t i;
        for (i = 0; i < b->length && i < 4; i++)
            v |= ((uint64_t)b->limbs[i]) << (16 * i);
        if (b->sign)
            v = (uint64_t)(-(int64_t)v);
        return v;
    }
    cl_error(CL_ERR_TYPE, "FFI: expected an integer");
    return 0;
}

/* uint64_t -> CL integer (fixnum if it fits, else normalized bignum). */
static CL_Obj ffi_u64_to_obj(uint64_t v)
{
    CL_Obj bn;
    CL_Bignum *b;
    if (v <= (uint64_t)CL_FIXNUM_MAX)
        return CL_MAKE_FIXNUM((int32_t)v);
    bn = cl_make_bignum(4, 0);
    b = (CL_Bignum *)CL_OBJ_TO_PTR(bn);
    b->limbs[0] = (uint16_t)(v & 0xFFFF);
    b->limbs[1] = (uint16_t)((v >> 16) & 0xFFFF);
    b->limbs[2] = (uint16_t)((v >> 32) & 0xFFFF);
    b->limbs[3] = (uint16_t)((v >> 48) & 0xFFFF);
    return cl_bignum_normalize(bn);
}

/* int64_t -> CL integer (signed). */
static CL_Obj ffi_i64_to_obj(int64_t v)
{
    uint64_t mag;
    CL_Obj bn;
    CL_Bignum *b;
    if (v >= (int64_t)CL_FIXNUM_MIN && v <= (int64_t)CL_FIXNUM_MAX)
        return CL_MAKE_FIXNUM((int32_t)v);
    mag = (v < 0) ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;
    bn = cl_make_bignum(4, v < 0 ? 1 : 0);
    b = (CL_Bignum *)CL_OBJ_TO_PTR(bn);
    b->limbs[0] = (uint16_t)(mag & 0xFFFF);
    b->limbs[1] = (uint16_t)((mag >> 16) & 0xFFFF);
    b->limbs[2] = (uint16_t)((mag >> 32) & 0xFFFF);
    b->limbs[3] = (uint16_t)((mag >> 48) & 0xFFFF);
    return cl_bignum_normalize(bn);
}

/* ================================================================
 * Helper: register function in FFI package and export
 * ================================================================ */

static void ffi_defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_ffi);
    CL_Obj fn;
    CL_Symbol *s;
    CL_GC_PROTECT(sym);
    fn = cl_make_function(func, sym, min, max);
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    cl_export_symbol(sym, cl_package_ffi);
    CL_GC_UNPROTECT(1);
}

/* ================================================================
 * Foreign pointer builtins
 * ================================================================ */

/* (ffi:make-foreign-pointer address &optional size) → foreign-pointer
 *
 * ADDRESS is a *real* machine address (the value seen by
 * FFI:FOREIGN-POINTER-ADDRESS / CFFI POINTER-ADDRESS), which on a 64-bit
 * host does not fit in a CL_ForeignPtr handle.  We register the real pointer
 * so it round-trips through a handle.  The result is unowned (size is advisory
 * only and not used to free anything). */
static CL_Obj bi_ffi_make_fp(CL_Obj *args, int nargs)
{
    uint64_t addr;
    uint32_t size, handle;
    (void)size;
    if (!CL_FIXNUM_P(args[0]) && !CL_BIGNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "FFI:MAKE-FOREIGN-POINTER: address must be an integer");
    addr = ffi_obj_to_u64(args[0]);
    size = (nargs > 1 && !CL_NULL_P(args[1])) ? (uint32_t)CL_FIXNUM_VAL(args[1]) : 0;
    handle = platform_ffi_register((void *)(uintptr_t)addr);
    return cl_make_foreign_pointer(handle, size, 0);
}

/* (ffi:foreign-pointer-address fp) → integer
 * Returns the *real* address (resolved through the side table on POSIX). */
static CL_Obj bi_ffi_fp_address(CL_Obj *args, int nargs)
{
    CL_ForeignPtr *fp;
    void *real;
    (void)nargs;
    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE, "FFI:FOREIGN-POINTER-ADDRESS: argument must be a foreign pointer");
    fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]);
    real = platform_ffi_resolve(fp->address);
    return ffi_u64_to_obj((uint64_t)(uintptr_t)real);
}

/* (ffi:foreign-pointer-p obj) → T or NIL */
static CL_Obj bi_ffi_fp_p(CL_Obj *args, int nargs)
{
    (void)nargs;
    return CL_FOREIGN_POINTER_P(args[0]) ? CL_T : CL_NIL;
}

/* (ffi:null-pointer-p fp) → T or NIL.  True when the real address is 0. */
static CL_Obj bi_ffi_null_p(CL_Obj *args, int nargs)
{
    CL_ForeignPtr *fp;
    (void)nargs;
    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE, "FFI:NULL-POINTER-P: argument must be a foreign pointer");
    fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]);
    if (fp->address == 0) return CL_T;
    return (platform_ffi_resolve(fp->address) == NULL) ? CL_T : CL_NIL;
}

/* (ffi:pointer-eq a b) → T or NIL.  Compares real addresses. */
static CL_Obj bi_ffi_pointer_eq(CL_Obj *args, int nargs)
{
    CL_ForeignPtr *a, *b;
    (void)nargs;
    if (!CL_FOREIGN_POINTER_P(args[0]) || !CL_FOREIGN_POINTER_P(args[1]))
        cl_error(CL_ERR_TYPE, "FFI:POINTER-EQ: arguments must be foreign pointers");
    a = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]);
    b = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[1]);
    return (platform_ffi_resolve(a->address) == platform_ffi_resolve(b->address))
           ? CL_T : CL_NIL;
}

/* (ffi:alloc-foreign size) → foreign-pointer */
static CL_Obj bi_ffi_alloc(CL_Obj *args, int nargs)
{
    uint32_t size, handle;
    (void)nargs;
    if (!CL_FIXNUM_P(args[0]) || CL_FIXNUM_VAL(args[0]) <= 0)
        cl_error(CL_ERR_ARGS, "FFI:ALLOC-FOREIGN: size must be a positive integer");
    size = (uint32_t)CL_FIXNUM_VAL(args[0]);
    handle = platform_ffi_alloc(size);
    if (handle == 0)
        cl_error(CL_ERR_GENERAL, "FFI:ALLOC-FOREIGN: allocation of %u bytes failed",
                 (unsigned)size);
    return cl_make_foreign_pointer(handle, size, CL_FPTR_FLAG_OWNED);
}

/* (ffi:free-foreign fp) → T */
static CL_Obj bi_ffi_free(CL_Obj *args, int nargs)
{
    CL_ForeignPtr *fp;
    (void)nargs;
    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE, "FFI:FREE-FOREIGN: argument must be a foreign pointer");
    fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]);
    if (!(fp->flags & CL_FPTR_FLAG_OWNED))
        cl_error(CL_ERR_GENERAL, "FFI:FREE-FOREIGN: pointer was not allocated by FFI");
    platform_ffi_free(fp->address, fp->size);
    fp->address = 0;
    fp->flags &= (uint8_t)~CL_FPTR_FLAG_OWNED;
    return CL_T;
}

/* ================================================================
 * Peek/Poke builtins
 *
 * (ffi:peek-u32 fp &optional offset) → integer
 * (ffi:poke-u32 fp value &optional offset) → value
 * ================================================================ */

/* Helper: extract address handle from foreign-pointer arg */
static uint32_t ffi_get_handle(CL_Obj arg)
{
    CL_ForeignPtr *fp;
    if (!CL_FOREIGN_POINTER_P(arg))
        cl_error(CL_ERR_TYPE, "FFI: argument must be a foreign pointer");
    fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(arg);
    return fp->address;
}

/* Helper: resolve (foreign-pointer arg + optional byte offset) to a real,
 * dereferenceable byte address.  OFFSET_IDX is the argument index of the
 * optional offset (used by the typed peek/poke accessors below). */
static uint8_t *ffi_resolve_at(CL_Obj *args, int nargs, int offset_idx)
{
    void *base = platform_ffi_resolve(ffi_get_handle(args[0]));
    uint32_t offset;
    if (!base)
        cl_error(CL_ERR_GENERAL, "FFI: dereference of an invalid/null foreign pointer");
    offset = (nargs > offset_idx && !CL_NULL_P(args[offset_idx]))
             ? (uint32_t)CL_FIXNUM_VAL(args[offset_idx]) : 0;
    return (uint8_t *)base + offset;
}

/* (ffi:peek-u32 fp &optional offset) */
static CL_Obj bi_ffi_peek32(CL_Obj *args, int nargs)
{
    uint32_t handle = ffi_get_handle(args[0]);
    uint32_t offset = (nargs > 1 && !CL_NULL_P(args[1]))
                      ? (uint32_t)CL_FIXNUM_VAL(args[1]) : 0;
    uint32_t val = platform_ffi_peek32(handle, offset);
    if (val <= (uint32_t)CL_FIXNUM_MAX)
        return CL_MAKE_FIXNUM((int32_t)val);
    else {
        CL_Obj bn = cl_make_bignum(2, 0);
        CL_Bignum *b = (CL_Bignum *)CL_OBJ_TO_PTR(bn);
        b->limbs[0] = (uint16_t)(val & 0xFFFF);
        b->limbs[1] = (uint16_t)(val >> 16);
        return bn;
    }
}

/* (ffi:peek-u16 fp &optional offset) */
static CL_Obj bi_ffi_peek16(CL_Obj *args, int nargs)
{
    uint32_t handle = ffi_get_handle(args[0]);
    uint32_t offset = (nargs > 1 && !CL_NULL_P(args[1]))
                      ? (uint32_t)CL_FIXNUM_VAL(args[1]) : 0;
    uint16_t val = platform_ffi_peek16(handle, offset);
    return CL_MAKE_FIXNUM((int32_t)val);
}

/* (ffi:peek-u8 fp &optional offset) */
static CL_Obj bi_ffi_peek8(CL_Obj *args, int nargs)
{
    uint32_t handle = ffi_get_handle(args[0]);
    uint32_t offset = (nargs > 1 && !CL_NULL_P(args[1]))
                      ? (uint32_t)CL_FIXNUM_VAL(args[1]) : 0;
    uint8_t val = platform_ffi_peek8(handle, offset);
    return CL_MAKE_FIXNUM((int32_t)val);
}

/* (ffi:poke-u32 fp value &optional offset) */
static CL_Obj bi_ffi_poke32(CL_Obj *args, int nargs)
{
    uint32_t handle = ffi_get_handle(args[0]);
    uint32_t val;
    uint32_t offset = (nargs > 2 && !CL_NULL_P(args[2]))
                      ? (uint32_t)CL_FIXNUM_VAL(args[2]) : 0;
    if (CL_FIXNUM_P(args[1]))
        val = (uint32_t)CL_FIXNUM_VAL(args[1]);
    else if (CL_BIGNUM_P(args[1])) {
        CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(args[1]);
        val = (uint32_t)bn->limbs[0];
        if (bn->length > 1)
            val |= ((uint32_t)bn->limbs[1]) << 16;
    } else
        cl_error(CL_ERR_TYPE, "FFI:POKE-U32: value must be an integer");
    platform_ffi_poke32(handle, offset, val);
    return args[1];
}

/* (ffi:poke-u16 fp value &optional offset) */
static CL_Obj bi_ffi_poke16(CL_Obj *args, int nargs)
{
    uint32_t handle = ffi_get_handle(args[0]);
    uint32_t offset = (nargs > 2 && !CL_NULL_P(args[2]))
                      ? (uint32_t)CL_FIXNUM_VAL(args[2]) : 0;
    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "FFI:POKE-U16: value must be a fixnum");
    platform_ffi_poke16(handle, offset, (uint16_t)CL_FIXNUM_VAL(args[1]));
    return args[1];
}

/* (ffi:poke-u8 fp value &optional offset) */
static CL_Obj bi_ffi_poke8(CL_Obj *args, int nargs)
{
    uint32_t handle = ffi_get_handle(args[0]);
    uint32_t offset = (nargs > 2 && !CL_NULL_P(args[2]))
                      ? (uint32_t)CL_FIXNUM_VAL(args[2]) : 0;
    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "FFI:POKE-U8: value must be a fixnum");
    platform_ffi_poke8(handle, offset, (uint8_t)CL_FIXNUM_VAL(args[1]));
    return args[1];
}

/* ================================================================
 * Bulk byte transfer
 *
 * POKE-U8 in a Lisp loop costs a VM dispatch, a handle resolve and a
 * fixnum unbox per byte, which dominates any path that pushes a real
 * buffer into foreign memory (bitplane rows, chip-RAM masks).  These
 * move a whole span in one call.
 *
 * A CL string holds its bytes contiguously, so it memcpys; a general
 * vector holds one tagged CL_Obj per element (clamiga upgrades
 * (UNSIGNED-BYTE 8) to T — see UPGRADED-ARRAY-ELEMENT-TYPE), so it
 * unpacks in a tight C loop instead.  Both beat per-byte POKE-U8; only
 * the string form reaches memcpy speed.
 * ================================================================ */

/* Resolve the (SOURCE, START, END) span of a byte-source argument.
 * Returns the element count, and sets whichever of SVEC, SSTR and SBV
 * matches SOURCE's representation (exactly one is left non-NULL).  A packed
 * byte vector (SBV) is the ideal representation here — the copy in/out of
 * foreign memory degenerates to a single memcpy. */
static uint32_t ffi_byte_span(CL_Obj source, CL_Obj start_arg, CL_Obj end_arg,
                              const char *who, CL_Vector **svec, CL_String **sstr,
                              CL_ByteVector **sbv, uint32_t *start_out)
{
    uint32_t len, start, end;

    *svec = NULL;
    *sstr = NULL;
    *sbv = NULL;

    if (CL_STRING_P(source)) {
        *sstr = (CL_String *)CL_OBJ_TO_PTR(source);
        len = (*sstr)->length;
    } else if (CL_BYTE_VECTOR_P(source)) {
        *sbv = (CL_ByteVector *)CL_OBJ_TO_PTR(source);
        len = cl_bytevec_active_length(*sbv);
    } else if (CL_VECTOR_P(source)) {
        *svec = (CL_Vector *)CL_OBJ_TO_PTR(source);
        if ((*svec)->rank > 1)
            cl_error(CL_ERR_TYPE, "%s: source must be a vector, not a rank-%d array",
                     who, (int)(*svec)->rank);
        len = cl_vector_active_length(*svec);
    } else {
        cl_error(CL_ERR_TYPE, "%s: source must be a vector or a string", who);
        return 0;
    }

    start = CL_NULL_P(start_arg) ? 0 : (uint32_t)CL_FIXNUM_VAL(start_arg);
    end   = CL_NULL_P(end_arg)   ? len : (uint32_t)CL_FIXNUM_VAL(end_arg);
    if (!CL_NULL_P(start_arg) && !CL_FIXNUM_P(start_arg))
        cl_error(CL_ERR_TYPE, "%s: START must be a fixnum", who);
    if (!CL_NULL_P(end_arg) && !CL_FIXNUM_P(end_arg))
        cl_error(CL_ERR_TYPE, "%s: END must be a fixnum", who);
    if (end > len)
        cl_error(CL_ERR_GENERAL, "%s: END %u is past the end of a %u-element source",
                 who, (unsigned)end, (unsigned)len);
    if (start > end)
        cl_error(CL_ERR_GENERAL, "%s: START %u is past END %u",
                 who, (unsigned)start, (unsigned)end);

    *start_out = start;
    return end - start;
}

/* (ffi:poke-bytes fp source &optional offset start end) → count
 *
 * Copies SOURCE[START..END) into foreign memory at FP + OFFSET.  SOURCE is
 * a string (memcpy) or a vector of (INTEGER 0 255).  Returns the number of
 * bytes written. */
static CL_Obj bi_ffi_poke_bytes(CL_Obj *args, int nargs)
{
    CL_ForeignPtr *fp;
    CL_Vector *svec;
    CL_String *sstr;
    CL_ByteVector *sbv;
    uint8_t *dest;
    void *base;
    uint32_t fp_size, fp_addr;
    uint32_t offset, start, count, i;

    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE, "FFI:POKE-BYTES: first argument must be a foreign pointer");
    /* Copy the fields out immediately: FP is a raw heap pointer, and no raw
     * heap pointer may stay live across anything that might allocate. */
    fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]);
    fp_size = fp->size;
    fp_addr = fp->address;

    if (nargs > 2 && !CL_NULL_P(args[2])) {
        if (!CL_FIXNUM_P(args[2]))
            cl_error(CL_ERR_TYPE, "FFI:POKE-BYTES: OFFSET must be a fixnum");
        offset = (uint32_t)CL_FIXNUM_VAL(args[2]);
    } else
        offset = 0;

    count = ffi_byte_span(args[1],
                          (nargs > 3) ? args[3] : CL_NIL,
                          (nargs > 4) ? args[4] : CL_NIL,
                          "FFI:POKE-BYTES", &svec, &sstr, &sbv, &start);

    /* A known allocation size lets us reject an overrun here rather than
     * corrupting whatever sits past the buffer.  size 0 = external memory
     * of unknown extent, so we have nothing to check against. */
    if (fp_size > 0 && (offset > fp_size || count > fp_size - offset))
        cl_error(CL_ERR_GENERAL,
                 "FFI:POKE-BYTES: writing %u bytes at offset %u overruns a %u-byte buffer",
                 (unsigned)count, (unsigned)offset, (unsigned)fp_size);

    base = platform_ffi_resolve(fp_addr);
    if (!base)
        cl_error(CL_ERR_GENERAL, "FFI:POKE-BYTES: invalid/null foreign pointer");
    dest = (uint8_t *)base + offset;

    /* Re-derive the source pointer from the (GC-rooted) argument now that
     * every check is done, rather than trusting the one FFI_BYTE_SPAN
     * produced earlier.  No allocation happens from here on, so neither
     * DEST nor the source data can be invalidated by a compaction mid-copy. */
    if (sstr) {
        sstr = (CL_String *)CL_OBJ_TO_PTR(args[1]);
    } else if (sbv) {
        sbv = (CL_ByteVector *)CL_OBJ_TO_PTR(args[1]);
    } else {
        svec = (CL_Vector *)CL_OBJ_TO_PTR(args[1]);
    }

    if (sstr) {
        memcpy(dest, sstr->data + start, count);
    } else if (sbv) {
        /* Packed byte vector: raw bytes straight into foreign memory. */
        memcpy(dest, sbv->data + start, count);
    } else {
        CL_Obj *elts = cl_vector_data(svec) + start;
        for (i = 0; i < count; i++) {
            CL_Obj e = elts[i];
            int32_t v;
            if (!CL_FIXNUM_P(e))
                cl_error(CL_ERR_TYPE,
                         "FFI:POKE-BYTES: element %u is not an integer",
                         (unsigned)(start + i));
            v = CL_FIXNUM_VAL(e);
            if (v < 0 || v > 255)
                cl_error(CL_ERR_TYPE,
                         "FFI:POKE-BYTES: element %u is %d, not in [0,255]",
                         (unsigned)(start + i), (int)v);
            dest[i] = (uint8_t)v;
        }
    }
    return CL_MAKE_FIXNUM((int32_t)count);
}

/* (ffi:peek-bytes fp vector &optional offset start end) → count
 *
 * The inverse: fills VECTOR[START..END) from foreign memory at FP + OFFSET
 * with fixnum byte values.  Returns the number of bytes read. */
static CL_Obj bi_ffi_peek_bytes(CL_Obj *args, int nargs)
{
    CL_ForeignPtr *fp;
    CL_Vector *svec;
    CL_String *sstr;
    CL_ByteVector *sbv;
    const uint8_t *src;
    void *base;
    uint32_t fp_size, fp_addr;
    uint32_t offset, start, count, i;

    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE, "FFI:PEEK-BYTES: first argument must be a foreign pointer");
    /* See FFI:POKE-BYTES: copy the fields out before anything may allocate. */
    fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]);
    fp_size = fp->size;
    fp_addr = fp->address;

    if (nargs > 2 && !CL_NULL_P(args[2])) {
        if (!CL_FIXNUM_P(args[2]))
            cl_error(CL_ERR_TYPE, "FFI:PEEK-BYTES: OFFSET must be a fixnum");
        offset = (uint32_t)CL_FIXNUM_VAL(args[2]);
    } else
        offset = 0;

    count = ffi_byte_span(args[1],
                          (nargs > 3) ? args[3] : CL_NIL,
                          (nargs > 4) ? args[4] : CL_NIL,
                          "FFI:PEEK-BYTES", &svec, &sstr, &sbv, &start);

    if (fp_size > 0 && (offset > fp_size || count > fp_size - offset))
        cl_error(CL_ERR_GENERAL,
                 "FFI:PEEK-BYTES: reading %u bytes at offset %u overruns a %u-byte buffer",
                 (unsigned)count, (unsigned)offset, (unsigned)fp_size);

    base = platform_ffi_resolve(fp_addr);
    if (!base)
        cl_error(CL_ERR_GENERAL, "FFI:PEEK-BYTES: invalid/null foreign pointer");
    src = (const uint8_t *)base + offset;

    /* Re-derive from the GC-rooted argument; no allocation from here on. */
    if (sstr) {
        sstr = (CL_String *)CL_OBJ_TO_PTR(args[1]);
    } else if (sbv) {
        sbv = (CL_ByteVector *)CL_OBJ_TO_PTR(args[1]);
    } else {
        svec = (CL_Vector *)CL_OBJ_TO_PTR(args[1]);
    }

    if (sstr) {
        memcpy(sstr->data + start, src, count);
    } else if (sbv) {
        /* Packed byte vector: raw bytes straight out of foreign memory. */
        memcpy(sbv->data + start, src, count);
    } else {
        CL_Obj *elts = cl_vector_data(svec) + start;
        for (i = 0; i < count; i++)
            elts[i] = CL_MAKE_FIXNUM((int32_t)src[i]);
    }
    return CL_MAKE_FIXNUM((int32_t)count);
}

/* ================================================================
 * String conversion builtins
 * ================================================================ */

/* (ffi:foreign-string str) → foreign-pointer
 * Copies a CL string to null-terminated foreign memory. */
static CL_Obj bi_ffi_foreign_string(CL_Obj *args, int nargs)
{
    CL_String *s;
    uint32_t handle;
    void *dest;
    (void)nargs;
    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "FFI:FOREIGN-STRING: argument must be a string");
    s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    handle = platform_ffi_alloc(s->length + 1);
    if (handle == 0)
        cl_error(CL_ERR_GENERAL, "FFI:FOREIGN-STRING: allocation failed");
    dest = platform_ffi_resolve(handle);
    if (dest) {
        memcpy(dest, s->data, s->length);
        ((char *)dest)[s->length] = '\0';
    }
    return cl_make_foreign_pointer(handle, s->length + 1, CL_FPTR_FLAG_OWNED);
}

/* (ffi:foreign-to-string fp &optional max-len) → string
 * Reads a null-terminated string from foreign memory. */
static CL_Obj bi_ffi_foreign_to_string(CL_Obj *args, int nargs)
{
    uint32_t handle;
    void *base;
    const char *cstr;
    uint32_t len, maxlen;

    handle = ffi_get_handle(args[0]);
    base = platform_ffi_resolve(handle);
    if (!base)
        cl_error(CL_ERR_GENERAL, "FFI:FOREIGN-TO-STRING: invalid foreign pointer");
    cstr = (const char *)base;
    maxlen = (nargs > 1 && !CL_NULL_P(args[1]))
             ? (uint32_t)CL_FIXNUM_VAL(args[1]) : 4096;
    for (len = 0; len < maxlen && cstr[len] != '\0'; len++)
        ;
    return cl_make_string(cstr, len);
}

/* ================================================================
 * Pointer arithmetic
 * ================================================================ */

/* (ffi:pointer+ fp offset) → foreign-pointer
 * Returns a new (unowned) foreign pointer at the real address of FP + OFFSET. */
static CL_Obj bi_ffi_pointer_plus(CL_Obj *args, int nargs)
{
    CL_ForeignPtr *fp;
    int32_t offset;
    uint8_t *real;
    uint32_t handle;
    (void)nargs;
    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE, "FFI:POINTER+: first argument must be a foreign pointer");
    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "FFI:POINTER+: offset must be a fixnum");
    fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]);
    offset = CL_FIXNUM_VAL(args[1]);
    real = (uint8_t *)platform_ffi_resolve(fp->address);
    handle = platform_ffi_register(real + offset);
    return cl_make_foreign_pointer(handle, 0, 0);
}

/* ================================================================
 * Typed peek/poke — signed, 64-bit, float/double, and pointer access.
 *
 * These resolve the foreign pointer to a real address and do a typed
 * load/store there.  Unaligned access is permitted (68020+ tolerates it,
 * as does every host CPU we target); the existing Amiga peek/poke already
 * relies on this.  Used by CFFI's %MEM-REF / %MEM-SET.
 * ================================================================ */

/* (ffi:peek-i8 fp &optional offset) → signed integer */
static CL_Obj bi_ffi_peek_i8(CL_Obj *args, int nargs)
{
    return CL_MAKE_FIXNUM((int32_t)*(int8_t *)ffi_resolve_at(args, nargs, 1));
}

/* (ffi:peek-i16 fp &optional offset) → signed integer */
static CL_Obj bi_ffi_peek_i16(CL_Obj *args, int nargs)
{
    int16_t v;
    memcpy(&v, ffi_resolve_at(args, nargs, 1), sizeof(v));
    return CL_MAKE_FIXNUM((int32_t)v);
}

/* (ffi:peek-i32 fp &optional offset) → signed integer */
static CL_Obj bi_ffi_peek_i32(CL_Obj *args, int nargs)
{
    int32_t v;
    memcpy(&v, ffi_resolve_at(args, nargs, 1), sizeof(v));
    return cl_bignum_normalize(cl_bignum_from_int32(v));
}

/* (ffi:peek-u64 fp &optional offset) → unsigned integer */
static CL_Obj bi_ffi_peek_u64(CL_Obj *args, int nargs)
{
    uint64_t v;
    memcpy(&v, ffi_resolve_at(args, nargs, 1), sizeof(v));
    return ffi_u64_to_obj(v);
}

/* (ffi:peek-i64 fp &optional offset) → signed integer */
static CL_Obj bi_ffi_peek_i64(CL_Obj *args, int nargs)
{
    int64_t v;
    memcpy(&v, ffi_resolve_at(args, nargs, 1), sizeof(v));
    return ffi_i64_to_obj(v);
}

/* (ffi:peek-single fp &optional offset) → single-float */
static CL_Obj bi_ffi_peek_single(CL_Obj *args, int nargs)
{
    float v;
    memcpy(&v, ffi_resolve_at(args, nargs, 1), sizeof(v));
    return cl_make_single_float(v);
}

/* (ffi:peek-double fp &optional offset) → double-float */
static CL_Obj bi_ffi_peek_double(CL_Obj *args, int nargs)
{
    double v;
    memcpy(&v, ffi_resolve_at(args, nargs, 1), sizeof(v));
    return cl_make_double_float(v);
}

/* (ffi:peek-pointer fp &optional offset) → foreign-pointer
 * Reads a machine pointer from memory and wraps it (unowned). */
static CL_Obj bi_ffi_peek_pointer(CL_Obj *args, int nargs)
{
    void *v;
    memcpy(&v, ffi_resolve_at(args, nargs, 1), sizeof(v));
    return cl_make_foreign_pointer(platform_ffi_register(v), 0, 0);
}

/* (ffi:poke-i8 fp value &optional offset) → value */
static CL_Obj bi_ffi_poke_i8(CL_Obj *args, int nargs)
{
    int8_t v = (int8_t)ffi_obj_to_u64(args[1]);
    *(int8_t *)ffi_resolve_at(args, nargs, 2) = v;
    return args[1];
}

/* (ffi:poke-i16 fp value &optional offset) → value */
static CL_Obj bi_ffi_poke_i16(CL_Obj *args, int nargs)
{
    int16_t v = (int16_t)ffi_obj_to_u64(args[1]);
    memcpy(ffi_resolve_at(args, nargs, 2), &v, sizeof(v));
    return args[1];
}

/* (ffi:poke-i32 fp value &optional offset) → value */
static CL_Obj bi_ffi_poke_i32(CL_Obj *args, int nargs)
{
    int32_t v = (int32_t)ffi_obj_to_u64(args[1]);
    memcpy(ffi_resolve_at(args, nargs, 2), &v, sizeof(v));
    return args[1];
}

/* (ffi:poke-u64 fp value &optional offset) → value */
static CL_Obj bi_ffi_poke_u64(CL_Obj *args, int nargs)
{
    uint64_t v = ffi_obj_to_u64(args[1]);
    memcpy(ffi_resolve_at(args, nargs, 2), &v, sizeof(v));
    return args[1];
}

/* (ffi:poke-i64 fp value &optional offset) → value (alias of poke-u64 bits) */
static CL_Obj bi_ffi_poke_i64(CL_Obj *args, int nargs)
{
    uint64_t v = ffi_obj_to_u64(args[1]);
    memcpy(ffi_resolve_at(args, nargs, 2), &v, sizeof(v));
    return args[1];
}

/* (ffi:poke-single fp value &optional offset) → value */
static CL_Obj bi_ffi_poke_single(CL_Obj *args, int nargs)
{
    float v;
    if (!CL_REALP(args[1]))
        cl_error(CL_ERR_TYPE, "FFI:POKE-SINGLE: value must be a real number");
    v = cl_to_float(args[1]);
    memcpy(ffi_resolve_at(args, nargs, 2), &v, sizeof(v));
    return args[1];
}

/* (ffi:poke-double fp value &optional offset) → value */
static CL_Obj bi_ffi_poke_double(CL_Obj *args, int nargs)
{
    double v;
    if (!CL_REALP(args[1]))
        cl_error(CL_ERR_TYPE, "FFI:POKE-DOUBLE: value must be a real number");
    v = cl_to_double(args[1]);
    memcpy(ffi_resolve_at(args, nargs, 2), &v, sizeof(v));
    return args[1];
}

/* (ffi:poke-pointer fp value &optional offset) → value
 * VALUE is a foreign pointer; its real address is written to memory. */
static CL_Obj bi_ffi_poke_pointer(CL_Obj *args, int nargs)
{
    void *v;
    if (!CL_FOREIGN_POINTER_P(args[1]))
        cl_error(CL_ERR_TYPE, "FFI:POKE-POINTER: value must be a foreign pointer");
    v = platform_ffi_resolve(((CL_ForeignPtr *)CL_OBJ_TO_PTR(args[1]))->address);
    memcpy(ffi_resolve_at(args, nargs, 2), &v, sizeof(v));
    return args[1];
}

/* ================================================================
 * Dynamic libraries + foreign function calls (host: dlopen + libffi)
 * ================================================================ */

/* Interned primitive type keywords, indexed by CLFFIType (filled in init).
 * CALL-FOREIGN accepts these; the CFFI backend maps its richer type set
 * down to them. */
static CL_Obj ffi_type_keywords[CL_FFI_POINTER + 1];

/* Copy a CL string (base or wide) into BUF as a NUL-terminated C string.
 * Library/symbol names are short ASCII; non-ASCII code points are truncated
 * to a byte.  Errors if ARG is not a string. */
static const char *ffi_string_to_cstr(CL_Obj arg, char *buf, uint32_t bufsize)
{
    uint32_t len, i, n;
    if (!CL_ANY_STRING_P(arg))
        cl_error(CL_ERR_TYPE, "FFI: expected a string");
    len = cl_string_length(arg);
    n = (len < bufsize - 1) ? len : bufsize - 1;
    for (i = 0; i < n; i++)
        buf[i] = (char)cl_string_char_at(arg, i);
    buf[n] = '\0';
    return buf;
}

/* Map a (presumed keyword) CL_Obj to a CLFFIType, or signal. */
static CLFFIType ffi_kw_to_type(CL_Obj kw)
{
    int i;
    for (i = 0; i <= (int)CL_FFI_POINTER; i++)
        if (ffi_type_keywords[i] == kw)
            return (CLFFIType)i;
    cl_error(CL_ERR_TYPE,
             "FFI:CALL-FOREIGN: unknown foreign type (expected one of :VOID "
             ":INT8 :UINT8 :INT16 :UINT16 :INT32 :UINT32 :INT64 :UINT64 "
             ":FLOAT :DOUBLE :POINTER)");
    return CL_FFI_VOID;
}

/* Extract the C value for one argument of type T from CL value V. */
static void ffi_marshal_arg(CLFFIType t, CL_Obj v, CLFFIValue *out)
{
    switch (t) {
    case CL_FFI_I8:   out->i8  = (int8_t)ffi_obj_to_u64(v); break;
    case CL_FFI_U8:   out->u8  = (uint8_t)ffi_obj_to_u64(v); break;
    case CL_FFI_I16:  out->i16 = (int16_t)ffi_obj_to_u64(v); break;
    case CL_FFI_U16:  out->u16 = (uint16_t)ffi_obj_to_u64(v); break;
    case CL_FFI_I32:  out->i32 = (int32_t)ffi_obj_to_u64(v); break;
    case CL_FFI_U32:  out->u32 = (uint32_t)ffi_obj_to_u64(v); break;
    case CL_FFI_I64:  out->i64 = (int64_t)ffi_obj_to_u64(v); break;
    case CL_FFI_U64:  out->u64 = ffi_obj_to_u64(v); break;
    case CL_FFI_FLOAT:
        if (!CL_REALP(v))
            cl_error(CL_ERR_TYPE, "FFI:CALL-FOREIGN: :FLOAT argument must be a real number");
        out->f = cl_to_float(v); break;
    case CL_FFI_DOUBLE:
        if (!CL_REALP(v))
            cl_error(CL_ERR_TYPE, "FFI:CALL-FOREIGN: :DOUBLE argument must be a real number");
        out->d = cl_to_double(v); break;
    case CL_FFI_POINTER:
        if (CL_NULL_P(v))
            out->p = NULL;
        else if (CL_FOREIGN_POINTER_P(v))
            out->p = platform_ffi_resolve(((CL_ForeignPtr *)CL_OBJ_TO_PTR(v))->address);
        else if (CL_INTEGER_P(v))
            out->p = (void *)(uintptr_t)ffi_obj_to_u64(v);
        else
            cl_error(CL_ERR_TYPE, "FFI:CALL-FOREIGN: :POINTER argument must be a foreign pointer, integer, or NIL");
        break;
    case CL_FFI_VOID:
        cl_error(CL_ERR_TYPE, "FFI:CALL-FOREIGN: :VOID is not a valid argument type");
        break;
    }
}

/* Box a foreign return value of type T into a CL object. */
static CL_Obj ffi_box_result(CLFFIType t, CLFFIValue *r)
{
    switch (t) {
    case CL_FFI_VOID:    return CL_NIL;
    case CL_FFI_I8:      return CL_MAKE_FIXNUM((int32_t)r->i8);
    case CL_FFI_U8:      return CL_MAKE_FIXNUM((int32_t)r->u8);
    case CL_FFI_I16:     return CL_MAKE_FIXNUM((int32_t)r->i16);
    case CL_FFI_U16:     return CL_MAKE_FIXNUM((int32_t)r->u16);
    case CL_FFI_I32:     return ffi_i64_to_obj((int64_t)r->i32);
    case CL_FFI_U32:     return ffi_u64_to_obj((uint64_t)r->u32);
    case CL_FFI_I64:     return ffi_i64_to_obj(r->i64);
    case CL_FFI_U64:     return ffi_u64_to_obj(r->u64);
    case CL_FFI_FLOAT:   return cl_make_single_float(r->f);
    case CL_FFI_DOUBLE:  return cl_make_double_float(r->d);
    case CL_FFI_POINTER: return cl_make_foreign_pointer(platform_ffi_register(r->p), 0, 0);
    }
    return CL_NIL;
}

/* (ffi:load-library name) → foreign-pointer (library handle) or NIL.
 * NAME may be NIL to obtain a handle to the global/default symbol namespace. */
static CL_Obj bi_ffi_load_library(CL_Obj *args, int nargs)
{
    char buf[1024];
    const char *name = NULL;
    uint32_t h;
    (void)nargs;
    if (!CL_NULL_P(args[0]))
        name = ffi_string_to_cstr(args[0], buf, sizeof(buf));
    h = platform_ffi_dlopen(name);
    if (h == 0) return CL_NIL;
    return cl_make_foreign_pointer(h, 0, 0);
}

/* (ffi:close-library lib) → T */
static CL_Obj bi_ffi_close_library(CL_Obj *args, int nargs)
{
    (void)nargs;
    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE, "FFI:CLOSE-LIBRARY: argument must be a library handle");
    platform_ffi_dlclose(((CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]))->address);
    return CL_T;
}

/* (ffi:symbol-pointer name &optional lib) → foreign-pointer or NIL.
 * LIB is a library handle from LOAD-LIBRARY; NIL searches the default
 * namespace. */
static CL_Obj bi_ffi_symbol_pointer(CL_Obj *args, int nargs)
{
    char buf[256];
    const char *name;
    uint32_t libh = 0, h;
    if (nargs > 1 && !CL_NULL_P(args[1])) {
        if (!CL_FOREIGN_POINTER_P(args[1]))
            cl_error(CL_ERR_TYPE, "FFI:SYMBOL-POINTER: LIB must be a library handle or NIL");
        libh = ((CL_ForeignPtr *)CL_OBJ_TO_PTR(args[1]))->address;
    }
    name = ffi_string_to_cstr(args[0], buf, sizeof(buf));
    h = platform_ffi_dlsym(libh, name);
    if (h == 0) return CL_NIL;
    return cl_make_foreign_pointer(h, 0, 0);
}

/* (ffi:call-foreign fn-ptr ret-type arg-types arg-values &optional n-fixed)
 *   FN-PTR     foreign pointer to the C function
 *   RET-TYPE   one primitive type keyword (or :VOID)
 *   ARG-TYPES  list of primitive type keywords
 *   ARG-VALUES list of CL values, parallel to ARG-TYPES
 *   N-FIXED    for variadic calls, the count of fixed (non-vararg) args;
 *              defaults to all args (a non-variadic call)
 * Returns the boxed C result (NIL for :VOID). */
static CL_Obj bi_ffi_call_foreign(CL_Obj *args, int nargs)
{
    void *fn;
    CLFFIType ret_type;
    CLFFIType atypes[CL_FFI_MAX_ARGS];
    CLFFIValue avals[CL_FFI_MAX_ARGS];
    CLFFIValue rv;
    int n = 0, nfixed;
    CL_Obj tlist, vlist;

    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE, "FFI:CALL-FOREIGN: first argument must be a foreign pointer (function address)");
    fn = platform_ffi_resolve(((CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]))->address);
    if (!fn)
        cl_error(CL_ERR_GENERAL, "FFI:CALL-FOREIGN: null or invalid function pointer");
    ret_type = ffi_kw_to_type(args[1]);

    tlist = args[2];
    vlist = args[3];
    while (!CL_NULL_P(tlist) && !CL_NULL_P(vlist)) {
        if (n >= CL_FFI_MAX_ARGS)
            cl_error(CL_ERR_ARGS, "FFI:CALL-FOREIGN: too many arguments (max %d)", CL_FFI_MAX_ARGS);
        atypes[n] = ffi_kw_to_type(cl_car(tlist));
        ffi_marshal_arg(atypes[n], cl_car(vlist), &avals[n]);
        n++;
        tlist = cl_cdr(tlist);
        vlist = cl_cdr(vlist);
    }
    if (!CL_NULL_P(tlist) || !CL_NULL_P(vlist))
        cl_error(CL_ERR_ARGS, "FFI:CALL-FOREIGN: argument type/value count mismatch");

    nfixed = (nargs > 4 && !CL_NULL_P(args[4])) ? (int)CL_FIXNUM_VAL(args[4]) : n;

    if (platform_ffi_call(fn, ret_type, &rv, n, nfixed, atypes, avals) != 0)
        cl_error(CL_ERR_GENERAL,
                 "FFI:CALL-FOREIGN: foreign call failed (FFI calls are not supported on this platform)");

    return ffi_box_result(ret_type, &rv);
}

/* ================================================================
 * Callbacks — Lisp functions exposed as C function pointers
 *
 * Each live callback occupies a fixed slot whose LISP_FN field is a
 * registered GC root (the closure is C-allocated outside the arena, so the
 * Lisp function it references would otherwise be invisible to GC).  The
 * CFFI backend creates one closure per callback NAME and dispatches through
 * an indirection, so this fixed table is ample.
 * ================================================================ */

#define CL_FFI_MAX_CALLBACKS 64

typedef struct {
    int       in_use;
    CLFFIType ret_type;
    CLFFIType arg_types[CL_FFI_MAX_ARGS];
    int       nargs;
    void     *plat_closure;
    uint32_t  code_handle; /* side-table handle from platform_ffi_register(code) */
    CL_Obj    lisp_fn;     /* GC root — registered in init */
} FFICallback;

static FFICallback ffi_callbacks[CL_FFI_MAX_CALLBACKS];
/* Serializes slot claim/release: two MP threads scanning for a free slot
 * unlocked could claim the SAME slot, clobbering each other's lisp_fn /
 * plat_closure (foreign code then invokes the wrong Lisp function). */
static void *ffi_callback_lock = NULL;

/* Invoked (via the platform trampoline) when foreign code calls a callback.
 * Marshals C args -> CL, applies the Lisp function, marshals the result back. */
static void ffi_callback_handler(void *ud, const CLFFIValue *cargs, CLFFIValue *cret)
{
    FFICallback *cb = (FFICallback *)ud;
    CL_Obj clargs[CL_FFI_MAX_ARGS];
    CL_Obj result = CL_NIL;
    int i, n = cb->nargs;

    for (i = 0; i < n; i++) clargs[i] = CL_NIL;
    /* Protect every arg slot BEFORE any boxing allocates — a later box may
     * trigger GC that would otherwise strand earlier (already-boxed) args. */
    for (i = 0; i < n; i++) CL_GC_PROTECT(clargs[i]);
    CL_GC_PROTECT(result);

    for (i = 0; i < n; i++)
        clargs[i] = ffi_box_result(cb->arg_types[i], (CLFFIValue *)&cargs[i]);
    result = cl_vm_apply(cb->lisp_fn, clargs, n);
    if (cb->ret_type != CL_FFI_VOID)
        ffi_marshal_arg(cb->ret_type, result, cret);

    CL_GC_UNPROTECT(n + 1);
}

/* (ffi:make-callback ret-type arg-types lisp-fn) → foreign-pointer
 * Returns a C-callable function pointer that invokes LISP-FN. */
static CL_Obj bi_ffi_make_callback(CL_Obj *args, int nargs)
{
    int slot, i, n = 0;
    CLFFIType ret_type;
    CLFFIType atypes[CL_FFI_MAX_ARGS];
    CL_Obj tlist;
    void *code, *plat = NULL;
    FFICallback *cb;
    (void)nargs;

    ret_type = ffi_kw_to_type(args[0]);
    for (tlist = args[1]; !CL_NULL_P(tlist); tlist = cl_cdr(tlist)) {
        if (n >= CL_FFI_MAX_ARGS)
            cl_error(CL_ERR_ARGS, "FFI:MAKE-CALLBACK: too many argument types (max %d)", CL_FFI_MAX_ARGS);
        atypes[n++] = ffi_kw_to_type(cl_car(tlist));
    }

    /* Claim the slot under the lock (in_use=1 immediately) so a peer
     * thread's scan cannot pick the same slot; release it on failure. */
    if (ffi_callback_lock) platform_mutex_lock(ffi_callback_lock);
    for (slot = 0; slot < CL_FFI_MAX_CALLBACKS; slot++)
        if (!ffi_callbacks[slot].in_use) break;
    if (slot == CL_FFI_MAX_CALLBACKS) {
        if (ffi_callback_lock) platform_mutex_unlock(ffi_callback_lock);
        cl_error(CL_ERR_GENERAL, "FFI:MAKE-CALLBACK: too many live callbacks (max %d)", CL_FFI_MAX_CALLBACKS);
    }
    cb = &ffi_callbacks[slot];
    cb->in_use = 1;
    if (ffi_callback_lock) platform_mutex_unlock(ffi_callback_lock);

    cb->ret_type = ret_type;
    cb->nargs = n;
    for (i = 0; i < n; i++) cb->arg_types[i] = atypes[i];
    cb->lisp_fn = args[2];  /* rooted slot — survives the alloc below */

    code = platform_ffi_make_closure(ret_type, n, atypes, ffi_callback_handler, cb, &plat);
    if (!code) {
        cb->lisp_fn = CL_NIL;
        cb->in_use = 0;
        cl_error(CL_ERR_GENERAL, "FFI:MAKE-CALLBACK: callbacks are not supported on this platform");
    }
    cb->plat_closure = plat;
    cb->code_handle = platform_ffi_register(code);
    return cl_make_foreign_pointer(cb->code_handle, 0, 0);
}

/* (ffi:free-callback fp) → nil
 * Release a callback created by ffi:make-callback.  Frees the native closure
 * and reclaims the callback slot so it can be reused.  The foreign-pointer
 * argument becomes invalid after this call. */
static CL_Obj bi_ffi_free_callback(CL_Obj *args, int nargs)
{
    CL_ForeignPtr *fp;
    uint32_t h;
    int slot;
    (void)nargs;

    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE, "FFI:FREE-CALLBACK: expected a foreign-pointer");

    fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]);
    h = fp->address;

    if (ffi_callback_lock) platform_mutex_lock(ffi_callback_lock);
    for (slot = 0; slot < CL_FFI_MAX_CALLBACKS; slot++) {
        FFICallback *cb = &ffi_callbacks[slot];
        if (cb->in_use && cb->code_handle == h) {
            platform_ffi_free_closure(cb->plat_closure);
            platform_ffi_release(cb->code_handle);
            cb->lisp_fn = CL_NIL;
            cb->plat_closure = NULL;
            cb->code_handle = 0;
            cb->in_use = 0;
            if (ffi_callback_lock) platform_mutex_unlock(ffi_callback_lock);
            return CL_NIL;
        }
    }
    if (ffi_callback_lock) platform_mutex_unlock(ffi_callback_lock);
    cl_error(CL_ERR_GENERAL, "FFI:FREE-CALLBACK: not a live callback pointer");
    return CL_NIL;
}

/* ================================================================
 * Init
 * ================================================================ */

void cl_builtins_ffi_init(void)
{
    {
        /* Callback Lisp-function slots are GC roots (referenced only from
         * C-allocated closures, invisible to the mark phase otherwise). */
        int ci;
        for (ci = 0; ci < CL_FFI_MAX_CALLBACKS; ci++) {
            ffi_callbacks[ci].lisp_fn = CL_NIL;
            cl_gc_register_root(&ffi_callbacks[ci].lisp_fn);
        }
        platform_mutex_init(&ffi_callback_lock);
    }

    /* Register all keyword cache slots as GC roots BEFORE any cl_intern_keyword
     * call allocates — a compaction triggered mid-sequence would leave already-
     * stored slots with stale offsets if they aren't roots yet (same class of
     * bug as the funcallable-instance stale-static-cache). */
    {
        int ti;
        for (ti = 0; ti <= (int)CL_FFI_POINTER; ti++) {
            ffi_type_keywords[ti] = CL_NIL;
            cl_gc_register_root(&ffi_type_keywords[ti]);
        }
    }

    /* Primitive type keywords for CALL-FOREIGN (interned once). */
    ffi_type_keywords[CL_FFI_VOID]    = cl_intern_keyword("VOID", 4);
    ffi_type_keywords[CL_FFI_I8]      = cl_intern_keyword("INT8", 4);
    ffi_type_keywords[CL_FFI_U8]      = cl_intern_keyword("UINT8", 5);
    ffi_type_keywords[CL_FFI_I16]     = cl_intern_keyword("INT16", 5);
    ffi_type_keywords[CL_FFI_U16]     = cl_intern_keyword("UINT16", 6);
    ffi_type_keywords[CL_FFI_I32]     = cl_intern_keyword("INT32", 5);
    ffi_type_keywords[CL_FFI_U32]     = cl_intern_keyword("UINT32", 6);
    ffi_type_keywords[CL_FFI_I64]     = cl_intern_keyword("INT64", 5);
    ffi_type_keywords[CL_FFI_U64]     = cl_intern_keyword("UINT64", 6);
    ffi_type_keywords[CL_FFI_FLOAT]   = cl_intern_keyword("FLOAT", 5);
    ffi_type_keywords[CL_FFI_DOUBLE]  = cl_intern_keyword("DOUBLE", 6);
    ffi_type_keywords[CL_FFI_POINTER] = cl_intern_keyword("POINTER", 7);

    /* Foreign pointer management */
    ffi_defun("MAKE-FOREIGN-POINTER",    bi_ffi_make_fp,        1, 2);
    ffi_defun("FOREIGN-POINTER-ADDRESS", bi_ffi_fp_address,     1, 1);
    ffi_defun("FOREIGN-POINTER-P",       bi_ffi_fp_p,           1, 1);
    ffi_defun("NULL-POINTER-P",          bi_ffi_null_p,         1, 1);
    ffi_defun("POINTER-EQ",              bi_ffi_pointer_eq,     2, 2);

    /* Allocation */
    ffi_defun("ALLOC-FOREIGN",           bi_ffi_alloc,          1, 1);
    ffi_defun("FREE-FOREIGN",            bi_ffi_free,           1, 1);

    /* Peek/Poke */
    ffi_defun("PEEK-U32",               bi_ffi_peek32,          1, 2);
    ffi_defun("PEEK-U16",               bi_ffi_peek16,          1, 2);
    ffi_defun("PEEK-U8",                bi_ffi_peek8,           1, 2);
    ffi_defun("POKE-U32",               bi_ffi_poke32,          2, 3);
    ffi_defun("POKE-U16",               bi_ffi_poke16,          2, 3);
    ffi_defun("POKE-U8",                bi_ffi_poke8,           2, 3);

    /* Bulk byte transfer */
    ffi_defun("POKE-BYTES",             bi_ffi_poke_bytes,      2, 5);
    ffi_defun("PEEK-BYTES",             bi_ffi_peek_bytes,      2, 5);

    /* Typed peek/poke: signed, 64-bit, float/double, pointer */
    ffi_defun("PEEK-I8",                bi_ffi_peek_i8,         1, 2);
    ffi_defun("PEEK-I16",               bi_ffi_peek_i16,        1, 2);
    ffi_defun("PEEK-I32",               bi_ffi_peek_i32,        1, 2);
    ffi_defun("PEEK-U64",               bi_ffi_peek_u64,        1, 2);
    ffi_defun("PEEK-I64",               bi_ffi_peek_i64,        1, 2);
    ffi_defun("PEEK-SINGLE",            bi_ffi_peek_single,     1, 2);
    ffi_defun("PEEK-DOUBLE",            bi_ffi_peek_double,     1, 2);
    ffi_defun("PEEK-POINTER",           bi_ffi_peek_pointer,    1, 2);
    ffi_defun("POKE-I8",                bi_ffi_poke_i8,         2, 3);
    ffi_defun("POKE-I16",               bi_ffi_poke_i16,        2, 3);
    ffi_defun("POKE-I32",               bi_ffi_poke_i32,        2, 3);
    ffi_defun("POKE-U64",               bi_ffi_poke_u64,        2, 3);
    ffi_defun("POKE-I64",               bi_ffi_poke_i64,        2, 3);
    ffi_defun("POKE-SINGLE",            bi_ffi_poke_single,     2, 3);
    ffi_defun("POKE-DOUBLE",            bi_ffi_poke_double,     2, 3);
    ffi_defun("POKE-POINTER",           bi_ffi_poke_pointer,    2, 3);

    /* String conversion */
    ffi_defun("FOREIGN-STRING",          bi_ffi_foreign_string,     1, 1);
    ffi_defun("FOREIGN-TO-STRING",       bi_ffi_foreign_to_string,  1, 2);

    /* Pointer arithmetic */
    ffi_defun("POINTER+",               bi_ffi_pointer_plus,        2, 2);

    /* Dynamic libraries + foreign function calls (host) */
    ffi_defun("LOAD-LIBRARY",           bi_ffi_load_library,        1, 1);
    ffi_defun("CLOSE-LIBRARY",          bi_ffi_close_library,       1, 1);
    ffi_defun("SYMBOL-POINTER",         bi_ffi_symbol_pointer,      1, 2);
    ffi_defun("CALL-FOREIGN",           bi_ffi_call_foreign,        4, 5);
    ffi_defun("MAKE-CALLBACK",          bi_ffi_make_callback,       3, 3);
    ffi_defun("FREE-CALLBACK",          bi_ffi_free_callback,       1, 1);
}
