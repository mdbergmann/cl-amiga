#include "string_utils.h"
#include "mem.h"

CL_Obj cl_string_copy(CL_Obj str)
{
#ifdef CL_WIDE_STRINGS
    if (CL_WIDE_STRING_P(str)) {
        CL_WideString *ws = (CL_WideString *)CL_OBJ_TO_PTR(str);
        return cl_make_wide_string(ws->data, ws->length);
    }
#endif
    {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(str);
        return cl_make_string(s->data, s->length);
    }
}

CL_Obj cl_string_substring(CL_Obj str, uint32_t start, uint32_t end)
{
#ifdef CL_WIDE_STRINGS
    if (CL_WIDE_STRING_P(str)) {
        CL_WideString *ws = (CL_WideString *)CL_OBJ_TO_PTR(str);
        return cl_make_wide_string(ws->data + start, end - start);
    }
#endif
    {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(str);
        return cl_make_string(s->data + start, end - start);
    }
}

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

/* --- UTF-8 codec --- */

int cl_utf8_encode(int cp, char *buf)
{
    if (cp < 0) return 0;
    if (cp <= 0x7F) {
        buf[0] = (char)cp;
        return 1;
    }
    if (cp <= 0x7FF) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp <= 0xFFFF) {
        /* Reject surrogates (U+D800..U+DFFF) */
        if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;  /* Code point out of range */
}

int cl_utf8_decode(const unsigned char *buf, uint32_t buflen, int *codepoint)
{
    unsigned char b0;
    int cp, expected;

    if (buflen == 0) return 0;
    b0 = buf[0];

    if (b0 <= 0x7F) {
        *codepoint = b0;
        return 1;
    }
    if ((b0 & 0xE0) == 0xC0) {
        cp = b0 & 0x1F;
        expected = 2;
    } else if ((b0 & 0xF0) == 0xE0) {
        cp = b0 & 0x0F;
        expected = 3;
    } else if ((b0 & 0xF8) == 0xF0) {
        cp = b0 & 0x07;
        expected = 4;
    } else {
        /* Invalid lead byte or continuation byte */
        *codepoint = 0xFFFD;  /* replacement character */
        return 1;
    }

    if ((uint32_t)expected > buflen) {
        /* Truncated sequence */
        *codepoint = 0xFFFD;
        return 1;
    }

    {
        int i;
        for (i = 1; i < expected; i++) {
            if ((buf[i] & 0xC0) != 0x80) {
                /* Invalid continuation byte */
                *codepoint = 0xFFFD;
                return 1;
            }
            cp = (cp << 6) | (buf[i] & 0x3F);
        }
    }

    /* Reject overlong encodings */
    if ((expected == 2 && cp < 0x80) ||
        (expected == 3 && cp < 0x800) ||
        (expected == 4 && cp < 0x10000)) {
        *codepoint = 0xFFFD;
        return expected;
    }

    /* Reject surrogates and out-of-range */
    if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
        *codepoint = 0xFFFD;
        return expected;
    }

    *codepoint = cp;
    return expected;
}

uint32_t cl_wide_string_to_utf8(CL_Obj wide_str, char *buf, uint32_t buf_size)
{
    CL_WideString *ws = (CL_WideString *)CL_OBJ_TO_PTR(wide_str);
    uint32_t i, pos = 0;
    uint32_t limit = (buf_size > 0) ? buf_size - 1 : 0;

    for (i = 0; i < ws->length && pos < limit; i++) {
        char tmp[4];
        int nb = cl_utf8_encode((int)ws->data[i], tmp);
        int j;
        if (nb == 0) nb = 1, tmp[0] = '?';  /* fallback for invalid */
        if (pos + (uint32_t)nb > limit) break;
        for (j = 0; j < nb; j++)
            buf[pos++] = tmp[j];
    }
    if (buf_size > 0) buf[pos] = '\0';
    return pos;
}

CL_Obj cl_utf8_to_cl_string(const char *buf, uint32_t len)
{
    uint32_t i;
    int has_multibyte = 0;

    /* Quick scan: if all bytes <= 0x7F, it's pure ASCII → base string */
    for (i = 0; i < len; i++) {
        if ((unsigned char)buf[i] > 0x7F) {
            has_multibyte = 1;
            break;
        }
    }

    if (!has_multibyte)
        return cl_make_string(buf, len);

    /* Decode UTF-8 into a wide string */
    {
        /* First pass: count code points */
        uint32_t cp_count = 0;
        uint32_t pos = 0;
        while (pos < len) {
            int cp;
            int nb = cl_utf8_decode((const unsigned char *)buf + pos,
                                    len - pos, &cp);
            if (nb == 0) break;
            cp_count++;
            pos += (uint32_t)nb;
        }

        /* Allocate wide string and decode again */
        {
            CL_Obj result = cl_make_wide_string(NULL, cp_count);
            CL_WideString *ws = (CL_WideString *)CL_OBJ_TO_PTR(result);
            uint32_t ci = 0;
            pos = 0;
            while (pos < len && ci < cp_count) {
                int cp;
                int nb = cl_utf8_decode((const unsigned char *)buf + pos,
                                        len - pos, &cp);
                if (nb == 0) break;
                ws->data[ci++] = (uint32_t)cp;
                pos += (uint32_t)nb;
            }
            return result;
        }
    }
}

#endif /* CL_WIDE_STRINGS */
