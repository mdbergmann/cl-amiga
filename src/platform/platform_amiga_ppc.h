/*
 * platform_amiga_ppc.h — MorphOS (PPC) entry-point gates for dos.library
 * CreateNewProc().
 *
 * Why this exists:
 *
 * dos.library's NP_Entry tag predates PPC: the address handed to it is entered
 * as *m68k* code, because that is what every AmigaOS program ever compiled
 * against it supplies.  MorphOS keeps that contract for compatibility, so
 * passing the address of a PPC-native function makes the freshly created
 * process jump into the ABox 68k emulator, which then decodes PPC words as
 * m68k instructions — the process dies immediately with a "68k Exception in
 * Task <name>" and a stack trace that shows dos.elf followed by an
 * "ABox: Emulation" frame.
 *
 * The portable MorphOS idiom is to hand NP_Entry a small trap gate
 * (struct EmulLibEntry, TRAP_LIB) instead: the emulator recognises the trap
 * word and calls the PPC function natively.  (MorphOS also offers the
 * NP_CodeType/CODETYPE_PPC tag pair, but that depends on the dos.library
 * version, whereas the gate works everywhere the emulator does.)
 *
 * Usage — declare the gate once at file scope after the entry function, then
 * pass it to NP_Entry:
 *
 *     static void my_entry(void) { ... }
 *     CL_PROC_ENTRY_GATE(my_entry_gate, my_entry);
 *     ...
 *     CreateNewProcTags(NP_Entry, CL_PROC_ENTRY(my_entry_gate, my_entry), ...);
 *
 * On m68k AmigaOS both macros collapse to the plain function address, so call
 * sites stay platform-neutral.
 *
 * NP_PPCStackSize: on MorphOS, NP_StackSize sizes the *68k* stack; native PPC
 * code runs on a separate stack whose default is not necessarily as large as
 * the caller asked for.  CL_PROC_STACK_TAGS() expands to both tags so a thread
 * that requested N bytes really gets N bytes of the stack its code runs on.
 */
#ifndef CL_PLATFORM_AMIGA_PPC_H
#define CL_PLATFORM_AMIGA_PPC_H

#ifdef PLATFORM_MORPHOS

#include <emul/emulinterface.h>

#define CL_PROC_ENTRY_GATE(gate, fn) \
    static const struct EmulLibEntry gate = { TRAP_LIB, 0, (void (*)(void))(fn) }
#define CL_PROC_ENTRY(gate, fn)      ((ULONG)&(gate))

#ifdef NP_PPCStackSize
#define CL_PROC_STACK_TAGS(size) \
    NP_StackSize, (ULONG)(size), NP_PPCStackSize, (ULONG)(size)
#else
#define CL_PROC_STACK_TAGS(size) NP_StackSize, (ULONG)(size)
#endif

#else  /* m68k AmigaOS: the entry is native code already */

#define CL_PROC_ENTRY_GATE(gate, fn) typedef int cl_unused_##gate
#define CL_PROC_ENTRY(gate, fn)      ((ULONG)(fn))
#define CL_PROC_STACK_TAGS(size)     NP_StackSize, (ULONG)(size)

#endif

#endif /* CL_PLATFORM_AMIGA_PPC_H */
