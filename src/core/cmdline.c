/*
 * cmdline.c — see cmdline.h.
 */
#include "cmdline.h"
#include "mem.h"
#include "symbol.h"
#include "package.h"
#include <string.h>

/* ---------------------------------------------------------------
 * The argv builder (pure C, host-tested by tests/test_cmdline.c)
 * --------------------------------------------------------------- */

void cl_argv_builder_init(CL_ArgvBuilder *b, char *buf, int bufsize,
                          char **argv, int argv_max)
{
    b->buf = buf;
    b->bufsize = bufsize;
    b->bufpos = 0;
    b->argv = argv;
    b->argv_max = argv_max;
    b->argc = 0;
    b->overflow = 0;
    if (argv_max > 0)
        argv[0] = NULL;
}

/* Append LEN bytes of TEXT as one argument. */
static int builder_add_n(CL_ArgvBuilder *b, const char *text, int len)
{
    if (b->argc + 1 >= b->argv_max || b->bufpos + len + 1 > b->bufsize) {
        b->overflow = 1;
        return 0;
    }
    memcpy(b->buf + b->bufpos, text, (size_t)len);
    b->buf[b->bufpos + len] = '\0';
    b->argv[b->argc++] = b->buf + b->bufpos;
    b->argv[b->argc] = NULL;
    b->bufpos += len + 1;
    return 1;
}

int cl_argv_builder_add(CL_ArgvBuilder *b, const char *arg)
{
    if (!arg)
        return 0;
    return builder_add_n(b, arg, (int)strlen(arg));
}

int cl_argv_builder_split(CL_ArgvBuilder *b, const char *line)
{
    int added = 0;
    const char *p = line;
    if (!p)
        return 0;
    for (;;) {
        const char *start;
        int len;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;
        if (*p == '"') {
            start = ++p;
            while (*p != '\0' && *p != '"')
                p++;
            len = (int)(p - start);
            if (*p == '"')
                p++;
        } else {
            start = p;
            while (*p != '\0' && *p != ' ' && *p != '\t')
                p++;
            len = (int)(p - start);
        }
        if (!builder_add_n(b, start, len))
            break;
        added++;
    }
    return added;
}

/* ---------------------------------------------------------------
 * The Lisp surface
 * --------------------------------------------------------------- */

static CL_Obj SYM_COMMAND_LINE_ARGS = CL_NIL;   /* EXT:*COMMAND-LINE-ARGS* */
static CL_Obj SYM_WORKBENCH_STARTED_P = CL_NIL; /* EXT:*WORKBENCH-STARTED-P* */

static CL_Obj defvar_ext(const char *name, CL_Obj *slot)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_ext);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->flags |= CL_SYM_SPECIAL;
    s->value = CL_NIL;
    *slot = sym;
    cl_gc_register_root(slot);
    cl_export_symbol(sym, cl_package_ext);
    return sym;
}

void cl_cmdline_builtins_init(void)
{
    defvar_ext("*COMMAND-LINE-ARGS*", &SYM_COMMAND_LINE_ARGS);
    defvar_ext("*WORKBENCH-STARTED-P*", &SYM_WORKBENCH_STARTED_P);
}

void cl_cmdline_publish(int n, char **args, int workbench_started)
{
    CL_Obj list = CL_NIL;
    int i;
    /* GC SAFETY: built back to front, one cons per argument, each
     * cl_make_string and cl_cons a collection point — the partial list
     * must be a forwarded root. */
    CL_GC_PROTECT(list);
    for (i = n - 1; i >= 0; i--) {
        CL_Obj s = cl_make_string(args[i], (uint32_t)strlen(args[i]));
        list = cl_cons(s, list);
    }
    if (CL_SYMBOL_P(SYM_COMMAND_LINE_ARGS))
        ((CL_Symbol *)CL_OBJ_TO_PTR(SYM_COMMAND_LINE_ARGS))->value = list;
    if (CL_SYMBOL_P(SYM_WORKBENCH_STARTED_P))
        ((CL_Symbol *)CL_OBJ_TO_PTR(SYM_WORKBENCH_STARTED_P))->value =
            workbench_started ? SYM_T : CL_NIL;
    CL_GC_UNPROTECT(1);  /* list */
}
