#ifndef LIBRARIES_MUI_H
#define LIBRARIES_MUI_H
/*
**	fixture header for tests/test_amiga_bindgen.sh -- the shape of the MUI
**	3.8 developer kit's libraries/mui.h, found under the SECOND C-header
**	root (BINDGEN_MUI_INCLUDE_H).  No .i twin exists for it anywhere; the
**	muimaster module claims it through the generator's *module-includes*,
**	so no header-only libraries/mui module may appear.
*/

#ifndef EXEC_TYPES_H
#include "exec/types.h"		/* not under any root here: ignored */
#endif

/* string #defines become string constants */
#define MUIMASTER_NAME    "muimaster.library"
#define MUIMASTER_VMIN    11
#define MUI_OBSOLETE		/* the header defines this ITSELF: its #ifdef blocks are live */

/* escapes are decoded and re-quoted; a control character (the ESC of the
   text-style sequences) is written as a #. form; a concatenation, a
   non-ASCII literal, a MAKE_ID() call and an alias of a string macro are
   not constants (skipped, counted) */
#define MUIX_C    "\033c"
#define FIX_ESC   "say \"hi\"\\"
#define FIX_CAT   "a" "b"
#define FIX_LAT1  "\253Frontmost\273"
#define FIX_ID    MAKE_ID('M','P','U','B')
#define FIX_ALIAS MUIMASTER_NAME

#ifdef _DCC
extern char MUIC_Notify[];
#else
#define MUIC_Notify "Notify.mui"
#endif
#define MUIC_Window "Window.mui"

#define MUIV_TriggerValue    0x49893131
#define MUIV_EveryTime       0x49893131
#define MUIV_Application_ReturnID_Quit -1
#define MUIV_Application_Save_ENV     ((STRPTR) 0)
#define MUIV_Application_Save_ENVARC  ((STRPTR)~0)
#define MC_TEMPLATE_ID ((STRPTR)~0)
#define MUI_EHF_ALWAYSKEYS (1<<0)

#define MUIM_Notify                         0x8042c9cb /* V4  */
#ifdef MUI_OBSOLETE
#define MUIM_Application_Input              0x8042d0f5 /* V4  */
#endif /* MUI_OBSOLETE */
#define MUIM_Application_NewInput           0x80423ba6 /* V11 */
#define MUIA_Window_CloseRequest            0x8042e86e /* V4  ..g BOOL */

/* --- struct definitions: laid out by the m68k rules (2-byte alignment) --- */
#define PSD_MAXLEN_NAME 32
enum FixHow { FIXHOW_A, FIXHOW_B };

struct MUI_MinMax { WORD MinWidth; WORD MinHeight; WORD MaxWidth; WORD MaxHeight; WORD DefWidth; WORD DefHeight; };	/* 12 */
struct MUIP_Notify { ULONG MethodID; ULONG TrigAttr; ULONG TrigVal; APTR DestObj; ULONG FollowParams; /* ... */ };	/* 20 */
struct MUIP_AskMinMax { ULONG MethodID; struct MUI_MinMax *MinMaxInfo; };	/* 8 */
struct MUIP_Setup { ULONG MethodID; struct MUI_RenderInfo *RenderInfo; };	/* 8: a pointer to a struct never defined here */
struct ExBase { APTR exb_Next; UWORD exb_Type; };	/* exec/exbase.i's STRUCTURE repeated in C: its row is the binding, none here */
struct MUI_RGBcolor
{
	ULONG red;
	ULONG green;
	ULONG blue;
};

struct FixLayout
{
	UBYTE fl_Tag;                      /* 0 */
	ULONG fl_Flags;                    /* 2: a longword is aligned to 2 */
	struct ExBase fl_Base;             /* 6: a STRUCTURE of exec/exbase.i (6 bytes) by value */
	struct MUI_MinMax fl_MinMax;       /* 12: a struct read above */
	char fl_Name[PSD_MAXLEN_NAME];     /* 24: a char buffer -> a pointer to it, (:struct 32) */
	BYTE fl_Pens[4];                   /* 56: (:array :i8 4) */
	struct MUI_RGBcolor fl_Palette[2]; /* 60: an array of structs -> (:struct 24) */
	Object *fl_Objs[1];                /* 84: (:array :fptr 1) */
	ULONG (*fl_Func)(APTR a, ULONG b); /* 88: a function pointer */
	const char *fl_Text, *fl_Help;     /* 92, 96: two declarators */
	WORD fl_W, fl_H;                   /* 100, 102 */
	unsigned char fl_U8;               /* 104 */
	enum FixHow fl_How;                /* 106: an enum is an int */
	BYTE fl_Odd;                       /* 110 */
};                                     /* 111, padded to 112 */

struct FixStuff
{
	struct ExBase fs_Node;             /* 0 */
	union
	{
		ULONG fs_sigs;                 /* 6 */
		struct
		{
			UWORD fs_millis;           /* 6 */
			UWORD fs_current;          /* 8 */
		} fs_timer;                    /* 6, (:struct 4) */
	}
	fs_stuff;                          /* 6, (:struct 4): the leaves are flattened next to it */
	ULONG fs_Flags;                    /* 10 */
	struct { LONG Width; LONG Height; } fs_Layout;	/* 14, (:struct 8) */
	union { UBYTE fs_A; UWORD fs_B; };	/* 22: an anonymous member -- its leaves only */
};                                     /* 24 */

typedef struct FixTyped { WORD ft_A; } FixTypedName, *FixTypedPtr;	/* 2; FixTypedName is an alias, the pointer typedef is not */
typedef struct { LONG fa_Id; FixTypedName fa_T; } FixAnon;	/* 6: named by the typedef; a typedef'd member */
typedef struct FixTyped FixAlias;	/* an alias without a body */
struct FixUsesAnon { FixAnon fu_A; FixAlias fu_B; struct FixTyped fu_C; };	/* 10 */

struct FixBits { ULONG fb_Flags : 4; };		/* skipped: a bitfield */
struct FixUnknown { struct NoSuchThing fk_X; };	/* skipped: an unknown struct */
struct FixFwd;					/* a forward declaration: nothing */
extern struct Library *FixBase;			/* a declaration, not a definition */
struct FixFwd *FixFunc(struct FixFwd *f, struct MUI_MinMax *mm);	/* a prototype */
#if 0
struct FixDead { ULONG fd_X; };			/* inactive: not read */
#endif
struct __dummyXFC2__ { struct MUI_MinMax mnd; struct FixTyped mad; };	/* reserved name: skipped, its layout still known */
struct FixLast { struct __dummyXFC2__ fl_D; };	/* 14 */

#ifndef MUI_NOSHORTCUTS
#define WindowObject        MUI_NewObject(MUIC_Window
#define End                 TAG_DONE)
#define MUIV_Window_AltHeight_Screen(p) (-3-(p))	/* function-like: never a constant */
#ifdef MUI_OBSOLETE
#define MUIA_Window_Open_Obsolete(x) (x)
#endif
#endif /* MUI_NOSHORTCUTS */

#define PSD_NUMMUIPENS  MPEN_COUNT		/* forward reference to an enumerator */
enum { MPEN_SHINE=0, MPEN_HALFSHINE, MPEN_COUNT=8 };

enum
{
	MUIKEY_RELEASE = -2,	/* not a real key, faked when MUIKEY_PRESS is released */
	MUIKEY_NONE    = -1,
	MUIKEY_PRESS,
	MUIKEY_TOGGLE,
};

#endif /* LIBRARIES_MUI_H */
