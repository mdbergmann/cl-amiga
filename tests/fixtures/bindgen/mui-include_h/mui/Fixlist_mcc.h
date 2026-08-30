#ifndef MUI_FIXLIST_MCC_H
#define MUI_FIXLIST_MCC_H
/*
**	fixture MUI custom-class header (the shape of the kit's Tron_mcc.h,
**	NList_mcc.h ...) under the MUI root's mui/ directory: a header-only
**	module of its own, amiga/raw/mui/fixlist -- the class opens by name
**	through MUI_NewObjectA, so there is no library base.  The include of
**	libraries/mui.h makes its constants known but does not copy them here.
*/

#ifndef LIBRARIES_MUI_H
#include "libraries/mui.h"
#endif

#define MUIC_Fixlist "Fixlist.mcc"
#define FixlistObject MUI_NewObject(MUIC_Fixlist

/*** Methods ***/
#define MUIM_Fixlist_Clear       0x80020001
#define MUIM_Fixlist_Insert      0x80020002

/*** Method structs ***/
struct MUIP_Fixlist_Insert { ULONG MethodID; APTR *entries; LONG count; LONG pos; };

/*** Special method values ***/
#define MUIV_Fixlist_Insert_Top     0
#define MUIV_Fixlist_Insert_Bottom  (-2)

/*** Attributes ***/
#define MUIA_Fixlist_Active      0x80020010
#define MUIA_Fixlist_Entries     0x80020011

#endif /* MUI_FIXLIST_MCC_H */
