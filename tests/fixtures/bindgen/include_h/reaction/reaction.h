#ifndef REACTION_REACTION_H
#define REACTION_REACTION_H
/*
**	fixture header for tests/test_amiga_bindgen.sh — a twin-less header
**	that other headers #include (like the NDK's reaction/reaction.h); no
**	library claims it, so it becomes the header-only module
**	amiga/raw/reaction/reaction.
*/
#ifndef REACTION_Dummy
#define REACTION_Dummy (EXF_BASE + 0x5000)
#endif
#define REACTION_TextAttr (REACTION_Dummy + 5)

/* function-like: never a constant */
#define MAKE_ID(a,b,c,d) ((ULONG) (a)<<24 | (ULONG) (b)<<16 | (ULONG) (c)<<8 | (ULONG) (d))

#endif /* REACTION_REACTION_H */
