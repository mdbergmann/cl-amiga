#ifndef LIBRARIES_EXAMPLE_H
#define LIBRARIES_EXAMPLE_H
/*
**	fixture header for tests/test_amiga_bindgen.sh — the C twin of
**	include/libraries/example.i.  A header WITH a .i twin is never read:
**	nothing below may surface in the generated output.
*/
#define EX_SHOULD_NOT_APPEAR	1
#define EXF_BASE		0x9999	/* would clash with example.i's value */

#endif /* LIBRARIES_EXAMPLE_H */
