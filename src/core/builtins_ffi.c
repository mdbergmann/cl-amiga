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
#include "../platform/platform.h"
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Helper: register function in FFI package and export
 * ================================================================ */

static void ffi_defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_ffi);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    cl_export_symbol(sym, cl_package_ffi);
}

/* ================================================================
 * Foreign pointer builtins
 * ================================================================ */

/* (ffi:make-foreign-pointer address &optional size) */
static CL_Obj bi_ffi_make_fp(CL_Obj *args, int nargs)
{
    uint32_t addr, size;
    if (!CL_FIXNUM_P(args[0]) && !CL_BIGNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "FFI:MAKE-FOREIGN-POINTER: address must be an integer");
    addr = (uint32_t)CL_FIXNUM_VAL(args[0]);
    if (CL_BIGNUM_P(args[0])) {
        CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(args[0]);
        addr = (uint32_t)bn->limbs[0];
        if (bn->length > 1)
            addr |= ((uint32_t)bn->limbs[1]) << 16;
    }
    size = (nargs > 1 && !CL_NULL_P(args[1])) ? (uint32_t)CL_FIXNUM_VAL(args[1]) : 0;
    return cl_make_foreign_pointer(addr, size, 0);
}

/* (ffi:foreign-pointer-address fp) → integer */
static CL_Obj bi_ffi_fp_address(CL_Obj *args, int nargs)
{
    CL_ForeignPtr *fp;
    (void)nargs;
    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE, "FFI:FOREIGN-POINTER-ADDRESS: argument must be a foreign pointer");
    fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]);
    /* Return as unsigned integer — may need bignum for addresses > 0x3FFFFFFF */
    if (fp->address <= (uint32_t)CL_FIXNUM_MAX)
        return CL_MAKE_FIXNUM((int32_t)fp->address);
    else {
        /* Large address: create bignum */
        CL_Obj bn = cl_make_bignum(2, 0);
        CL_Bignum *b = (CL_Bignum *)CL_OBJ_TO_PTR(bn);
        b->limbs[0] = (uint16_t)(fp->address & 0xFFFF);
        b->limbs[1] = (uint16_t)(fp->address >> 16);
        return bn;
    }
}

/* (ffi:foreign-pointer-p obj) → T or NIL */
static CL_Obj bi_ffi_fp_p(CL_Obj *args, int nargs)
{
    (void)nargs;
    return CL_FOREIGN_POINTER_P(args[0]) ? CL_T : CL_NIL;
}

/* (ffi:null-pointer-p fp) → T or NIL */
static CL_Obj bi_ffi_null_p(CL_Obj *args, int nargs)
{
    CL_ForeignPtr *fp;
    (void)nargs;
    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE, "FFI:NULL-POINTER-P: argument must be a foreign pointer");
    fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]);
    return (fp->address == 0) ? CL_T : CL_NIL;
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
 * Returns a new foreign pointer at fp->address + offset. */
static CL_Obj bi_ffi_pointer_plus(CL_Obj *args, int nargs)
{
    CL_ForeignPtr *fp;
    int32_t offset;
    (void)nargs;
    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE, "FFI:POINTER+: first argument must be a foreign pointer");
    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "FFI:POINTER+: offset must be a fixnum");
    fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]);
    offset = CL_FIXNUM_VAL(args[1]);
    return cl_make_foreign_pointer(fp->address + (uint32_t)offset, 0, 0);
}

/* ================================================================
 * Init
 * ================================================================ */

void cl_builtins_ffi_init(void)
{
    /* Foreign pointer management */
    ffi_defun("MAKE-FOREIGN-POINTER",    bi_ffi_make_fp,        1, 2);
    ffi_defun("FOREIGN-POINTER-ADDRESS", bi_ffi_fp_address,     1, 1);
    ffi_defun("FOREIGN-POINTER-P",       bi_ffi_fp_p,           1, 1);
    ffi_defun("NULL-POINTER-P",          bi_ffi_null_p,         1, 1);

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

    /* String conversion */
    ffi_defun("FOREIGN-STRING",          bi_ffi_foreign_string,     1, 1);
    ffi_defun("FOREIGN-TO-STRING",       bi_ffi_foreign_to_string,  1, 2);

    /* Pointer arithmetic */
    ffi_defun("POINTER+",               bi_ffi_pointer_plus,        2, 2);
}
