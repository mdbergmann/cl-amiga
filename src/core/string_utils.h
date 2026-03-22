#ifndef CL_STRING_UTILS_H
#define CL_STRING_UTILS_H

/*
 * String accessor functions — abstract over base strings (TYPE_STRING)
 * and wide strings (TYPE_WIDE_STRING) for dual-string Unicode support.
 *
 * These should be used instead of direct s->data[i] access in code
 * that needs to handle both string types.
 */

#include "types.h"

/* Get string length (works for both base and wide strings) */
static inline uint32_t cl_string_length(CL_Obj str)
{
#ifdef CL_WIDE_STRINGS
    if (CL_WIDE_STRING_P(str))
        return ((CL_WideString *)CL_OBJ_TO_PTR(str))->length;
#endif
    return ((CL_String *)CL_OBJ_TO_PTR(str))->length;
}

/* Get character at index as code point (works for both types) */
static inline int cl_string_char_at(CL_Obj str, uint32_t idx)
{
#ifdef CL_WIDE_STRINGS
    if (CL_WIDE_STRING_P(str))
        return (int)((CL_WideString *)CL_OBJ_TO_PTR(str))->data[idx];
#endif
    return (int)(unsigned char)((CL_String *)CL_OBJ_TO_PTR(str))->data[idx];
}

/* Set character at index (works for both types) */
static inline void cl_string_set_char_at(CL_Obj str, uint32_t idx, int ch)
{
#ifdef CL_WIDE_STRINGS
    if (CL_WIDE_STRING_P(str)) {
        ((CL_WideString *)CL_OBJ_TO_PTR(str))->data[idx] = (uint32_t)ch;
        return;
    }
#endif
    ((CL_String *)CL_OBJ_TO_PTR(str))->data[idx] = (char)ch;
}

/* Check if string is a base string (TYPE_STRING) */
static inline int cl_string_is_base(CL_Obj str)
{
    return CL_STRING_P(str);
}

/* Get raw data pointer for base strings only.
 * Caller must ensure str is a base string (TYPE_STRING). */
static inline const char *cl_string_base_data(CL_Obj str)
{
    return ((CL_String *)CL_OBJ_TO_PTR(str))->data;
}

#ifdef CL_WIDE_STRINGS
/* Widen a base string to a wide string (allocates — may trigger GC).
 * Returns a new TYPE_WIDE_STRING. Caller must GC-protect inputs. */
CL_Obj cl_string_widen(CL_Obj base_str);

/* Narrow a wide string to a base string if all chars <= 255.
 * Returns TYPE_STRING if possible, or the original wide string if not. */
CL_Obj cl_string_narrow(CL_Obj wide_str);
#endif

#endif /* CL_STRING_UTILS_H */
