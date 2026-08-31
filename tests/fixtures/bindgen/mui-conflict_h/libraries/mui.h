#ifndef LIBRARIES_MUI_H
#define LIBRARIES_MUI_H
/*
**	fixture header for tests/test_amiga_bindgen.sh -- an additive mui.h
**	whose value for an existing name DIFFERS from the 3.8 baseline and
**	is not a known evolution: the generator must name the conflict and
**	refuse to write any bindings.
*/

#define MUIA_Window_CloseRequest 0x12345678	/* the baseline says 0x8042e86e */

#endif /* LIBRARIES_MUI_H */
