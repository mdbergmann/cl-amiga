#ifndef LIBRARIES_MUI_H
#define LIBRARIES_MUI_H
/*
**	fixture header for tests/test_amiga_bindgen.sh -- the MUI 5 SDK's
**	libraries/mui.h (BINDGEN_MUI5_INCLUDE_H), an ADDITIVE constant
**	source next to the 3.8 baseline (mui-include_h): parsed in a FRESH
**	environment seeded with the .i symbols only, so its values are its
**	own; only the names the baseline does not define join the muimaster
**	table; a differing value for a shared name is fatal unless it is a
**	known evolution (the generator's *MUI-VALUE-EVOLUTIONS* -- the 3.8
**	value wins).  Structs are never taken from an additive header.
*/

#define MUIMASTER_NAME "muimaster.library"	/* agrees with the baseline: nothing to add */
#define MUIMASTER_VMIN 20			/* known evolution: the baseline's 11 wins */
#define MUIA_Window_CloseRequest 0x8042e86e	/* agrees: nothing to add */
#define MUIC_Window "Window.mui"		/* agrees: nothing to add */

#define MUIC_Panel "Panel.mui"			/* new string constant (the MorphOS header has it too: added once) */
#define MUIV_Fix_Both 7				/* new, in the MorphOS header too with the same value */
#define MUIA_Fix_New5 (EXB_MAGIC+1)		/* new; resolves against the .i symbols of the NDK environment */
#define MUIA_Fix_Bad (MUIM_Notify+1)		/* references a macro only the BASELINE mui.h defines: unresolvable in the fresh environment, dropped */
#define FixPanelObject MUI_NewObject(MUIC_Panel	/* a call: not a constant, skipped */
#define MUIF_Fix_NonConst sizeof(int)		/* not a constant either: skipped */

enum { FIXENUM5_A = 40, FIXENUM5_B };		/* new enumerators join too */

struct FixNew5 { ULONG fn5_A; };		/* additive structs are NOT taken */

#endif /* LIBRARIES_MUI_H */
