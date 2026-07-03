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
#include "compiler.h"
#include "../platform/platform.h"
#include <string.h>
#include <stdio.h>

/* Symbol AMIGA::%FFI-CALL — sentinel that the compiler matches to emit
 * OP_AMIGA_CALL.  Initialized to CL_NIL on non-Amiga builds (where the
 * AMIGA package doesn't exist), to the actual symbol on Amiga.  Declared
 * in compiler.h so compile_call can reference it without a hard dep on
 * Amiga-only headers. */
CL_Obj cl_amiga_ffi_call_sym = CL_NIL;

#ifdef PLATFORM_AMIGA

/* ================================================================
 * Helper: register function in AMIGA package and export
 * ================================================================ */

static void amiga_defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_amiga);
    CL_Obj fn;
    CL_Symbol *s;
    CL_GC_PROTECT(sym);
    fn = cl_make_function(func, sym, min, max);
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    cl_export_symbol(sym, cl_package_amiga);
    CL_GC_UNPROTECT(1);
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
    /* :A5 (index 13) is deliberately REJECTED: ffi_dispatch_m68k.s only
     * loads d0-d7/a0-a4 (a5 is its scratch/frame register — see the
     * comment at the top of the dispatcher).  Accepting it here silently
     * dropped the argument. */
    if (kw == kw_a5)
        cl_error(CL_ERR_ARGS,
                 "AMIGA FFI: register :A5 is reserved by the call "
                 "dispatcher and cannot carry an argument (use d0-d7/a0-a4)");
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

/* (amiga:call-library-fast base offset regspec &rest values) → integer
 *
 * Fast path: register layout is encoded in REGSPEC as a fixnum of nibbles
 * (low to high), one per value argument:
 *   bits  0-3  -> register index for value 0
 *   bits  4-7  -> register index for value 1
 *   ...
 *   bits 24-27 -> register index for value 6
 * Indices match decode_register_keyword (D0..D7=0..7, A0..A5=8..13).
 *
 * This avoids the per-call list allocation and keyword walk that
 * CALL-LIBRARY does, at the cost of capping at 7 register args
 * (sufficient for all standard Amiga library calls). */
static CL_Obj bi_amiga_call_library_fast(CL_Obj *args, int nargs)
{
    CL_ForeignPtr *fp;
    int16_t offset;
    uint32_t regspec;
    uint32_t regs[14];
    uint16_t reg_mask = 0;
    int n_regs, i;
    uint32_t result;

    if (!CL_FOREIGN_POINTER_P(args[0]))
        cl_error(CL_ERR_TYPE,
                 "AMIGA:CALL-LIBRARY-FAST: base must be a foreign pointer");
    fp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(args[0]);

    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE,
                 "AMIGA:CALL-LIBRARY-FAST: offset must be a fixnum");
    offset = (int16_t)CL_FIXNUM_VAL(args[1]);

    if (!CL_FIXNUM_P(args[2]))
        cl_error(CL_ERR_TYPE,
                 "AMIGA:CALL-LIBRARY-FAST: regspec must be a fixnum");
    regspec = (uint32_t)CL_FIXNUM_VAL(args[2]);

    n_regs = nargs - 3;
    if (n_regs < 0 || n_regs > 7)
        cl_error(CL_ERR_ARGS,
                 "AMIGA:CALL-LIBRARY-FAST: too many register args (max 7)");

    memset(regs, 0, sizeof(regs));
    for (i = 0; i < n_regs; i++) {
        int reg_idx = (int)((regspec >> (i * 4)) & 0xF);
        if (reg_idx > 13)
            cl_error(CL_ERR_ARGS,
                     "AMIGA:CALL-LIBRARY-FAST: invalid register index in regspec");
        regs[reg_idx] = ffi_arg_to_u32(args[3 + i]);
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

/* OP_AMIGA_CALL dispatch helper — called from vm.c when the dedicated
 * bytecode op fires.  Decodes regspec into the trampoline registers,
 * invokes the library, and returns the boxed result (or CL_NIL if
 * void_p is set).  The caller has already validated base_addr.
 *
 * regspec layout: low 28 bits = 7 nibbles (one register index per arg),
 * bit 28 = void-p (skip result boxing).  Kept inside the 30-bit fixnum
 * range so defcfun can emit it as a literal fixnum. */
CL_Obj cl_amiga_ffi_call_dispatch(uint32_t base_addr, int16_t offset,
                                  uint32_t regspec, int n_args,
                                  CL_Obj *arg_base)
{
    uint32_t regs[14];
    uint16_t reg_mask = 0;
    int i;
    uint32_t result;
    int void_p = (int)((regspec >> 28) & 1);

    if (n_args < 0 || n_args > 7)
        cl_error(CL_ERR_ARGS,
                 "OP_AMIGA_CALL: too many register args (max 7), got %d",
                 n_args);

    memset(regs, 0, sizeof(regs));
    for (i = 0; i < n_args; i++) {
        int reg_idx = (int)((regspec >> (i * 4)) & 0xF);
        if (reg_idx > 13)
            cl_error(CL_ERR_ARGS,
                     "OP_AMIGA_CALL: invalid register index in regspec");
        regs[reg_idx] = ffi_arg_to_u32(arg_base[i]);
        reg_mask |= (uint16_t)(1 << reg_idx);
    }

    result = platform_amiga_call(base_addr, offset, regs, reg_mask);

    if (void_p)
        return CL_NIL;
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

#else /* !PLATFORM_AMIGA — host stub so vm.c links cleanly */

CL_Obj cl_amiga_ffi_call_dispatch(uint32_t base_addr, int16_t offset,
                                  uint32_t regspec, int n_args,
                                  CL_Obj *arg_base)
{
    (void)base_addr; (void)offset; (void)regspec;
    (void)n_args; (void)arg_base;
    cl_error(CL_ERR_GENERAL,
             "OP_AMIGA_CALL emitted on non-Amiga build — defcfun expansion bug");
    return CL_NIL;
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
    amiga_defun("OPEN-LIBRARY",      bi_amiga_open_library,       1,  2);
    amiga_defun("CLOSE-LIBRARY",     bi_amiga_close_library,      1,  1);
    amiga_defun("CALL-LIBRARY",      bi_amiga_call_library,       3,  3);
    amiga_defun("CALL-LIBRARY-FAST", bi_amiga_call_library_fast,  3, -1);
    amiga_defun("ALLOC-CHIP",        bi_amiga_alloc_chip,         1,  1);
    amiga_defun("FREE-CHIP",         bi_amiga_free_chip,          1,  1);

    /* Intern AMIGA::%FFI-CALL — compile_call matches against this exact
     * symbol object to emit OP_AMIGA_CALL.  Exported so defcfun (which
     * lives in AMIGA.FFI) can emit it as `amiga:%ffi-call` without
     * package gymnastics; user code shouldn't write it directly. */
    cl_amiga_ffi_call_sym = cl_intern_in("%FFI-CALL", 9, cl_package_amiga);
    cl_export_symbol(cl_amiga_ffi_call_sym, cl_package_amiga);

    /* Register cached symbols for GC compaction forwarding */
    cl_gc_register_root(&cl_amiga_ffi_call_sym);
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
