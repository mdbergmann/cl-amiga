| stack_swap_m68k.s -- run the runtime's main on a stack of its own (m68k AmigaOS)
|
| int  platform_stack_call(struct StackSwapStruct *ss,
|                          int (*fn)(int, char **), int argc, char **argv);
| void platform_stack_exit(struct StackSwapStruct *ss, void *mem, int code);
|
| Both wrap exec's StackSwap.  They are assembly on purpose: to gcc,
| StackSwap is an ordinary function call, so C code around it may keep
| sp-relative temporaries, read its own arguments from the old frame, or
| -- with -Os -fomit-frame-pointer -- defer the pop of a callee's pushed
| arguments PAST the swap back, unwinding the wrong stack by those bytes
| before movem/rts (seen in platform_run_main's disassembly, 2026-09-19).
| Here every value that lives across a swap is in a callee-saved register.
|
| platform_stack_call: swaps to ss, calls fn(argc, argv) there, swaps back
| and returns fn's result; ss then describes the block again (stk_Pointer =
| the sp fn was left at) so the caller may free it.
|
| platform_stack_exit: for a process ending on the swapped stack (libnix's
| exit resets sp to what _start saved on the ORIGINAL stack, so the block
| must not be freed while sp is still inside it): swap back, FreeVec(mem),
| _exit(code).  Never returns.

	.text
	.even

	.globl	_platform_stack_call
_platform_stack_call:
	movem.l	d2-d3/a2-a3/a6,-(sp)	| 5 regs = 20 bytes
	| 20(sp) = return address, 24(sp) = ss, 28(sp) = fn, 32(sp) = argc, 36(sp) = argv
	movea.l	24(sp),a2		| a2 = ss
	movea.l	28(sp),a3		| a3 = fn
	move.l	32(sp),d2		| d2 = argc
	move.l	36(sp),d3		| d3 = argv
	movea.l	_SysBase,a6
	movea.l	a2,a0
	jsr	-732(a6)		| StackSwap(ss): on the new stack from here
	move.l	d3,-(sp)
	move.l	d2,-(sp)
	jsr	(a3)			| fn(argc, argv)
	addq.l	#8,sp
	move.l	d0,d2			| rc
	movea.l	_SysBase,a6
	movea.l	a2,a0
	jsr	-732(a6)		| StackSwap(ss): back on the caller's stack
	move.l	d2,d0
	movem.l	(sp)+,d2-d3/a2-a3/a6
	rts

	.globl	_platform_stack_exit
_platform_stack_exit:
	| never returns: no registers to preserve
	movea.l	4(sp),a0		| ss
	movea.l	8(sp),a3		| mem
	move.l	12(sp),d3		| code
	movea.l	_SysBase,a6
	jsr	-732(a6)		| StackSwap(ss): back on the original stack
	movea.l	a3,a1
	jsr	-690(a6)		| FreeVec(mem)
	move.l	d3,-(sp)
	jsr	__exit			| _exit(code)
	| not reached
	illegal
