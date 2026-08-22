	IFND	EXEC_EXBASE_I
EXEC_EXBASE_I	SET	1
**
**	fixture include: pulled in by libraries/example.i, and a module of
**	its own (amiga/raw/exec/exbase) because no library claims it.
**
CMD_NONSTD	EQU	9	; like exec/io.i — DEVINIT's default base
LIB_BASE	EQU	-30	; like exec/libraries.i — LIBINIT's default base
LIB_VECTSIZE	EQU	6
EXB_MAGIC	EQU	$CAFE

   STRUCTURE ExBase,0
	APTR	exb_Next
	UWORD	exb_Type
	LABEL	exb_SIZEOF

	ENDC	; EXEC_EXBASE_I
