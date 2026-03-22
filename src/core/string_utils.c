#include "string_utils.h"
#include "mem.h"

#ifdef CL_WIDE_STRINGS

CL_Obj cl_string_widen(CL_Obj base_str)
{
    CL_String *s = (CL_String *)CL_OBJ_TO_PTR(base_str);
    uint32_t len = s->length;
    uint32_t i;
    CL_WideString *ws;
    CL_Obj result;

    CL_GC_PROTECT(base_str);
    result = cl_make_wide_string(NULL, len);
    CL_GC_UNPROTECT(1);

    /* Re-deref after allocation (GC may have moved nothing, but base_str
       is arena-relative so pointer is still valid) */
    s = (CL_String *)CL_OBJ_TO_PTR(base_str);
    ws = (CL_WideString *)CL_OBJ_TO_PTR(result);
    for (i = 0; i < len; i++)
        ws->data[i] = (uint32_t)(unsigned char)s->data[i];
    return result;
}

CL_Obj cl_string_narrow(CL_Obj wide_str)
{
    CL_WideString *ws = (CL_WideString *)CL_OBJ_TO_PTR(wide_str);
    uint32_t len = ws->length;
    uint32_t i;
    CL_String *s;
    CL_Obj result;

    /* Check if all characters fit in a byte */
    for (i = 0; i < len; i++) {
        if (ws->data[i] > 255)
            return wide_str;  /* Can't narrow */
    }

    CL_GC_PROTECT(wide_str);
    result = cl_make_string(NULL, len);
    CL_GC_UNPROTECT(1);

    ws = (CL_WideString *)CL_OBJ_TO_PTR(wide_str);
    s = (CL_String *)CL_OBJ_TO_PTR(result);
    for (i = 0; i < len; i++)
        s->data[i] = (char)ws->data[i];
    s->data[len] = '\0';
    return result;
}

#endif /* CL_WIDE_STRINGS */
