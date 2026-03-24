/*
 * builtins_amiga.c — AMIGA package builtins.
 *
 * AmigaOS-specific: shared library open/close, register-based library
 * calls, chip memory allocation.  Entire file is compiled on all platforms
 * but the init function is a no-op on POSIX (AMIGA package doesn't exist).
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

#ifdef PLATFORM_AMIGA

/* ================================================================
 * Helper: register function in AMIGA package and export
 * ================================================================ */

static void amiga_defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_amiga);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    s->value = fn;
    cl_export_symbol(sym, cl_package_amiga);
}

/* Pre-interned register keyword symbols */
static CL_Obj kw_d0, kw_d1, kw_d2, kw_d3, kw_d4, kw_d5, kw_d6, kw_d7;
static CL_Obj kw_a0, kw_a1, kw_a2, kw_a3, kw_a4, kw_a5;

/* Map a keyword symbol to a register index (0-13), or -1 if unknown */
static int decode_register_keyword(CL_Obj kw)
{
    if (kw == kw_d0) return 0;
    if (kw == kw_d1) return 1;
    if (kw == kw_d2) return 2;
    if (kw == kw_d3) return 3;
    if (kw == kw_d4) return 4;
    if (kw == kw_d5) return 5;
    if (kw == kw_d6) return 6;
    if (kw == kw_d7) return 7;
    if (kw == kw_a0) return 8;
    if (kw == kw_a1) return 9;
    if (kw == kw_a2) return 10;
    if (kw == kw_a3) return 11;
    if (kw == kw_a4) return 12;
    if (kw == kw_a5) return 13;
    return -1;
}

/* Helper: convert a CL_Obj argument to a uint32_t for register loading.
 * Accepts fixnums, bignums, and foreign pointers (extracts address). */
static uint32_t ffi_arg_to_u32(CL_Obj val)
{
    if (CL_FIXNUM_P(val))
        return (uint32_t)CL_FIXNUM_VAL(val);
    if (CL_BIGNUM_P(val)) {
        CL_Bignum *bn = (CL_Bignum *)CL_OBJ_TO_PTR(val);
        uint32_t r = (uint32_t)bn->limbs[0];
        if (bn->length > 1)
            r |= ((uint32_t)bn->limbs[1]) << 16;
        return r;
    }
    if (CL_FOREIGN_POINTER_P(val)) {
        CL_ForeignPtr *fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(val);
        return fp->address;
    }
    if (CL_NULL_P(val))
        return 0;
    cl_error(CL_ERR_TYPE,
             "AMIGA:CALL-LIBRARY: register argument must be integer or foreign pointer");
    return 0;
}

/* ================================================================
 * Library management
 * ================================================================ */

/* (amiga:open-library name &optional version) → foreign-pointer */
static CL_Obj bi_amiga_open_library(CL_Obj *args, int nargs)
{
    CL_String *s;
    char namebuf[128];
    uint32_t version, base;

    if (!CL_STRING_P(args[0]))
        cl_error(CL_ERR_TYPE, "AMIGA:OPEN-LIBRARY: name must be a string");
    s = (CL_String *)CL_OBJ_TO_PTR(args[0]);
    if (s->length >= sizeof(namebuf))
        cl_error(CL_ERR_ARGS, "AMIGA:OPEN-LIBRARY: name too long");
    memcpy(namebuf, s->data, s->length);
    namebuf[s->length] = '\0';
    version = (nargs > 1 && !CL_NULL_P(args[1]))
              ? (uint32_t)CL_FIXNUM_VAL(args[1]) : 0;
    base = platform_amiga_open_library(namebuf, version);
    if (base == 0)
        return CL_NIL;
    return cl_make_foreign_pointer(base, 0, 0);
}

/* (amiga:close-library lib) → T */
static CL_Obj bi_amiga_close_library(CL_Obj *args, int nargs)
{
    CL_ForeignPtr *fp;
    (void)nargs;
    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE, "AMIGA:CLOSE-LIBRARY: argument must be a foreign pointer");
    fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]);
    platform_amiga_close_library(fp->address);
    fp->address = 0;
    return CL_T;
}

/* (amiga:call-library base offset reg-spec) → integer
 *
 * reg-spec is a plist: (:D0 val :A0 ptr :D1 42 ...)
 * Each keyword names a 68k register, each value is the argument.
 * Values can be fixnums, bignums, or foreign pointers.
 * Returns d0 result as integer. */
static CL_Obj bi_amiga_call_library(CL_Obj *args, int nargs)
{
    CL_ForeignPtr *fp;
    int16_t offset;
    uint32_t regs[14];
    uint16_t reg_mask = 0;
    CL_Obj spec;
    uint32_t result;

    (void)nargs;

    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE, "AMIGA:CALL-LIBRARY: base must be a foreign pointer");
    fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]);

    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "AMIGA:CALL-LIBRARY: offset must be a fixnum");
    offset = (int16_t)CL_FIXNUM_VAL(args[1]);

    /* Clear register array */
    memset(regs, 0, sizeof(regs));

    /* Parse register spec plist: (:D0 42 :A0 ptr ...) */
    spec = args[2];
    while (!CL_NULL_P(spec)) {
        CL_Obj kw, val;
        int reg_idx;
        if (!CL_CONS_P(spec))
            cl_error(CL_ERR_TYPE, "AMIGA:CALL-LIBRARY: malformed register spec");
        kw = cl_car(spec);
        spec = cl_cdr(spec);
        if (CL_NULL_P(spec))
            cl_error(CL_ERR_ARGS, "AMIGA:CALL-LIBRARY: odd number of elements in register spec");
        val = cl_car(spec);
        spec = cl_cdr(spec);

        reg_idx = decode_register_keyword(kw);
        if (reg_idx < 0)
            cl_error(CL_ERR_ARGS, "AMIGA:CALL-LIBRARY: unknown register keyword");
        regs[reg_idx] = ffi_arg_to_u32(val);
        reg_mask |= (uint16_t)(1 << reg_idx);
    }

    result = platform_amiga_call(fp->address, offset, regs, reg_mask);

    if (result <= (uint32_t)CL_FIXNUM_MAX)
        return CL_MAKE_FIXNUM((int32_t)result);
    else {
        CL_Obj bn = cl_make_bignum(2, 0);
        CL_Bignum *b = (CL_Bignum *)CL_OBJ_TO_PTR(bn);
        b->limbs[0] = (uint16_t)(result & 0xFFFF);
        b->limbs[1] = (uint16_t)(result >> 16);
        return bn;
    }
}

/* (amiga:alloc-chip size) → foreign-pointer */
static CL_Obj bi_amiga_alloc_chip(CL_Obj *args, int nargs)
{
    uint32_t size, addr;
    (void)nargs;
    if (!CL_FIXNUM_P(args[0]) || CL_FIXNUM_VAL(args[0]) <= 0)
        cl_error(CL_ERR_ARGS, "AMIGA:ALLOC-CHIP: size must be a positive integer");
    size = (uint32_t)CL_FIXNUM_VAL(args[0]);
    addr = platform_amiga_alloc_chip(size);
    if (addr == 0)
        cl_error(CL_ERR_GENERAL, "AMIGA:ALLOC-CHIP: allocation of %u bytes failed",
                 (unsigned)size);
    return cl_make_foreign_pointer(addr, size,
                                    CL_FPTR_FLAG_OWNED | CL_FPTR_FLAG_CHIP);
}

/* (amiga:free-chip fp) → T */
static CL_Obj bi_amiga_free_chip(CL_Obj *args, int nargs)
{
    CL_ForeignPtr *fp;
    (void)nargs;
    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE, "AMIGA:FREE-CHIP: argument must be a foreign pointer");
    fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]);
    if (!(fp->flags & CL_FPTR_FLAG_CHIP))
        cl_error(CL_ERR_GENERAL, "AMIGA:FREE-CHIP: pointer is not chip memory");
    platform_amiga_free_chip(fp->address, fp->size);
    fp->address = 0;
    fp->flags &= (uint8_t)~(CL_FPTR_FLAG_OWNED | CL_FPTR_FLAG_CHIP);
    return CL_T;
}

#endif /* PLATFORM_AMIGA */

/* ================================================================
 * Init
 * ================================================================ */

void cl_builtins_amiga_init(void)
{
#ifndef PLATFORM_AMIGA
    /* AMIGA package only exists on AmigaOS */
    return;
#else
    /* Pre-intern register keywords */
    kw_d0 = cl_intern_in("D0", 2, cl_package_keyword);
    kw_d1 = cl_intern_in("D1", 2, cl_package_keyword);
    kw_d2 = cl_intern_in("D2", 2, cl_package_keyword);
    kw_d3 = cl_intern_in("D3", 2, cl_package_keyword);
    kw_d4 = cl_intern_in("D4", 2, cl_package_keyword);
    kw_d5 = cl_intern_in("D5", 2, cl_package_keyword);
    kw_d6 = cl_intern_in("D6", 2, cl_package_keyword);
    kw_d7 = cl_intern_in("D7", 2, cl_package_keyword);
    kw_a0 = cl_intern_in("A0", 2, cl_package_keyword);
    kw_a1 = cl_intern_in("A1", 2, cl_package_keyword);
    kw_a2 = cl_intern_in("A2", 2, cl_package_keyword);
    kw_a3 = cl_intern_in("A3", 2, cl_package_keyword);
    kw_a4 = cl_intern_in("A4", 2, cl_package_keyword);
    kw_a5 = cl_intern_in("A5", 2, cl_package_keyword);

    /* Register builtins in AMIGA package */
    amiga_defun("OPEN-LIBRARY",   bi_amiga_open_library,    1, 2);
    amiga_defun("CLOSE-LIBRARY",  bi_amiga_close_library,   1, 1);
    amiga_defun("CALL-LIBRARY",   bi_amiga_call_library,    3, 3);
    amiga_defun("ALLOC-CHIP",     bi_amiga_alloc_chip,      1, 1);
    amiga_defun("FREE-CHIP",      bi_amiga_free_chip,       1, 1);

    /* Register cached symbols for GC compaction forwarding */
    cl_gc_register_root(&kw_d0);
    cl_gc_register_root(&kw_d1);
    cl_gc_register_root(&kw_d2);
    cl_gc_register_root(&kw_d3);
    cl_gc_register_root(&kw_d4);
    cl_gc_register_root(&kw_d5);
    cl_gc_register_root(&kw_d6);
    cl_gc_register_root(&kw_d7);
    cl_gc_register_root(&kw_a0);
    cl_gc_register_root(&kw_a1);
    cl_gc_register_root(&kw_a2);
    cl_gc_register_root(&kw_a3);
    cl_gc_register_root(&kw_a4);
    cl_gc_register_root(&kw_a5);
#endif /* PLATFORM_AMIGA */
}
