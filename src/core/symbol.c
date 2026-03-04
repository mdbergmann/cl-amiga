#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "../platform/platform.h"
#include <string.h>

/* Well-known symbols */
CL_Obj SYM_T = CL_NIL;
CL_Obj SYM_NIL = CL_NIL;
CL_Obj SYM_QUOTE = CL_NIL;
CL_Obj SYM_IF = CL_NIL;
CL_Obj SYM_PROGN = CL_NIL;
CL_Obj SYM_LAMBDA = CL_NIL;
CL_Obj SYM_LET = CL_NIL;
CL_Obj SYM_LETSTAR = CL_NIL;
CL_Obj SYM_SETQ = CL_NIL;
CL_Obj SYM_DEFUN = CL_NIL;
CL_Obj SYM_DEFMACRO = CL_NIL;
CL_Obj SYM_FUNCTION = CL_NIL;
CL_Obj SYM_BLOCK = CL_NIL;
CL_Obj SYM_RETURN_FROM = CL_NIL;
CL_Obj SYM_QUASIQUOTE = CL_NIL;
CL_Obj SYM_UNQUOTE = CL_NIL;
CL_Obj SYM_UNQUOTE_SPLICING = CL_NIL;
CL_Obj SYM_AMP_REST = CL_NIL;
CL_Obj SYM_AMP_OPTIONAL = CL_NIL;
CL_Obj SYM_AMP_BODY = CL_NIL;
CL_Obj SYM_AND = CL_NIL;
CL_Obj SYM_OR = CL_NIL;
CL_Obj SYM_COND = CL_NIL;
CL_Obj SYM_DO = CL_NIL;
CL_Obj SYM_DOLIST = CL_NIL;
CL_Obj SYM_DOTIMES = CL_NIL;
CL_Obj SYM_RETURN = CL_NIL;
CL_Obj SYM_CASE = CL_NIL;
CL_Obj SYM_ECASE = CL_NIL;
CL_Obj SYM_TYPECASE = CL_NIL;
CL_Obj SYM_ETYPECASE = CL_NIL;
CL_Obj SYM_OTHERWISE = CL_NIL;
CL_Obj SYM_FLET = CL_NIL;
CL_Obj SYM_LABELS = CL_NIL;
CL_Obj SYM_AMP_KEY = CL_NIL;
CL_Obj SYM_AMP_ALLOW_OTHER_KEYS = CL_NIL;
CL_Obj KW_ALLOW_OTHER_KEYS = CL_NIL;
CL_Obj SYM_TAGBODY = CL_NIL;
CL_Obj SYM_GO = CL_NIL;
CL_Obj SYM_CATCH = CL_NIL;
CL_Obj SYM_THROW = CL_NIL;
CL_Obj SYM_UNWIND_PROTECT = CL_NIL;
CL_Obj SYM_MULTIPLE_VALUE_BIND = CL_NIL;
CL_Obj SYM_MULTIPLE_VALUE_LIST = CL_NIL;
CL_Obj SYM_MULTIPLE_VALUE_PROG1 = CL_NIL;
CL_Obj SYM_NTH_VALUE = CL_NIL;
CL_Obj SYM_DEFVAR = CL_NIL;
CL_Obj SYM_DEFPARAMETER = CL_NIL;
CL_Obj SYM_DEFCONSTANT = CL_NIL;
CL_Obj SYM_SETF = CL_NIL;
CL_Obj SYM_EVAL_WHEN = CL_NIL;
CL_Obj SYM_DESTRUCTURING_BIND = CL_NIL;
CL_Obj SYM_DEFSETF = CL_NIL;
CL_Obj SYM_DEFTYPE = CL_NIL;
CL_Obj SYM_DECLARE = CL_NIL;
CL_Obj SYM_DECLAIM = CL_NIL;
CL_Obj SYM_LOCALLY = CL_NIL;
CL_Obj SYM_TRACE = CL_NIL;
CL_Obj SYM_UNTRACE = CL_NIL;
CL_Obj SYM_TIME = CL_NIL;
CL_Obj SYM_IN_PACKAGE = CL_NIL;
CL_Obj SYM_STAR_PACKAGE = CL_NIL;
CL_Obj SYM_MACROLET = CL_NIL;
CL_Obj SYM_SYMBOL_MACROLET = CL_NIL;
CL_Obj SYM_THE = CL_NIL;

/* Declaration specifier symbols */
CL_Obj SYM_SPECIAL_DECL = CL_NIL;
CL_Obj SYM_TYPE_DECL = CL_NIL;
CL_Obj SYM_FTYPE_DECL = CL_NIL;
CL_Obj SYM_INLINE_DECL = CL_NIL;
CL_Obj SYM_NOTINLINE_DECL = CL_NIL;
CL_Obj SYM_OPTIMIZE_DECL = CL_NIL;
CL_Obj SYM_IGNORE_DECL = CL_NIL;
CL_Obj SYM_IGNORABLE_DECL = CL_NIL;
CL_Obj SYM_DYNAMIC_EXTENT_DECL = CL_NIL;

/* Optimize quality symbols */
CL_Obj SYM_SPEED = CL_NIL;
CL_Obj SYM_SAFETY = CL_NIL;
CL_Obj SYM_DEBUG = CL_NIL;
CL_Obj SYM_SPACE = CL_NIL;

/* Condition type name symbols */
CL_Obj SYM_CONDITION = CL_NIL;
CL_Obj SYM_WARNING = CL_NIL;
CL_Obj SYM_SERIOUS_CONDITION = CL_NIL;
CL_Obj SYM_ERROR_COND = CL_NIL;
CL_Obj SYM_SIMPLE_CONDITION = CL_NIL;
CL_Obj SYM_SIMPLE_ERROR = CL_NIL;
CL_Obj SYM_SIMPLE_WARNING = CL_NIL;
CL_Obj SYM_TYPE_ERROR = CL_NIL;
CL_Obj SYM_UNBOUND_VARIABLE_COND = CL_NIL;
CL_Obj SYM_UNDEFINED_FUNCTION_COND = CL_NIL;
CL_Obj SYM_PROGRAM_ERROR = CL_NIL;
CL_Obj SYM_CONTROL_ERROR = CL_NIL;
CL_Obj SYM_ARITHMETIC_ERROR = CL_NIL;
CL_Obj SYM_DIVISION_BY_ZERO = CL_NIL;

/* Signaling symbols */
CL_Obj SYM_SIGNAL = CL_NIL;
CL_Obj SYM_WARN = CL_NIL;
CL_Obj SYM_HANDLER_BIND = CL_NIL;
CL_Obj SYM_RESTART_CASE = CL_NIL;

/* Restart name symbols */
CL_Obj SYM_ABORT = CL_NIL;
CL_Obj SYM_CONTINUE = CL_NIL;
CL_Obj SYM_MUFFLE_WARNING = CL_NIL;
CL_Obj SYM_USE_VALUE = CL_NIL;
CL_Obj SYM_STORE_VALUE = CL_NIL;

/* Condition keyword symbols */
CL_Obj KW_FORMAT_CONTROL = CL_NIL;
CL_Obj KW_FORMAT_ARGUMENTS = CL_NIL;
CL_Obj KW_DATUM = CL_NIL;
CL_Obj KW_EXPECTED_TYPE = CL_NIL;

/* REPL history symbols */
CL_Obj SYM_STAR = CL_NIL;
CL_Obj SYM_STARSTAR = CL_NIL;
CL_Obj SYM_STARSTARSTAR = CL_NIL;
CL_Obj SYM_PLUS = CL_NIL;
CL_Obj SYM_PLUSPLUS = CL_NIL;
CL_Obj SYM_PLUSPLUSPLUS = CL_NIL;
CL_Obj SYM_MINUS = CL_NIL;

/* Standard stream variable symbols */
CL_Obj SYM_STANDARD_INPUT = CL_NIL;
CL_Obj SYM_STANDARD_OUTPUT = CL_NIL;
CL_Obj SYM_ERROR_OUTPUT = CL_NIL;
CL_Obj SYM_TRACE_OUTPUT = CL_NIL;
CL_Obj SYM_DEBUG_IO = CL_NIL;
CL_Obj SYM_QUERY_IO = CL_NIL;
CL_Obj SYM_TERMINAL_IO = CL_NIL;
CL_Obj SYM_STAR_FEATURES = CL_NIL;
CL_Obj SYM_STAR_READTABLE = CL_NIL;

/* Printer control variable symbols */
CL_Obj SYM_PRINT_ESCAPE = CL_NIL;
CL_Obj SYM_PRINT_READABLY = CL_NIL;
CL_Obj SYM_PRINT_BASE = CL_NIL;
CL_Obj SYM_PRINT_RADIX = CL_NIL;
CL_Obj SYM_PRINT_LEVEL = CL_NIL;
CL_Obj SYM_PRINT_LENGTH = CL_NIL;
CL_Obj SYM_PRINT_CASE = CL_NIL;
CL_Obj SYM_PRINT_GENSYM = CL_NIL;
CL_Obj SYM_PRINT_ARRAY = CL_NIL;
CL_Obj SYM_PRINT_CIRCLE = CL_NIL;
CL_Obj SYM_PRINT_PRETTY = CL_NIL;
CL_Obj SYM_PRINT_RIGHT_MARGIN = CL_NIL;
CL_Obj SYM_PRINT_PPRINT_DISPATCH = CL_NIL;
CL_Obj KW_UPCASE = CL_NIL;
CL_Obj KW_DOWNCASE = CL_NIL;
CL_Obj KW_CAPITALIZE = CL_NIL;

/* Boole operation constants */
CL_Obj SYM_BOOLE_CLR = CL_NIL;
CL_Obj SYM_BOOLE_SET = CL_NIL;
CL_Obj SYM_BOOLE_1 = CL_NIL;
CL_Obj SYM_BOOLE_2 = CL_NIL;
CL_Obj SYM_BOOLE_C1 = CL_NIL;
CL_Obj SYM_BOOLE_C2 = CL_NIL;
CL_Obj SYM_BOOLE_AND = CL_NIL;
CL_Obj SYM_BOOLE_IOR = CL_NIL;
CL_Obj SYM_BOOLE_XOR = CL_NIL;
CL_Obj SYM_BOOLE_EQV = CL_NIL;
CL_Obj SYM_BOOLE_NAND = CL_NIL;
CL_Obj SYM_BOOLE_NOR = CL_NIL;
CL_Obj SYM_BOOLE_ANDC1 = CL_NIL;
CL_Obj SYM_BOOLE_ANDC2 = CL_NIL;
CL_Obj SYM_BOOLE_ORC1 = CL_NIL;
CL_Obj SYM_BOOLE_ORC2 = CL_NIL;

CL_Obj SYM_RANDOM_STATE = CL_NIL;

/* Pathname */
CL_Obj SYM_PATHNAME = CL_NIL;
CL_Obj KW_ABSOLUTE = CL_NIL;
CL_Obj KW_RELATIVE = CL_NIL;
CL_Obj KW_NEWEST = CL_NIL;
CL_Obj KW_HOST = CL_NIL;
CL_Obj KW_DEVICE = CL_NIL;
CL_Obj KW_DIRECTORY = CL_NIL;
CL_Obj KW_NAME = CL_NIL;
CL_Obj KW_TYPE = CL_NIL;
CL_Obj KW_VERSION = CL_NIL;
CL_Obj KW_DEFAULTS = CL_NIL;
CL_Obj SYM_STAR_DEFAULT_PATHNAME_DEFAULTS = CL_NIL;

CL_Obj SYM_STAR_MODULES = CL_NIL;

CL_Obj KW_CL_AMIGA = CL_NIL;
CL_Obj KW_COMMON_LISP = CL_NIL;
CL_Obj KW_POSIX = CL_NIL;
CL_Obj KW_AMIGAOS = CL_NIL;
CL_Obj KW_M68K = CL_NIL;

/* FNV-1a hash */
uint32_t cl_hash_string(const char *str, uint32_t len)
{
    uint32_t hash = 2166136261u;
    uint32_t i;
    for (i = 0; i < len; i++) {
        hash ^= (uint8_t)str[i];
        hash *= 16777619u;
    }
    return hash;
}

CL_Obj cl_intern_in(const char *name, uint32_t len, CL_Obj package)
{
    CL_Obj existing;
    CL_Obj name_str, sym;
    CL_Symbol *s;

    /* Check if already interned */
    existing = cl_package_find_symbol(name, len, package);
    if (!CL_NULL_P(existing))
        return existing;

    /* Create new symbol */
    name_str = cl_make_string(name, len);
    CL_GC_PROTECT(name_str);

    sym = cl_make_symbol(name_str);
    CL_GC_UNPROTECT(1);

    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->hash = cl_hash_string(name, len);

    cl_package_add_symbol(package, sym);
    return sym;
}

CL_Obj cl_intern(const char *name, uint32_t len)
{
    return cl_intern_in(name, len, cl_current_package);
}

CL_Obj cl_find_symbol(const char *name, uint32_t len, CL_Obj package)
{
    return cl_package_find_symbol(name, len, package);
}

CL_Obj cl_intern_keyword(const char *name, uint32_t len)
{
    CL_Obj sym = cl_intern_in(name, len, cl_package_keyword);
    /* Keywords are self-evaluating constants */
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->value = sym;
    s->flags |= CL_SYM_CONSTANT;
    return sym;
}

CL_Obj cl_make_uninterned_symbol(CL_Obj name_str)
{
    CL_Obj sym;
    CL_Symbol *s;
    CL_String *name;

    CL_GC_PROTECT(name_str);
    sym = cl_make_symbol(name_str);
    CL_GC_UNPROTECT(1);

    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    name = (CL_String *)CL_OBJ_TO_PTR(name_str);
    s->hash = cl_hash_string(name->data, name->length);
    s->package = CL_NIL;  /* No home package */
    return sym;
}

int cl_symbol_specialp(CL_Obj sym)
{
    CL_Symbol *s;
    if (CL_NULL_P(sym)) return 0;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    return (s->flags & CL_SYM_SPECIAL) != 0;
}

const char *cl_symbol_name(CL_Obj sym)
{
    CL_Symbol *s;
    CL_String *name;
    if (CL_NULL_P(sym)) return "NIL";
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    name = (CL_String *)CL_OBJ_TO_PTR(s->name);
    return name->data;
}

void cl_symbol_init(void)
{
    /* Intern well-known symbols in CL package */
    SYM_T             = cl_intern_in("T", 1, cl_package_cl);
    SYM_NIL           = cl_intern_in("NIL", 3, cl_package_cl);
    /* T and NIL are constants per CL spec */
    {
        CL_Symbol *s;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_T);
        s->flags |= CL_SYM_CONSTANT;
        s->value = SYM_T;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_NIL);
        s->flags |= CL_SYM_CONSTANT;
        /* NIL's value is already 0 = CL_NIL */
    }
    SYM_QUOTE         = cl_intern_in("QUOTE", 5, cl_package_cl);
    SYM_IF            = cl_intern_in("IF", 2, cl_package_cl);
    SYM_PROGN         = cl_intern_in("PROGN", 5, cl_package_cl);
    SYM_LAMBDA        = cl_intern_in("LAMBDA", 6, cl_package_cl);
    SYM_LET           = cl_intern_in("LET", 3, cl_package_cl);
    SYM_LETSTAR       = cl_intern_in("LET*", 4, cl_package_cl);
    SYM_SETQ          = cl_intern_in("SETQ", 4, cl_package_cl);
    SYM_DEFUN         = cl_intern_in("DEFUN", 5, cl_package_cl);
    SYM_DEFMACRO      = cl_intern_in("DEFMACRO", 8, cl_package_cl);
    SYM_FUNCTION      = cl_intern_in("FUNCTION", 8, cl_package_cl);
    SYM_BLOCK         = cl_intern_in("BLOCK", 5, cl_package_cl);
    SYM_RETURN_FROM   = cl_intern_in("RETURN-FROM", 11, cl_package_cl);
    SYM_QUASIQUOTE    = cl_intern_in("QUASIQUOTE", 10, cl_package_cl);
    SYM_UNQUOTE       = cl_intern_in("UNQUOTE", 7, cl_package_cl);
    SYM_UNQUOTE_SPLICING = cl_intern_in("UNQUOTE-SPLICING", 16, cl_package_cl);
    SYM_AMP_REST      = cl_intern_in("&REST", 5, cl_package_cl);
    SYM_AMP_OPTIONAL  = cl_intern_in("&OPTIONAL", 9, cl_package_cl);
    SYM_AMP_BODY      = cl_intern_in("&BODY", 5, cl_package_cl);
    SYM_AND           = cl_intern_in("AND", 3, cl_package_cl);
    SYM_OR            = cl_intern_in("OR", 2, cl_package_cl);
    SYM_COND          = cl_intern_in("COND", 4, cl_package_cl);
    SYM_DO            = cl_intern_in("DO", 2, cl_package_cl);
    SYM_DOLIST        = cl_intern_in("DOLIST", 6, cl_package_cl);
    SYM_DOTIMES       = cl_intern_in("DOTIMES", 7, cl_package_cl);
    SYM_RETURN        = cl_intern_in("RETURN", 6, cl_package_cl);
    SYM_CASE          = cl_intern_in("CASE", 4, cl_package_cl);
    SYM_ECASE         = cl_intern_in("ECASE", 5, cl_package_cl);
    SYM_TYPECASE      = cl_intern_in("TYPECASE", 8, cl_package_cl);
    SYM_ETYPECASE     = cl_intern_in("ETYPECASE", 9, cl_package_cl);
    SYM_OTHERWISE     = cl_intern_in("OTHERWISE", 9, cl_package_cl);
    SYM_FLET          = cl_intern_in("FLET", 4, cl_package_cl);
    SYM_LABELS        = cl_intern_in("LABELS", 6, cl_package_cl);
    SYM_AMP_KEY       = cl_intern_in("&KEY", 4, cl_package_cl);
    SYM_AMP_ALLOW_OTHER_KEYS = cl_intern_in("&ALLOW-OTHER-KEYS", 17, cl_package_cl);
    KW_ALLOW_OTHER_KEYS = cl_intern_keyword("ALLOW-OTHER-KEYS", 16);
    SYM_TAGBODY        = cl_intern_in("TAGBODY", 7, cl_package_cl);
    SYM_GO             = cl_intern_in("GO", 2, cl_package_cl);
    SYM_CATCH          = cl_intern_in("CATCH", 5, cl_package_cl);
    SYM_THROW          = cl_intern_in("THROW", 5, cl_package_cl);
    SYM_UNWIND_PROTECT = cl_intern_in("UNWIND-PROTECT", 14, cl_package_cl);
    SYM_MULTIPLE_VALUE_BIND  = cl_intern_in("MULTIPLE-VALUE-BIND", 19, cl_package_cl);
    SYM_MULTIPLE_VALUE_LIST  = cl_intern_in("MULTIPLE-VALUE-LIST", 19, cl_package_cl);
    SYM_MULTIPLE_VALUE_PROG1 = cl_intern_in("MULTIPLE-VALUE-PROG1", 20, cl_package_cl);
    SYM_NTH_VALUE            = cl_intern_in("NTH-VALUE", 9, cl_package_cl);
    SYM_DEFVAR               = cl_intern_in("DEFVAR", 6, cl_package_cl);
    SYM_DEFPARAMETER         = cl_intern_in("DEFPARAMETER", 12, cl_package_cl);
    SYM_DEFCONSTANT          = cl_intern_in("DEFCONSTANT", 11, cl_package_cl);
    SYM_SETF                 = cl_intern_in("SETF", 4, cl_package_cl);
    SYM_EVAL_WHEN            = cl_intern_in("EVAL-WHEN", 9, cl_package_cl);
    SYM_DESTRUCTURING_BIND   = cl_intern_in("DESTRUCTURING-BIND", 18, cl_package_cl);
    SYM_DEFSETF              = cl_intern_in("DEFSETF", 7, cl_package_cl);
    SYM_DEFTYPE              = cl_intern_in("DEFTYPE", 7, cl_package_cl);
    SYM_DECLARE              = cl_intern_in("DECLARE", 7, cl_package_cl);
    SYM_DECLAIM              = cl_intern_in("DECLAIM", 7, cl_package_cl);
    SYM_LOCALLY              = cl_intern_in("LOCALLY", 7, cl_package_cl);
    SYM_TRACE                = cl_intern_in("TRACE", 5, cl_package_cl);
    SYM_UNTRACE              = cl_intern_in("UNTRACE", 7, cl_package_cl);
    SYM_TIME                 = cl_intern_in("TIME", 4, cl_package_cl);
    SYM_IN_PACKAGE           = cl_intern_in("IN-PACKAGE", 10, cl_package_cl);
    SYM_STAR_PACKAGE         = cl_intern_in("*PACKAGE*", 9, cl_package_cl);
    SYM_MACROLET             = cl_intern_in("MACROLET", 8, cl_package_cl);
    SYM_SYMBOL_MACROLET      = cl_intern_in("SYMBOL-MACROLET", 15, cl_package_cl);
    SYM_THE                  = cl_intern_in("THE", 3, cl_package_cl);

    /* Declaration specifier symbols */
    SYM_SPECIAL_DECL         = cl_intern_in("SPECIAL", 7, cl_package_cl);
    SYM_TYPE_DECL            = cl_intern_in("TYPE", 4, cl_package_cl);
    SYM_FTYPE_DECL           = cl_intern_in("FTYPE", 5, cl_package_cl);
    SYM_INLINE_DECL          = cl_intern_in("INLINE", 6, cl_package_cl);
    SYM_NOTINLINE_DECL       = cl_intern_in("NOTINLINE", 9, cl_package_cl);
    SYM_OPTIMIZE_DECL        = cl_intern_in("OPTIMIZE", 8, cl_package_cl);
    SYM_IGNORE_DECL          = cl_intern_in("IGNORE", 6, cl_package_cl);
    SYM_IGNORABLE_DECL       = cl_intern_in("IGNORABLE", 9, cl_package_cl);
    SYM_DYNAMIC_EXTENT_DECL  = cl_intern_in("DYNAMIC-EXTENT", 14, cl_package_cl);

    /* Optimize quality symbols */
    SYM_SPEED                = cl_intern_in("SPEED", 5, cl_package_cl);
    SYM_SAFETY               = cl_intern_in("SAFETY", 6, cl_package_cl);
    SYM_DEBUG                = cl_intern_in("DEBUG", 5, cl_package_cl);
    SYM_SPACE                = cl_intern_in("SPACE", 5, cl_package_cl);

    /* Condition type name symbols */
    SYM_CONDITION                = cl_intern_in("CONDITION", 9, cl_package_cl);
    SYM_WARNING                  = cl_intern_in("WARNING", 7, cl_package_cl);
    SYM_SERIOUS_CONDITION        = cl_intern_in("SERIOUS-CONDITION", 17, cl_package_cl);
    SYM_ERROR_COND               = cl_intern_in("ERROR", 5, cl_package_cl);
    SYM_SIMPLE_CONDITION         = cl_intern_in("SIMPLE-CONDITION", 16, cl_package_cl);
    SYM_SIMPLE_ERROR             = cl_intern_in("SIMPLE-ERROR", 12, cl_package_cl);
    SYM_SIMPLE_WARNING           = cl_intern_in("SIMPLE-WARNING", 14, cl_package_cl);
    SYM_TYPE_ERROR               = cl_intern_in("TYPE-ERROR", 10, cl_package_cl);
    SYM_UNBOUND_VARIABLE_COND    = cl_intern_in("UNBOUND-VARIABLE", 16, cl_package_cl);
    SYM_UNDEFINED_FUNCTION_COND  = cl_intern_in("UNDEFINED-FUNCTION", 18, cl_package_cl);
    SYM_PROGRAM_ERROR            = cl_intern_in("PROGRAM-ERROR", 13, cl_package_cl);
    SYM_CONTROL_ERROR            = cl_intern_in("CONTROL-ERROR", 13, cl_package_cl);
    SYM_ARITHMETIC_ERROR         = cl_intern_in("ARITHMETIC-ERROR", 16, cl_package_cl);
    SYM_DIVISION_BY_ZERO         = cl_intern_in("DIVISION-BY-ZERO", 16, cl_package_cl);

    /* Signaling symbols */
    SYM_SIGNAL                   = cl_intern_in("SIGNAL", 6, cl_package_cl);
    SYM_WARN                     = cl_intern_in("WARN", 4, cl_package_cl);
    SYM_HANDLER_BIND             = cl_intern_in("HANDLER-BIND", 12, cl_package_cl);
    SYM_RESTART_CASE             = cl_intern_in("RESTART-CASE", 12, cl_package_cl);

    /* Restart name symbols */
    SYM_ABORT                    = cl_intern_in("ABORT", 5, cl_package_cl);
    SYM_CONTINUE                 = cl_intern_in("CONTINUE", 8, cl_package_cl);
    SYM_MUFFLE_WARNING           = cl_intern_in("MUFFLE-WARNING", 14, cl_package_cl);
    SYM_USE_VALUE                = cl_intern_in("USE-VALUE", 9, cl_package_cl);
    SYM_STORE_VALUE              = cl_intern_in("STORE-VALUE", 11, cl_package_cl);

    /* Condition keyword symbols */
    KW_FORMAT_CONTROL            = cl_intern_keyword("FORMAT-CONTROL", 14);
    KW_FORMAT_ARGUMENTS          = cl_intern_keyword("FORMAT-ARGUMENTS", 16);
    KW_DATUM                     = cl_intern_keyword("DATUM", 5);
    KW_EXPECTED_TYPE             = cl_intern_keyword("EXPECTED-TYPE", 13);

    /* REPL history symbols — intern multi-char ones now;
       *, +, - will be looked up in cl_repl_init() after builtins register them */
    SYM_STARSTAR       = cl_intern_in("**", 2, cl_package_cl);
    SYM_STARSTARSTAR   = cl_intern_in("***", 3, cl_package_cl);
    SYM_PLUSPLUS        = cl_intern_in("++", 2, cl_package_cl);
    SYM_PLUSPLUSPLUS   = cl_intern_in("+++", 3, cl_package_cl);

    /* Standard stream variable symbols — mark as special */
    SYM_STANDARD_INPUT   = cl_intern_in("*STANDARD-INPUT*", 16, cl_package_cl);
    SYM_STANDARD_OUTPUT  = cl_intern_in("*STANDARD-OUTPUT*", 17, cl_package_cl);
    SYM_ERROR_OUTPUT     = cl_intern_in("*ERROR-OUTPUT*", 14, cl_package_cl);
    SYM_TRACE_OUTPUT     = cl_intern_in("*TRACE-OUTPUT*", 14, cl_package_cl);
    SYM_DEBUG_IO         = cl_intern_in("*DEBUG-IO*", 10, cl_package_cl);
    SYM_QUERY_IO         = cl_intern_in("*QUERY-IO*", 10, cl_package_cl);
    SYM_TERMINAL_IO      = cl_intern_in("*TERMINAL-IO*", 13, cl_package_cl);
    {
        CL_Symbol *s;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STANDARD_INPUT);
        s->flags |= CL_SYM_SPECIAL;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STANDARD_OUTPUT);
        s->flags |= CL_SYM_SPECIAL;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_ERROR_OUTPUT);
        s->flags |= CL_SYM_SPECIAL;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_TRACE_OUTPUT);
        s->flags |= CL_SYM_SPECIAL;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_DEBUG_IO);
        s->flags |= CL_SYM_SPECIAL;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_QUERY_IO);
        s->flags |= CL_SYM_SPECIAL;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_TERMINAL_IO);
        s->flags |= CL_SYM_SPECIAL;
    }

    /* Printer control variables — CL spec defaults */
    SYM_PRINT_ESCAPE   = cl_intern_in("*PRINT-ESCAPE*", 14, cl_package_cl);
    SYM_PRINT_READABLY = cl_intern_in("*PRINT-READABLY*", 16, cl_package_cl);
    SYM_PRINT_BASE     = cl_intern_in("*PRINT-BASE*", 12, cl_package_cl);
    SYM_PRINT_RADIX    = cl_intern_in("*PRINT-RADIX*", 13, cl_package_cl);
    SYM_PRINT_LEVEL    = cl_intern_in("*PRINT-LEVEL*", 13, cl_package_cl);
    SYM_PRINT_LENGTH   = cl_intern_in("*PRINT-LENGTH*", 14, cl_package_cl);
    SYM_PRINT_CASE     = cl_intern_in("*PRINT-CASE*", 12, cl_package_cl);
    SYM_PRINT_GENSYM   = cl_intern_in("*PRINT-GENSYM*", 14, cl_package_cl);
    SYM_PRINT_ARRAY    = cl_intern_in("*PRINT-ARRAY*", 13, cl_package_cl);
    SYM_PRINT_CIRCLE   = cl_intern_in("*PRINT-CIRCLE*", 14, cl_package_cl);
    SYM_PRINT_PRETTY   = cl_intern_in("*PRINT-PRETTY*", 14, cl_package_cl);
    SYM_PRINT_RIGHT_MARGIN = cl_intern_in("*PRINT-RIGHT-MARGIN*", 20, cl_package_cl);
    KW_UPCASE          = cl_intern_keyword("UPCASE", 6);
    KW_DOWNCASE        = cl_intern_keyword("DOWNCASE", 8);
    KW_CAPITALIZE      = cl_intern_keyword("CAPITALIZE", 10);
    {
        CL_Symbol *s;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_ESCAPE);
        s->flags |= CL_SYM_SPECIAL; s->value = SYM_T;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_READABLY);
        s->flags |= CL_SYM_SPECIAL; s->value = CL_NIL;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_BASE);
        s->flags |= CL_SYM_SPECIAL; s->value = CL_MAKE_FIXNUM(10);
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_RADIX);
        s->flags |= CL_SYM_SPECIAL; s->value = CL_NIL;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_LEVEL);
        s->flags |= CL_SYM_SPECIAL; s->value = CL_NIL;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_LENGTH);
        s->flags |= CL_SYM_SPECIAL; s->value = CL_NIL;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_CASE);
        s->flags |= CL_SYM_SPECIAL; s->value = KW_UPCASE;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_GENSYM);
        s->flags |= CL_SYM_SPECIAL; s->value = SYM_T;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_ARRAY);
        s->flags |= CL_SYM_SPECIAL; s->value = SYM_T;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_CIRCLE);
        s->flags |= CL_SYM_SPECIAL; s->value = CL_NIL;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_PRETTY);
        s->flags |= CL_SYM_SPECIAL; s->value = CL_NIL;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_RIGHT_MARGIN);
        s->flags |= CL_SYM_SPECIAL; s->value = CL_NIL;
    }

    /* *PRINT-PPRINT-DISPATCH* — default is NIL (no custom dispatch) */
    SYM_PRINT_PPRINT_DISPATCH = cl_intern_in("*PRINT-PPRINT-DISPATCH*", 23, cl_package_cl);
    {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_PRINT_PPRINT_DISPATCH);
        s->flags |= CL_SYM_SPECIAL; s->value = CL_NIL;
    }

    /* *RANDOM-STATE* — default random state */
    SYM_RANDOM_STATE = cl_intern_in("*RANDOM-STATE*", 14, cl_package_cl);
    {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_RANDOM_STATE);
        s->flags |= CL_SYM_SPECIAL;
        s->value = cl_make_random_state(platform_time_ms() ^ platform_universal_time());
    }

    /* Pathname symbols */
    SYM_PATHNAME = cl_intern_in("PATHNAME", 8, cl_package_cl);
    KW_ABSOLUTE  = cl_intern_keyword("ABSOLUTE", 8);
    KW_RELATIVE  = cl_intern_keyword("RELATIVE", 8);
    KW_NEWEST    = cl_intern_keyword("NEWEST", 6);
    KW_HOST      = cl_intern_keyword("HOST", 4);
    KW_DEVICE    = cl_intern_keyword("DEVICE", 6);
    KW_DIRECTORY = cl_intern_keyword("DIRECTORY", 9);
    KW_NAME      = cl_intern_keyword("NAME", 4);
    KW_TYPE      = cl_intern_keyword("TYPE", 4);
    KW_VERSION   = cl_intern_keyword("VERSION", 7);
    KW_DEFAULTS  = cl_intern_keyword("DEFAULTS", 8);
    SYM_STAR_DEFAULT_PATHNAME_DEFAULTS = cl_intern_in("*DEFAULT-PATHNAME-DEFAULTS*", 27, cl_package_cl);
    {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_DEFAULT_PATHNAME_DEFAULTS);
        s->flags |= CL_SYM_SPECIAL;
        /* Value set later in cl_builtins_pathname_init after pathname type is available */
    }

    /* *MODULES* — list of provided module names */
    SYM_STAR_MODULES = cl_intern_in("*MODULES*", 9, cl_package_cl);
    {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_MODULES);
        s->flags |= CL_SYM_SPECIAL;
        s->value = CL_NIL;
    }

    /* *FEATURES* — feature list for #+ / #- */
    SYM_STAR_FEATURES = cl_intern_in("*FEATURES*", 10, cl_package_cl);
    KW_CL_AMIGA    = cl_intern_keyword("CL-AMIGA", 8);
    KW_COMMON_LISP = cl_intern_keyword("COMMON-LISP", 11);
    KW_POSIX       = cl_intern_keyword("POSIX", 5);
    KW_AMIGAOS     = cl_intern_keyword("AMIGAOS", 7);
    KW_M68K        = cl_intern_keyword("M68K", 4);
    {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_FEATURES);
        CL_Obj features = CL_NIL;
        s->flags |= CL_SYM_SPECIAL;
#ifdef PLATFORM_AMIGA
        features = cl_cons(KW_M68K, features);
        features = cl_cons(KW_AMIGAOS, features);
#else
        features = cl_cons(KW_POSIX, features);
#endif
        features = cl_cons(KW_COMMON_LISP, features);
        features = cl_cons(KW_CL_AMIGA, features);
        s->value = features;
    }

    /* T is self-evaluating */
    {
        CL_Symbol *t = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_T);
        t->value = SYM_T;
    }

    /* NIL's value is NIL */
    {
        CL_Symbol *n = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_NIL);
        n->value = CL_NIL;
    }

    /* Set the global CL_T for use by type predicates */
    CL_T = SYM_T;

    /* *PACKAGE* is a special variable initialized to CL-USER */
    {
        CL_Symbol *pkg_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_PACKAGE);
        pkg_sym->value = cl_current_package;
        pkg_sym->flags |= CL_SYM_SPECIAL;
    }

    /* *READTABLE* — readtable pool index, initial = 1 (user-modifiable copy) */
    SYM_STAR_READTABLE = cl_intern_in("*READTABLE*", 11, cl_package_cl);
    {
        CL_Symbol *rt_sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_READTABLE);
        rt_sym->value = CL_MAKE_FIXNUM(1);
        rt_sym->flags |= CL_SYM_SPECIAL;
    }

    /* Export all CL symbols so CL-USER inherits them.
       Also called again after cl_builtins_init() for builtin names. */
    cl_package_export_all_cl_symbols();
}
