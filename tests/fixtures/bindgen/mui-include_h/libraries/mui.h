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

struct MUIP_Notify { ULONG MethodID; ULONG TrigAttr; };	/* C structs are not read */

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
