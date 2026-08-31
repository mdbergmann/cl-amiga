#ifndef LIBRARIES_MUI_H
#define LIBRARIES_MUI_H
/*
**	fixture header for tests/test_amiga_bindgen.sh -- the MorphOS SDK's
**	gg:os-include/libraries/mui.h (BINDGEN_MOS_MUI_INCLUDE_H), the
**	second additive source: a name an earlier additive source (the MUI 5
**	header) already added must carry the same value and is added once.
*/

#define MUIMASTER_VMIN 20			/* known evolution: the baseline's 11 wins */
#define MUIC_Panel "Panel.mui"			/* added by the MUI 5 header already */
#define MUIV_Fix_Both 7				/* same */
#define MUIA_Fix_MosOnly 0x80421111		/* new: this header's alone */

#endif /* LIBRARIES_MUI_H */
