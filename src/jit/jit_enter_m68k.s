| jit_enter_m68k.s -- the C-to-native entry of the m68k JIT  (AmigaOS)
|
| CL_Obj cl_jit_enter(void *entry, CL_Thread *thread, CL_Obj func,
|                     const CL_Obj *argv, int32_t nargs);
|
| Calls the native function at `entry` with A3 = `thread`, the stack laid
| out the way a native call site lays it out (specs/jit-direct-calls.md §4):
|
|     8(a6)            func
|     12(a6)           argv[nargs-1]      <- the last argument nearest
|     ...                                    the return address
|     12+4*(n-1)(a6)   argv[0]
|
| i.e. parameter i at 12 + 4*(nargs-1-i)(a6), the operand-stack order.  The
| keyword ABI, native(func, bc, nargs, args), goes through here too: the
| caller passes argv = { args, nargs, bc } with nargs = 3, which pushes
| them in C order.
|
| A3 holds the CL_Thread * for as long as native code runs.  The walker
| never allocates A2-A5, and A3 is callee-saved in the m68k SysV ABI (and
| in the AmigaOS library ABI), so every C helper and OS call the native
| code makes hands it back unchanged; this routine saves and restores the
| caller's A3.  A longjmp out of native code lands in a C frame whose
| setjmp saved its own A3, so nothing here needs to be unwound.
|
| D2 is ours too (the pop count); native code preserves it like every
| callee-saved register.  dbf counts a word, which is plenty: the walker
| caps positional arity far below 32K.
|
| cl_jit_invoke (src/jit/jit_m68k.c) is the only caller.  Not built on MorphOS
| (PPC has no JIT).

	.text
	.even

	.globl	_cl_jit_enter
_cl_jit_enter:
	movem.l	d2/a2-a3,-(sp)		| 3 regs = 12 bytes
	movea.l	16(sp),a2		| a2 = entry     (12 saved + 4 return)
	movea.l	20(sp),a3		| a3 = thread
	move.l	24(sp),d1		| d1 = func
	movea.l	28(sp),a0		| a0 = argv
	move.l	32(sp),d2		| d2 = nargs
	move.l	d2,d0
	bra.s	.Lnext
.Lpush:
	move.l	(a0)+,-(sp)		| argv[0] first: it ends up highest
.Lnext:
	dbf	d0,.Lpush
	move.l	d1,-(sp)		| func at 8(a6) of the callee
	jsr	(a2)			| D0 = the result
	lsl.l	#2,d2
	lea	4(sp,d2.l),sp		| drop func + the arguments
	movem.l	(sp)+,d2/a2-a3
	rts
