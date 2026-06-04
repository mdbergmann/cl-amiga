#ifndef CL_ASCII_CTYPE_H
#define CL_ASCII_CTYPE_H

/*
 * Locale-independent ASCII character classification.
 *
 * The Common Lisp reader and printer operate on the ASCII range with
 * fixed, locale-independent semantics (CLHS 2.1.3 defines the standard
 * whitespace[2] characters explicitly), so they must NOT depend on the C
 * locale or on libc's <ctype.h> classification table.
 *
 * This matters on minimal AmigaOS / MorphOS -noixemul C libraries, whose
 * <ctype.h> table is not reliably initialized: isspace(' ') can return 0,
 * which silently breaks the reader (skip_whitespace never advances, so the
 * tokenizer lands on a space and reports "Unexpected end of input").
 *
 * Use these helpers instead of <ctype.h> in reader/printer code.  They are
 * pure ASCII and behave identically on every platform.
 */

/* CL whitespace[2]: Space, Tab, Newline (LF), Return (CR), Page (FF), plus
 * vertical-tab for parity with C isspace(). */
static inline int cl_ascii_isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\r' || c == '\f' || c == '\v';
}

static inline int cl_ascii_isdigit(int c)
{
    return c >= '0' && c <= '9';
}

static inline int cl_ascii_isalpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/* ASCII upcase only — leaves non-[a-z] (including non-ASCII) untouched,
 * matching the standard readtable's case conversion. */
static inline int cl_ascii_toupper(int c)
{
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

#endif /* CL_ASCII_CTYPE_H */
