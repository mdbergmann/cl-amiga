#ifndef MUI_FIXED_MCC_H
#define MUI_FIXED_MCC_H
/*
**	fixture MUI custom-class header under the THIRD C-header root
**	(BINDGEN_MCC_INCLUDE_H -- where the .sh wrapper collects the headers of
**	the kit's ExtClasses/ and of MCC_HEADERS=<dir>), in the MUI/ spelling
**	the class kits use: amiga/raw/mui/fixed.  Its method struct embeds a
**	struct of libraries/mui.h, which the include below makes known.
*/

#ifndef LIBRARIES_MUI_H
#include "libraries/mui.h"
#endif

#define MUIC_Fixed "Fixed.mcc"

#define MUIM_Fixed_Layout        0x80030001
struct MUIP_Fixed_Layout { ULONG MethodID; struct MUI_MinMax mm; };

#define MUIA_Fixed_Width         0x80030010

#endif /* MUI_FIXED_MCC_H */
