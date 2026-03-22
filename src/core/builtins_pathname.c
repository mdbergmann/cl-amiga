/*
 * builtins_pathname.c — Pathname type: parsing, construction, accessors
 *
 * CL-compliant structured pathnames with 6 components:
 *   host, device, directory, name, type, version
 *
 * Platform-aware: Amiga uses VOLUME:dir/file, POSIX uses /dir/file.
 */

#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "vm.h"
#include "../platform/platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Helper to register a builtin */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    s->value = fn;
}

/* ================================================================
 * Namestring parsing
 * ================================================================ */

/*
 * Parse a namestring into pathname components.
 * Returns a pathname object.
 *
 * Amiga:  "VOLUME:dir1/dir2/name.type"
 * POSIX:  "/dir1/dir2/name.type" or "dir1/dir2/name.type"
 */
CL_Obj cl_parse_namestring(const char *str, uint32_t len)
{
    CL_Obj host = CL_NIL;
    CL_Obj device = CL_NIL;
    CL_Obj directory = CL_NIL;
    CL_Obj pn_name = CL_NIL;
    CL_Obj pn_type = CL_NIL;
    CL_Obj version = CL_NIL;
    char expand_buf[1024];
    const char *p;
    const char *end;

    /* Expand leading ~ to home directory */
    if (len > 0 && str[0] == '~' && len < sizeof(expand_buf)) {
        char tmp[1024];
        const char *expanded;
        memcpy(tmp, str, len);
        tmp[len] = '\0';
        expanded = platform_expand_home(tmp, expand_buf, (int)sizeof(expand_buf));
        if (expanded != tmp) {
            str = expanded;
            len = (uint32_t)strlen(expanded);
        }
    }

    p = str;
    end = str + len;
    const char *dir_start;
    const char *filename;
    const char *last_slash;
    const char *dot;

    CL_GC_PROTECT(host);
    CL_GC_PROTECT(device);
    CL_GC_PROTECT(directory);
    CL_GC_PROTECT(pn_name);
    CL_GC_PROTECT(pn_type);
    CL_GC_PROTECT(version);

    /* Empty string -> empty pathname */
    if (len == 0) {
        CL_Obj result = cl_make_pathname(CL_NIL, CL_NIL, CL_NIL,
                                          CL_NIL, CL_NIL, CL_NIL);
        CL_GC_UNPROTECT(6);
        return result;
    }

    dir_start = p;

    /* Check for Amiga device (VOLUME:...) */
    {
        const char *colon = NULL;
        const char *scan;
        for (scan = p; scan < end; scan++) {
            if (*scan == ':') { colon = scan; break; }
            if (*scan == '/') break;  /* slash before colon = no device */
        }
        if (colon && colon > p) {
            device = cl_make_string(p, (uint32_t)(colon - p));
            dir_start = colon + 1;
        }
    }

    p = dir_start;

    /* Find last slash to separate directory from filename */
    last_slash = NULL;
    {
        const char *scan;
        for (scan = p; scan < end; scan++) {
            if (*scan == '/') last_slash = scan;
        }
    }

    /* Parse directory components */
    if (last_slash != NULL || (p < end && *p == '/')) {
        /* There is directory info */
        const char *dir_end = last_slash ? last_slash : p;
        const char *dp = p;
        CL_Obj dir_list = CL_NIL;
        CL_Obj dir_tail = CL_NIL;
        CL_Obj dir_kind;

        CL_GC_PROTECT(dir_list);
        CL_GC_PROTECT(dir_tail);

        /* Determine absolute vs relative */
        if (dp < end && *dp == '/') {
            dir_kind = KW_ABSOLUTE;
            dp++;
        } else if (!CL_NULL_P(device)) {
            /* Amiga: device present implies absolute (DH0:foo = absolute) */
            dir_kind = KW_ABSOLUTE;
        } else {
            dir_kind = KW_RELATIVE;
        }

        /* Start with (:ABSOLUTE) or (:RELATIVE) */
        dir_list = cl_cons(dir_kind, CL_NIL);
        dir_tail = dir_list;

        /* Parse directory components between slashes */
        while (dp <= dir_end && dp < end) {
            const char *comp_start = dp;
            while (dp < end && dp <= dir_end && *dp != '/') dp++;
            if (dp > comp_start) {
                CL_Obj comp;
                uint32_t clen = (uint32_t)(dp - comp_start);
                if (clen == 1 && *comp_start == '*')
                    comp = KW_WILD;
                else
                    comp = cl_make_string(comp_start, clen);
                {
                    CL_Obj cell = cl_cons(comp, CL_NIL);
                    ((CL_Cons *)CL_OBJ_TO_PTR(dir_tail))->cdr = cell;
                    dir_tail = cell;
                }
            }
            if (dp < end && *dp == '/') dp++;
        }

        directory = dir_list;
        CL_GC_UNPROTECT(2);

        /* Filename starts after last slash */
        filename = last_slash ? last_slash + 1 : end;
    } else if (!CL_NULL_P(device)) {
        /* Device-only path (e.g. "PROGDIR:", "S:") — absolute with no subdirs */
        directory = cl_cons(KW_ABSOLUTE, CL_NIL);
        filename = p;
    } else {
        /* No directory — just a filename */
        filename = p;
    }

    /* Parse filename: name.type */
    if (filename < end) {
        uint32_t flen = (uint32_t)(end - filename);
        /* Find last dot for type separation */
        dot = NULL;
        {
            const char *scan;
            for (scan = filename; scan < end; scan++) {
                if (*scan == '.') dot = scan;
            }
        }
        if (dot && dot > filename) {
            uint32_t nlen = (uint32_t)(dot - filename);
            if (nlen == 1 && *filename == '*')
                pn_name = KW_WILD;
            else
                pn_name = cl_make_string(filename, nlen);
            if (dot + 1 < end) {
                uint32_t tlen = (uint32_t)(end - dot - 1);
                if (tlen == 1 && *(dot + 1) == '*')
                    pn_type = KW_WILD;
                else
                    pn_type = cl_make_string(dot + 1, tlen);
            }
        } else if (flen > 0) {
            /* No extension */
            if (flen == 1 && *filename == '*')
                pn_name = KW_WILD;
            else
                pn_name = cl_make_string(filename, flen);
        }
    }

    {
        CL_Obj result = cl_make_pathname(host, device, directory,
                                          pn_name, pn_type, version);
        CL_GC_UNPROTECT(6);
        return result;
    }
}

/* ================================================================
 * Namestring construction
 * ================================================================ */

/*
 * Build a namestring from pathname components into a buffer.
 * Returns the length written (not including NUL).
 */
uint32_t cl_pathname_to_namestring(CL_Pathname *pn, char *buf, uint32_t bufsz)
{
    uint32_t pos = 0;

#define EMIT_CHAR(c) do { if (pos < bufsz - 1) buf[pos] = (c); pos++; } while(0)
#define EMIT_STR(s, slen) do { \
    uint32_t _i; \
    for (_i = 0; _i < (slen); _i++) EMIT_CHAR((s)[_i]); \
} while(0)

/* Check if a component is "empty" (NIL or :UNSPECIFIC) */
#define COMP_EMPTY(c) (CL_NULL_P(c) || (c) == KW_UNSPECIFIC)

    /* Device (Amiga: "DH0:") */
    if (!COMP_EMPTY(pn->device) && CL_STRING_P(pn->device)) {
        CL_String *dev = (CL_String *)CL_OBJ_TO_PTR(pn->device);
        EMIT_STR(dev->data, dev->length);
        EMIT_CHAR(':');
    }

    /* Directory */
    if (!COMP_EMPTY(pn->directory) && CL_CONS_P(pn->directory)) {
        CL_Obj dir = pn->directory;
        CL_Obj kind = cl_car(dir);
        CL_Obj rest = cl_cdr(dir);

        if (kind == KW_ABSOLUTE && CL_NULL_P(pn->device)) {
            EMIT_CHAR('/');
        }

        while (!CL_NULL_P(rest)) {
            CL_Obj comp = cl_car(rest);
            if (comp == KW_WILD) {
                EMIT_CHAR('*');
            } else if (CL_STRING_P(comp)) {
                CL_String *cs = (CL_String *)CL_OBJ_TO_PTR(comp);
                EMIT_STR(cs->data, cs->length);
            }
            rest = cl_cdr(rest);
            /* Always emit trailing slash for directory components */
            EMIT_CHAR('/');
        }
    }

    /* Name */
    if (pn->name == KW_WILD) {
        EMIT_CHAR('*');
    } else if (!COMP_EMPTY(pn->name) && CL_STRING_P(pn->name)) {
        CL_String *nm = (CL_String *)CL_OBJ_TO_PTR(pn->name);
        EMIT_STR(nm->data, nm->length);
    }

    /* Type (with dot) */
    if (pn->type == KW_WILD) {
        EMIT_CHAR('.');
        EMIT_CHAR('*');
    } else if (!COMP_EMPTY(pn->type) && CL_STRING_P(pn->type)) {
        CL_String *tp = (CL_String *)CL_OBJ_TO_PTR(pn->type);
        EMIT_CHAR('.');
        EMIT_STR(tp->data, tp->length);
    }

    if (pos < bufsz) buf[pos] = '\0';
    else if (bufsz > 0) buf[bufsz - 1] = '\0';

#undef EMIT_CHAR
#undef EMIT_STR

    return pos;
}

/*
 * Get namestring as a C string from either a string or pathname argument.
 * Returns pointer to a static buffer.
 */
const char *cl_coerce_to_namestring(CL_Obj arg, char *buf, uint32_t bufsz)
{
    if (CL_STRING_P(arg)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(arg);
        /* Expand leading ~ to home directory */
        return platform_expand_home(s->data, buf, (int)bufsz);
    }
    if (CL_PATHNAME_P(arg)) {
        CL_Pathname *pn = (CL_Pathname *)CL_OBJ_TO_PTR(arg);
        cl_pathname_to_namestring(pn, buf, bufsz);
        return buf;
    }
    cl_error(CL_ERR_TYPE, "Expected string or pathname");
    return "";
}

/* Coerce argument to pathname (string -> parse, pathname -> identity) */
static CL_Obj coerce_to_pathname(CL_Obj arg)
{
    if (CL_PATHNAME_P(arg)) return arg;
    if (CL_STRING_P(arg)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(arg);
        return cl_parse_namestring(s->data, s->length);
    }
    cl_error(CL_ERR_TYPE, "PATHNAME: expected string or pathname");
    return CL_NIL;
}

/* ================================================================
 * Builtins
 * ================================================================ */

/* (pathnamep thing) */
static CL_Obj bi_pathnamep(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return CL_PATHNAME_P(args[0]) ? SYM_T : CL_NIL;
}

/* (pathname thing) — coerce string or pathname to pathname */
static CL_Obj bi_pathname(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return coerce_to_pathname(args[0]);
}

/* (parse-namestring string) */
static CL_Obj bi_parse_namestring(CL_Obj *args, int n)
{
    CL_Obj arg = args[0];
    CL_UNUSED(n);
    if (CL_PATHNAME_P(arg)) return arg;
    if (CL_STRING_P(arg)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(arg);
        return cl_parse_namestring(s->data, s->length);
    }
    cl_error(CL_ERR_TYPE, "PARSE-NAMESTRING: expected string or pathname");
    return CL_NIL;
}

/* (namestring pathname) — return namestring as string */
static CL_Obj bi_namestring(CL_Obj *args, int n)
{
    char buf[1024];
    uint32_t len;
    CL_Obj pn_obj;
    CL_Pathname *pn;
    CL_UNUSED(n);

    pn_obj = coerce_to_pathname(args[0]);
    pn = (CL_Pathname *)CL_OBJ_TO_PTR(pn_obj);
    len = cl_pathname_to_namestring(pn, buf, sizeof(buf));
    return cl_make_string(buf, len);
}

/* --- Component accessors --- */

/* (pathname-host p) */
static CL_Obj bi_pathname_host(CL_Obj *args, int n)
{
    CL_Obj pn_obj;
    CL_UNUSED(n);
    pn_obj = coerce_to_pathname(args[0]);
    return ((CL_Pathname *)CL_OBJ_TO_PTR(pn_obj))->host;
}

/* (pathname-device p) */
static CL_Obj bi_pathname_device(CL_Obj *args, int n)
{
    CL_Obj pn_obj;
    CL_UNUSED(n);
    pn_obj = coerce_to_pathname(args[0]);
    return ((CL_Pathname *)CL_OBJ_TO_PTR(pn_obj))->device;
}

/* (pathname-directory p) */
static CL_Obj bi_pathname_directory(CL_Obj *args, int n)
{
    CL_Obj pn_obj;
    CL_UNUSED(n);
    pn_obj = coerce_to_pathname(args[0]);
    return ((CL_Pathname *)CL_OBJ_TO_PTR(pn_obj))->directory;
}

/* (pathname-name p) */
static CL_Obj bi_pathname_name(CL_Obj *args, int n)
{
    CL_Obj pn_obj;
    CL_UNUSED(n);
    pn_obj = coerce_to_pathname(args[0]);
    return ((CL_Pathname *)CL_OBJ_TO_PTR(pn_obj))->name;
}

/* (pathname-type p) */
static CL_Obj bi_pathname_type(CL_Obj *args, int n)
{
    CL_Obj pn_obj;
    CL_UNUSED(n);
    pn_obj = coerce_to_pathname(args[0]);
    return ((CL_Pathname *)CL_OBJ_TO_PTR(pn_obj))->type;
}

/* (pathname-version p) */
static CL_Obj bi_pathname_version(CL_Obj *args, int n)
{
    CL_Obj pn_obj;
    CL_UNUSED(n);
    pn_obj = coerce_to_pathname(args[0]);
    return ((CL_Pathname *)CL_OBJ_TO_PTR(pn_obj))->version;
}

/* (make-pathname &key host device directory name type version defaults) */
static CL_Obj bi_make_pathname(CL_Obj *args, int n)
{
    CL_Obj host = CL_NIL, device = CL_NIL, directory = CL_NIL;
    CL_Obj pn_name = CL_NIL, pn_type = CL_NIL, version = CL_NIL;
    /* Track which keywords were explicitly provided so NIL overrides defaults */
    int has_host = 0, has_device = 0, has_directory = 0;
    int has_name = 0, has_type = 0, has_version = 0;
    int i;

    for (i = 0; i + 1 < n; i += 2) {
        CL_Obj key = args[i];
        CL_Obj val = args[i + 1];
        if (key == KW_HOST) { host = val; has_host = 1; }
        else if (key == KW_DEVICE) { device = val; has_device = 1; }
        else if (key == KW_DIRECTORY) { directory = val; has_directory = 1; }
        else if (key == KW_NAME) { pn_name = val; has_name = 1; }
        else if (key == KW_TYPE) { pn_type = val; has_type = 1; }
        else if (key == KW_VERSION) { version = val; has_version = 1; }
        else if (key == KW_DEFAULTS) {
            /* Merge with defaults — only fill in components not explicitly provided.
               Per CL spec, :defaults accepts any pathname designator. */
            CL_Obj def_pn = coerce_to_pathname(val);
            CL_Pathname *def = (CL_Pathname *)CL_OBJ_TO_PTR(def_pn);
            if (!has_host) host = def->host;
            if (!has_device) device = def->device;
            if (!has_directory) directory = def->directory;
            if (!has_name) pn_name = def->name;
            if (!has_type) pn_type = def->type;
            if (!has_version) version = def->version;
        }
    }

    return cl_make_pathname(host, device, directory, pn_name, pn_type, version);
}

/* (merge-pathnames pathname &optional defaults default-version) */
static CL_Obj bi_merge_pathnames(CL_Obj *args, int n)
{
    CL_Obj pn_obj = coerce_to_pathname(args[0]);
    CL_Obj def_obj;
    CL_Pathname *pn, *def;
    CL_Obj host, device, directory, pn_name, pn_type, version;

    if (n >= 2) {
        def_obj = coerce_to_pathname(args[1]);
    } else {
        /* Use *default-pathname-defaults* */
        {
            CL_Obj dpd_val = cl_symbol_value(SYM_STAR_DEFAULT_PATHNAME_DEFAULTS);
            if (!CL_NULL_P(dpd_val) && CL_PATHNAME_P(dpd_val))
                def_obj = dpd_val;
            else
                def_obj = cl_make_pathname(CL_NIL, CL_NIL, CL_NIL, CL_NIL, CL_NIL, CL_NIL);
        }
    }

    pn = (CL_Pathname *)CL_OBJ_TO_PTR(pn_obj);
    def = (CL_Pathname *)CL_OBJ_TO_PTR(def_obj);

    /* Fill in missing components from defaults */
    host      = CL_NULL_P(pn->host)      ? def->host      : pn->host;
    device    = CL_NULL_P(pn->device)    ? def->device    : pn->device;
    pn_name   = CL_NULL_P(pn->name)     ? def->name      : pn->name;
    pn_type   = CL_NULL_P(pn->type)     ? def->type      : pn->type;
    version   = CL_NULL_P(pn->version)  ? def->version   : pn->version;

    /* Merge directories per CL spec: if pathname has :RELATIVE directory
       and defaults have a directory, append relative components to defaults */
    if (CL_NULL_P(pn->directory)) {
        directory = def->directory;
    } else if (!CL_NULL_P(def->directory) && CL_CONS_P(pn->directory) &&
               cl_car(pn->directory) == KW_RELATIVE) {
        /* Build merged list: copy def->directory, then append cdr(pn->directory) */
        CL_Obj merged = CL_NIL, tail = CL_NIL;
        CL_Obj src;
        CL_GC_PROTECT(pn_obj);
        CL_GC_PROTECT(def_obj);
        CL_GC_PROTECT(merged);
        CL_GC_PROTECT(tail);

        /* Copy all of def->directory */
        for (src = def->directory; CL_CONS_P(src); src = cl_cdr(src)) {
            CL_Obj cell = cl_cons(cl_car(src), CL_NIL);
            if (CL_NULL_P(merged)) {
                merged = cell;
                tail = cell;
            } else {
                ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                tail = cell;
            }
        }
        /* Append pn's relative components (skip :RELATIVE keyword) */
        for (src = cl_cdr(pn->directory); CL_CONS_P(src); src = cl_cdr(src)) {
            CL_Obj cell = cl_cons(cl_car(src), CL_NIL);
            if (CL_NULL_P(merged)) {
                merged = cell;
                tail = cell;
            } else {
                ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                tail = cell;
            }
        }
        directory = merged;
        CL_GC_UNPROTECT(4);
        /* Re-fetch pointers after potential GC */
        pn = (CL_Pathname *)CL_OBJ_TO_PTR(pn_obj);
        def = (CL_Pathname *)CL_OBJ_TO_PTR(def_obj);
    } else {
        directory = pn->directory;
    }

    if (n >= 3 && CL_NULL_P(version))
        version = args[2];

    return cl_make_pathname(host, device, directory, pn_name, pn_type, version);
}

/* (file-namestring pathname) — just the name.type part */
static CL_Obj bi_file_namestring(CL_Obj *args, int n)
{
    CL_Obj pn_obj;
    CL_Pathname *pn;
    char buf[512];
    uint32_t pos = 0;
    CL_UNUSED(n);

    pn_obj = coerce_to_pathname(args[0]);
    pn = (CL_Pathname *)CL_OBJ_TO_PTR(pn_obj);

    if (!COMP_EMPTY(pn->name) && CL_STRING_P(pn->name)) {
        CL_String *nm = (CL_String *)CL_OBJ_TO_PTR(pn->name);
        if (pos + nm->length < sizeof(buf)) {
            memcpy(buf + pos, nm->data, nm->length);
            pos += nm->length;
        }
    }
    if (!COMP_EMPTY(pn->type) && CL_STRING_P(pn->type)) {
        CL_String *tp = (CL_String *)CL_OBJ_TO_PTR(pn->type);
        if (pos + 1 + tp->length < sizeof(buf)) {
            buf[pos++] = '.';
            memcpy(buf + pos, tp->data, tp->length);
            pos += tp->length;
        }
    }
    buf[pos] = '\0';
    return cl_make_string(buf, pos);
}

/* (directory-namestring pathname) — just the directory part */
static CL_Obj bi_directory_namestring(CL_Obj *args, int n)
{
    CL_Obj pn_obj;
    CL_Pathname *pn;
    CL_Pathname temp;
    char buf[1024];
    uint32_t len;
    CL_UNUSED(n);

    pn_obj = coerce_to_pathname(args[0]);
    pn = (CL_Pathname *)CL_OBJ_TO_PTR(pn_obj);

    /* Build temp pathname with only directory (and device for Amiga) */
    memset(&temp, 0, sizeof(temp));
    temp.host = CL_NIL;
    temp.device = pn->device;
    temp.directory = pn->directory;
    temp.name = CL_NIL;
    temp.type = CL_NIL;
    temp.version = CL_NIL;

    len = cl_pathname_to_namestring(&temp, buf, sizeof(buf));
    return cl_make_string(buf, len);
}

/* (enough-namestring pathname &optional defaults) */
static CL_Obj bi_enough_namestring(CL_Obj *args, int n)
{
    /* Simplified: just return the namestring for now */
    char buf[1024];
    uint32_t len;
    CL_Obj pn_obj = coerce_to_pathname(args[0]);
    CL_Pathname *pn;
    CL_UNUSED(n);

    pn = (CL_Pathname *)CL_OBJ_TO_PTR(pn_obj);
    len = cl_pathname_to_namestring(pn, buf, sizeof(buf));
    return cl_make_string(buf, len);
}

/* ================================================================
 * Pathname equality (used by EQUAL)
 * ================================================================ */

int cl_pathname_equal(CL_Obj a, CL_Obj b)
{
    CL_Pathname *pa, *pb;
    if (!CL_PATHNAME_P(a) || !CL_PATHNAME_P(b)) return 0;
    pa = (CL_Pathname *)CL_OBJ_TO_PTR(a);
    pb = (CL_Pathname *)CL_OBJ_TO_PTR(b);

    /* Compare each component */
    /* host */
    if (pa->host != pb->host) {
        if (!CL_STRING_P(pa->host) || !CL_STRING_P(pb->host)) return 0;
        { CL_String *sa = (CL_String *)CL_OBJ_TO_PTR(pa->host);
          CL_String *sb = (CL_String *)CL_OBJ_TO_PTR(pb->host);
          if (sa->length != sb->length || memcmp(sa->data, sb->data, sa->length) != 0) return 0; }
    }
    /* device */
    if (pa->device != pb->device) {
        if (!CL_STRING_P(pa->device) || !CL_STRING_P(pb->device)) return 0;
        { CL_String *sa = (CL_String *)CL_OBJ_TO_PTR(pa->device);
          CL_String *sb = (CL_String *)CL_OBJ_TO_PTR(pb->device);
          if (sa->length != sb->length || memcmp(sa->data, sb->data, sa->length) != 0) return 0; }
    }
    /* name */
    if (pa->name != pb->name) {
        if (!CL_STRING_P(pa->name) || !CL_STRING_P(pb->name)) return 0;
        { CL_String *sa = (CL_String *)CL_OBJ_TO_PTR(pa->name);
          CL_String *sb = (CL_String *)CL_OBJ_TO_PTR(pb->name);
          if (sa->length != sb->length || memcmp(sa->data, sb->data, sa->length) != 0) return 0; }
    }
    /* type */
    if (pa->type != pb->type) {
        if (!CL_STRING_P(pa->type) || !CL_STRING_P(pb->type)) return 0;
        { CL_String *sa = (CL_String *)CL_OBJ_TO_PTR(pa->type);
          CL_String *sb = (CL_String *)CL_OBJ_TO_PTR(pb->type);
          if (sa->length != sb->length || memcmp(sa->data, sb->data, sa->length) != 0) return 0; }
    }
    /* version */
    if (pa->version != pb->version) return 0;
    /* directory: compare structurally via list equality */
    if (pa->directory != pb->directory) {
        CL_Obj da = pa->directory, db = pb->directory;
        while (CL_CONS_P(da) && CL_CONS_P(db)) {
            CL_Obj ca = cl_car(da), cb = cl_car(db);
            if (ca != cb) {
                if (!CL_STRING_P(ca) || !CL_STRING_P(cb)) return 0;
                { CL_String *sa = (CL_String *)CL_OBJ_TO_PTR(ca);
                  CL_String *sb = (CL_String *)CL_OBJ_TO_PTR(cb);
                  if (sa->length != sb->length || memcmp(sa->data, sb->data, sa->length) != 0) return 0; }
            }
            da = cl_cdr(da);
            db = cl_cdr(db);
        }
        if (da != db) return 0;  /* different lengths */
    }
    return 1;
}

/* (user-homedir-pathname &optional host) */
static CL_Obj bi_user_homedir_pathname(CL_Obj *args, int n)
{
    const char *home;
    CL_UNUSED(args);
    CL_UNUSED(n);
#ifdef PLATFORM_AMIGA
    home = "PROGDIR:";
#else
    home = getenv("HOME");
    if (!home) home = "/";
#endif
    {
        uint32_t hlen = (uint32_t)strlen(home);
        /* Ensure path ends with separator */
        if (hlen > 0 && home[hlen - 1] != '/'
#ifdef PLATFORM_AMIGA
            && home[hlen - 1] != ':'
#endif
           ) {
            char buf[512];
            if (hlen + 1 < sizeof(buf)) {
                memcpy(buf, home, hlen);
                buf[hlen] = '/';
                buf[hlen + 1] = '\0';
                return cl_parse_namestring(buf, hlen + 1);
            }
        }
        return cl_parse_namestring(home, hlen);
    }
}

/* wild-pathname-p — check for :WILD components */
static CL_Obj bi_wild_pathname_p(CL_Obj *args, int n)
{
    CL_Obj pn_obj = coerce_to_pathname(args[0]);
    CL_Pathname *pn = (CL_Pathname *)CL_OBJ_TO_PTR(pn_obj);
    CL_Obj field = (n >= 2 && !CL_NULL_P(args[1])) ? args[1] : CL_NIL;

    if (!CL_NULL_P(field)) {
        /* Check specific field */
        CL_Obj val = CL_NIL;
        if (field == KW_NAME)           val = pn->name;
        else if (field == KW_TYPE)      val = pn->type;
        else if (field == KW_DIRECTORY) {
            /* Check all directory components */
            if (CL_CONS_P(pn->directory)) {
                CL_Obj d = cl_cdr(pn->directory);
                while (CL_CONS_P(d)) {
                    if (cl_car(d) == KW_WILD) return CL_T;
                    d = cl_cdr(d);
                }
            }
            return CL_NIL;
        }
        else if (field == KW_HOST)      val = pn->host;
        else if (field == KW_DEVICE)    val = pn->device;
        else if (field == KW_VERSION)   val = pn->version;
        return (val == KW_WILD) ? CL_T : CL_NIL;
    }

    /* Check all components */
    if (pn->name == KW_WILD || pn->type == KW_WILD ||
        pn->host == KW_WILD || pn->device == KW_WILD ||
        pn->version == KW_WILD)
        return CL_T;
    if (CL_CONS_P(pn->directory)) {
        CL_Obj d = cl_cdr(pn->directory);
        while (CL_CONS_P(d)) {
            if (cl_car(d) == KW_WILD) return CL_T;
            d = cl_cdr(d);
        }
    }
    return CL_NIL;
}

/* pathname-match-p — simplified: compare namestrings for equality
 * (no wildcard support — just string equality) */
static CL_Obj bi_pathname_match_p(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    CL_Obj ns1 = bi_namestring(&args[0], 1);
    CL_Obj ns2 = bi_namestring(&args[1], 1);
    CL_String *s1 = (CL_String *)CL_OBJ_TO_PTR(ns1);
    CL_String *s2 = (CL_String *)CL_OBJ_TO_PTR(ns2);
    if (s1->length != s2->length) return CL_NIL;
    return memcmp(s1->data, s2->data, s1->length) == 0 ? CL_T : CL_NIL;
}

/* translate-pathname — simplified: just return the source pathname
 * (no wildcard translation support) */
static CL_Obj bi_translate_pathname(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    return coerce_to_pathname(args[0]);
}

/* ================================================================
 * Registration
 * ================================================================ */

void cl_builtins_pathname_init(void)
{
    defun("PATHNAMEP", bi_pathnamep, 1, 1);
    defun("PATHNAME", bi_pathname, 1, 1);
    defun("PARSE-NAMESTRING", bi_parse_namestring, 1, 1);
    defun("NAMESTRING", bi_namestring, 1, 1);
    defun("PATHNAME-HOST", bi_pathname_host, 1, 1);
    defun("PATHNAME-DEVICE", bi_pathname_device, 1, 1);
    defun("PATHNAME-DIRECTORY", bi_pathname_directory, 1, 1);
    defun("PATHNAME-NAME", bi_pathname_name, 1, 1);
    defun("PATHNAME-TYPE", bi_pathname_type, 1, 1);
    defun("PATHNAME-VERSION", bi_pathname_version, 1, 1);
    defun("MAKE-PATHNAME", bi_make_pathname, 0, -1);
    defun("MERGE-PATHNAMES", bi_merge_pathnames, 1, 3);
    defun("FILE-NAMESTRING", bi_file_namestring, 1, 1);
    defun("DIRECTORY-NAMESTRING", bi_directory_namestring, 1, 1);
    defun("ENOUGH-NAMESTRING", bi_enough_namestring, 1, 2);
    defun("USER-HOMEDIR-PATHNAME", bi_user_homedir_pathname, 0, 1);
    defun("WILD-PATHNAME-P", bi_wild_pathname_p, 1, 2);
    defun("PATHNAME-MATCH-P", bi_pathname_match_p, 2, 2);
    defun("TRANSLATE-PATHNAME", bi_translate_pathname, 3, 3);

    /* Initialize *default-pathname-defaults* to empty pathname */
    {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_DEFAULT_PATHNAME_DEFAULTS);
        s->value = cl_make_pathname(CL_NIL, CL_NIL, CL_NIL, CL_NIL, CL_NIL, CL_NIL);
    }
}
