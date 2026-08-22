#ifndef GADGETS_FIXGAD_H
#define GADGETS_FIXGAD_H
/*
**	fixture header for tests/test_amiga_bindgen.sh — a C header without
**	a .i twin: every construct the generator's C header reader understands,
**	and the ones it must skip.  The include guard above is not a constant.
*/

#ifndef REACTION_REACTION_H
#include <reaction/reaction.h>		/* twin-less: read first, REACTION_Dummy known */
#endif
#ifndef LIBRARIES_EXAMPLE_H
#include <libraries/example.h>		/* has a .i twin: must NOT be read */
#endif

/*****************************************************************************/

#define FIXGAD_Dummy		(EXF_BASE + 0x400)	/* assembler constant + hex */
#define FIXGAD_TextPen		(FIXGAD_Dummy + 1)
#define FIXGAD_Long		(FIXGAD_Dummy+2L)	/* L suffix, no spaces */
#define FIXGAD_Shift		(1<<16)
#define FIXGAD_Mask		(0xffff0000)
#define FIXGAD_Ignore		(~0L)
#define FIXGAD_Char		'A'
#define FIXGAD_Packed		'FORM'			/* multi-char, big-endian */
#define FIXGAD_Escaped		'\n'
#define FIXGAD_Word		((UWORD)~0)		/* cast narrows */
#define FIXGAD_Ulong		((ULONG)(-1))
#define FIXGAD_Byte		((BYTE)0xFF)
#define FIXGAD_Alias		EXF_FIRST		/* alias of an assembler constant */
#define FIXGAD_Reaction		(REACTION_Dummy + 3)	/* from the included header */
#define FIXGAD_Ternary		(1 ? 5 : 6)
#define FIXGAD_Oct		010
#define FIXGAD_Rel		(2 < 3)
#define FIXGAD_Multi		(FIXGAD_Shift | \
				 FIXGAD_Char)		/* backslash continuation */
#define FIXGAD_Forward		(FIXGAD_Later + 1)	/* forward reference */
#define FIXGAD_Later		40
#define FIXGAD_Comment		7 /* a comment
				     spanning lines */ + 1
#define FIXGAD_Slash		9 // line comment
#define FIXGAD_Flag					/* empty body: defined, not a constant */

/* not integer constants: skipped and counted (5), function-like not counted */
#define FIXGAD_Name		"gadgets/fixgad.gadget"
#define FIXGAD_Float		((float) 1.5)
#define FixGadObject		NewObject(FIXGAD_GetClass(), NULL
#define FIXGAD_Size		sizeof(struct FixGadInfo)
#define FIXGAD_Stmt		custom.dmacon = 1;
#define FixGadSet(obj, tag)	SetAttrs(obj, tag, TAG_DONE)

/* conditionals: only macros the headers define themselves are defined */
#ifdef __cplusplus
#define FIXGAD_Cpp		1
#endif
#ifndef __cplusplus
#define FIXGAD_NotCpp		2
#else
#define FIXGAD_CppElse		3
#endif
#if 0
#define FIXGAD_IfZero		4
#elif defined(__VBCC__)
#define FIXGAD_Vbcc		5
#else
#define FIXGAD_IfElse		6
#endif
#if defined(FIXGAD_Flag) && !defined(FIXGAD_Nope)
#define FIXGAD_IfDefined	8
#endif

/* #undef + redefinition (texteditor.h): the first value is the constant,
 * later references use the new one, nothing is emitted twice */
#undef FIXGAD_Dummy
#define FIXGAD_Dummy		(0x3000)
#define FIXGAD_After		(FIXGAD_Dummy + 1)

/* enumerators: anonymous, explicit values, expressions, trailing comma */
enum
{
    FIXGAD_IMG_DEFAULT,
    FIXGAD_IMG_INFO,
    FIXGAD_IMG_SKIP = 5,
    FIXGAD_IMG_NEXT,
    FIXGAD_IMG_EXPR = (FIXGAD_IMG_SKIP << 1),
    FIXGAD_IMG_LAST,
};

enum FixGadHow { FIXGAD_SAVE = 0, FIXGAD_USE };

/* C structs are not read; an enum inside one is not a top-level enum */
struct FixGadInfo
{
    WORD fgi_Width;
    enum FixGadHow fgi_How;
    enum { FIXGAD_INNER } fgi_Inner;
    STRPTR fgi_Title;	/* "{ not a brace }" */
};

typedef enum { FIXGAD_TD_A = 100, FIXGAD_TD_B } FixGadTypedef;

#endif /* GADGETS_FIXGAD_H */
