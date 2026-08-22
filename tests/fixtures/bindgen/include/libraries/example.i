	IFND	LIBRARIES_EXAMPLE_I
LIBRARIES_EXAMPLE_I	SET	1
**
**	fixture include for tests/test_amiga_bindgen.sh — exercises every
**	construct the generator understands.
**
	INCLUDE "exec/exbase.i"

EXAMPLENAME	MACRO
	dc.b	'example.library',0
	ENDM

EXF_BASE	EQU	$1000	; flags base
EXF_FIRST	EQU	EXF_BASE+1
EXF_MASK	EQU	(1<<4)!(1<<5)	* a trailing remark after the operand
EXF_LATER	EQU	EXF_FORWARD*2	; forward reference
EXF_FORWARD	EQU	3
EXF_HEX		equ	0x1f
EXF_CHAR	EQU	(('E'<<24)!('X'<<16)!('A'<<8)!'M')
EXF_NEG		EQU	-5
EXF_NOT		EQU	~0
EXF_BIN		EQU	%1010
EXF_SPACED	EQU	( 'D'<<8 ) ! ( 'O' )
	BITDEF	EX,READY,3
	ENUM	EXF_BASE+$100
	EITEM	EXA_First
	EITEM	EXA_Second,
	ENUM
	EITEM	EXZ_Zero
	DEVINIT
	DEVCMD	EXCMD_PING
	DEVCMD	EXCMD_PONG
	LIBINIT
	LIBDEF	_LVOExFlush
	LIBDEF	_LVOExCreate

   STRUCTURE ExThing,exb_SIZEOF
	APTR	ext_Name
	WORD	ext_X
	WORD	ext_Y
	UBYTE	ext_Flag
	ALIGNWORD
	ULONG	ext_Count
	STRUCT	ext_Inner,<(EXF_FORWARD+5)>	; 8 bytes via <...> brackets
	LABEL	ext_Marker
	BPTR	ext_Lock
	FLOAT	ext_Scale
	LABEL	ext_SIZEOF

* exec-style structure with two size labels: IORequest inside IOStdReq
   STRUCTURE IO,0
	APTR	IO_DEVICE
	LABEL	IO_SIZE
	ULONG	IO_ACTUAL
	LABEL	IOSTD_SIZE

	ENDC	; LIBRARIES_EXAMPLE_I
