/*
 * test_stream.c — Tests for stream heap type and platform file I/O
 */

#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/builtins.h"
#include "core/stream.h"
#include "core/reader.h"
#include "core/printer.h"
#include "platform/platform.h"
#include <string.h>

static void setup(void)
{
    platform_init();
    cl_error_init();
    cl_mem_init(CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
    cl_reader_init();
    cl_stream_init();
    cl_builtins_init();
}

static void teardown(void)
{
    cl_stream_shutdown();
    cl_mem_shutdown();
    platform_shutdown();
}

/* --- Stream allocation tests --- */

TEST(make_console_input_stream)
{
    CL_Obj s = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_CONSOLE);
    CL_Stream *st;
    ASSERT(!CL_NULL_P(s));
    ASSERT(CL_STREAM_P(s));
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    ASSERT_EQ_INT((int)st->direction, CL_STREAM_INPUT);
    ASSERT_EQ_INT((int)st->stream_type, CL_STREAM_CONSOLE);
    ASSERT(st->flags & CL_STREAM_FLAG_OPEN);
    ASSERT_EQ_INT((int)st->unread_char, -1);
}

TEST(make_console_output_stream)
{
    CL_Obj s = cl_make_stream(CL_STREAM_OUTPUT, CL_STREAM_CONSOLE);
    CL_Stream *st;
    ASSERT(!CL_NULL_P(s));
    ASSERT(CL_STREAM_P(s));
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    ASSERT_EQ_INT((int)st->direction, CL_STREAM_OUTPUT);
    ASSERT_EQ_INT((int)st->stream_type, CL_STREAM_CONSOLE);
}

TEST(make_file_stream)
{
    CL_Obj s = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_FILE);
    CL_Stream *st;
    ASSERT(!CL_NULL_P(s));
    ASSERT(CL_STREAM_P(s));
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    ASSERT_EQ_INT((int)st->stream_type, CL_STREAM_FILE);
}

TEST(make_string_stream)
{
    CL_Obj s = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_STRING);
    CL_Stream *st;
    ASSERT(!CL_NULL_P(s));
    ASSERT(CL_STREAM_P(s));
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    ASSERT_EQ_INT((int)st->stream_type, CL_STREAM_STRING);
}

TEST(stream_type_name)
{
    CL_Obj s = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_CONSOLE);
    ASSERT_STR_EQ(cl_type_name(s), "STREAM");
}

/* --- GC tests --- */

TEST(stream_gc_survives)
{
    CL_Obj s = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_CONSOLE);
    CL_Stream *st;
    int i;

    CL_GC_PROTECT(s);

    /* Create garbage */
    for (i = 0; i < 200; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    cl_gc();

    /* Stream should survive */
    ASSERT(CL_STREAM_P(s));
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    ASSERT_EQ_INT((int)st->direction, CL_STREAM_INPUT);

    CL_GC_UNPROTECT(1);
}

TEST(stream_gc_marks_string_buf)
{
    CL_Obj s = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_STRING);
    CL_Obj str = cl_make_string("hello world", 11);
    CL_Stream *st;
    int i;

    CL_GC_PROTECT(s);
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    st->string_buf = str;

    /* Create garbage */
    for (i = 0; i < 200; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    cl_gc();

    /* String buf should survive through the stream */
    st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    ASSERT(CL_STRING_P(st->string_buf));
    {
        CL_String *ss = (CL_String *)CL_OBJ_TO_PTR(st->string_buf);
        ASSERT_STR_EQ(ss->data, "hello world");
    }

    CL_GC_UNPROTECT(1);
}

/* --- Output buffer tests --- */

TEST(outbuf_alloc_free)
{
    uint32_t h = cl_stream_alloc_outbuf(256);
    ASSERT(h != 0);
    ASSERT(cl_stream_outbuf_data(h) != NULL);
    ASSERT_EQ_INT((int)cl_stream_outbuf_len(h), 0);
    cl_stream_free_outbuf(h);
}

TEST(outbuf_putchar)
{
    CL_Obj s = cl_make_stream(CL_STREAM_OUTPUT, CL_STREAM_STRING);
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    uint32_t h;

    h = cl_stream_alloc_outbuf(256);
    ASSERT(h != 0);
    st->out_buf_handle = h;

    cl_stream_outbuf_putchar(st, 'A');
    cl_stream_outbuf_putchar(st, 'B');
    cl_stream_outbuf_putchar(st, 'C');

    ASSERT_EQ_INT((int)cl_stream_outbuf_len(h), 3);
    ASSERT_STR_EQ(cl_stream_outbuf_data(h), "ABC");

    cl_stream_free_outbuf(h);
}

TEST(outbuf_write_string)
{
    CL_Obj s = cl_make_stream(CL_STREAM_OUTPUT, CL_STREAM_STRING);
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    uint32_t h;

    h = cl_stream_alloc_outbuf(256);
    ASSERT(h != 0);
    st->out_buf_handle = h;

    cl_stream_outbuf_write(st, "Hello ");
    cl_stream_outbuf_write(st, "World");

    ASSERT_EQ_INT((int)cl_stream_outbuf_len(h), 11);
    ASSERT_STR_EQ(cl_stream_outbuf_data(h), "Hello World");

    cl_stream_free_outbuf(h);
}

TEST(outbuf_growth)
{
    CL_Obj s = cl_make_stream(CL_STREAM_OUTPUT, CL_STREAM_STRING);
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    uint32_t h;
    int i;

    /* Start with a tiny buffer */
    h = cl_stream_alloc_outbuf(4);
    ASSERT(h != 0);
    st->out_buf_handle = h;

    /* Write more than 4 chars — should grow */
    for (i = 0; i < 100; i++)
        cl_stream_outbuf_putchar(st, 'x');

    ASSERT_EQ_INT((int)cl_stream_outbuf_len(h), 100);
    /* Verify all chars are correct */
    {
        char *data = cl_stream_outbuf_data(h);
        int ok = 1;
        for (i = 0; i < 100; i++) {
            if (data[i] != 'x') { ok = 0; break; }
        }
        ASSERT(ok);
    }

    cl_stream_free_outbuf(h);
}

TEST(outbuf_reset)
{
    uint32_t h = cl_stream_alloc_outbuf(256);
    CL_Obj s = cl_make_stream(CL_STREAM_OUTPUT, CL_STREAM_STRING);
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    st->out_buf_handle = h;

    cl_stream_outbuf_write(st, "data");
    ASSERT_EQ_INT((int)cl_stream_outbuf_len(h), 4);

    cl_stream_outbuf_reset(h);
    ASSERT_EQ_INT((int)cl_stream_outbuf_len(h), 0);
    ASSERT_STR_EQ(cl_stream_outbuf_data(h), "");

    cl_stream_free_outbuf(h);
}

/* --- Platform file I/O tests --- */

TEST(platform_file_write_read)
{
    PlatformFile wf, rf;
    int ch;
    const char *path = "/tmp/cl_test_stream.tmp";

    /* Write */
    wf = platform_file_open(path, PLATFORM_FILE_WRITE);
    ASSERT(wf != PLATFORM_FILE_INVALID);
    ASSERT(platform_file_write_string(wf, "Hello") == 0);
    ASSERT(platform_file_write_char(wf, '!') == 0);
    platform_file_close(wf);

    /* Read back */
    rf = platform_file_open(path, PLATFORM_FILE_READ);
    ASSERT(rf != PLATFORM_FILE_INVALID);

    ch = platform_file_getchar(rf);
    ASSERT_EQ_INT(ch, 'H');
    ch = platform_file_getchar(rf);
    ASSERT_EQ_INT(ch, 'e');
    ch = platform_file_getchar(rf);
    ASSERT_EQ_INT(ch, 'l');
    ch = platform_file_getchar(rf);
    ASSERT_EQ_INT(ch, 'l');
    ch = platform_file_getchar(rf);
    ASSERT_EQ_INT(ch, 'o');
    ch = platform_file_getchar(rf);
    ASSERT_EQ_INT(ch, '!');

    /* EOF */
    ch = platform_file_getchar(rf);
    ASSERT_EQ_INT(ch, -1);

    platform_file_close(rf);

    /* Cleanup */
    remove(path);
}

TEST(platform_file_open_nonexistent)
{
    PlatformFile rf = platform_file_open("/tmp/cl_nonexistent_xyz.tmp",
                                          PLATFORM_FILE_READ);
    ASSERT_EQ_INT((int)rf, (int)PLATFORM_FILE_INVALID);
}

TEST(platform_file_append)
{
    PlatformFile f;
    int ch;
    const char *path = "/tmp/cl_test_append.tmp";

    /* Write initial content */
    f = platform_file_open(path, PLATFORM_FILE_WRITE);
    ASSERT(f != PLATFORM_FILE_INVALID);
    platform_file_write_string(f, "AB");
    platform_file_close(f);

    /* Append more */
    f = platform_file_open(path, PLATFORM_FILE_APPEND);
    ASSERT(f != PLATFORM_FILE_INVALID);
    platform_file_write_string(f, "CD");
    platform_file_close(f);

    /* Read and verify */
    f = platform_file_open(path, PLATFORM_FILE_READ);
    ASSERT(f != PLATFORM_FILE_INVALID);
    ch = platform_file_getchar(f); ASSERT_EQ_INT(ch, 'A');
    ch = platform_file_getchar(f); ASSERT_EQ_INT(ch, 'B');
    ch = platform_file_getchar(f); ASSERT_EQ_INT(ch, 'C');
    ch = platform_file_getchar(f); ASSERT_EQ_INT(ch, 'D');
    ch = platform_file_getchar(f); ASSERT_EQ_INT(ch, -1);
    platform_file_close(f);

    remove(path);
}

/* --- typep test --- */

TEST(stream_typep)
{
    CL_Obj s = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_CONSOLE);
    CL_Obj type_sym = cl_intern("STREAM", 6);
    ASSERT(cl_typep(s, type_sym));

    /* Fixnums should not be streams */
    ASSERT(!cl_typep(CL_MAKE_FIXNUM(42), type_sym));
}

/* --- Console streams and standard stream variables --- */

TEST(console_streams_created)
{
    ASSERT(!CL_NULL_P(cl_stdin_stream));
    ASSERT(CL_STREAM_P(cl_stdin_stream));
    ASSERT(!CL_NULL_P(cl_stdout_stream));
    ASSERT(CL_STREAM_P(cl_stdout_stream));
    ASSERT(!CL_NULL_P(cl_stderr_stream));
    ASSERT(CL_STREAM_P(cl_stderr_stream));
}

TEST(standard_stream_variables_bound)
{
    CL_Symbol *s;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STANDARD_INPUT);
    ASSERT(CL_STREAM_P(s->value));
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STANDARD_OUTPUT);
    ASSERT(CL_STREAM_P(s->value));
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_ERROR_OUTPUT);
    ASSERT(CL_STREAM_P(s->value));
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_TRACE_OUTPUT);
    ASSERT(CL_STREAM_P(s->value));
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_DEBUG_IO);
    ASSERT(CL_STREAM_P(s->value));
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_QUERY_IO);
    ASSERT(CL_STREAM_P(s->value));
    s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_TERMINAL_IO);
    ASSERT(CL_STREAM_P(s->value));
}

TEST(standard_input_is_input)
{
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(cl_stdin_stream);
    ASSERT_EQ_INT((int)st->direction, CL_STREAM_INPUT);
}

TEST(standard_output_is_output)
{
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(cl_stdout_stream);
    ASSERT_EQ_INT((int)st->direction, CL_STREAM_OUTPUT);
}

TEST(interactive_stream_p_console)
{
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(cl_stdout_stream);
    ASSERT_EQ_INT((int)st->stream_type, CL_STREAM_CONSOLE);
    /* Console streams are interactive */
    ASSERT_EQ_INT((int)(st->stream_type == CL_STREAM_CONSOLE), 1);
}

TEST(interactive_stream_p_file)
{
    CL_Obj s = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_FILE);
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    ASSERT_EQ_INT((int)(st->stream_type == CL_STREAM_CONSOLE), 0);
}

TEST(interactive_stream_p_string)
{
    CL_Obj s = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_STRING);
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(s);
    ASSERT_EQ_INT((int)(st->stream_type == CL_STREAM_CONSOLE), 0);
}

TEST(console_streams_survive_gc)
{
    CL_Stream *st;
    int i;

    /* Create garbage to trigger GC */
    for (i = 0; i < 200; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    cl_gc();

    /* Console streams should survive (reachable via symbol values) */
    ASSERT(CL_STREAM_P(cl_stdin_stream));
    st = (CL_Stream *)CL_OBJ_TO_PTR(cl_stdin_stream);
    ASSERT_EQ_INT((int)st->direction, CL_STREAM_INPUT);

    ASSERT(CL_STREAM_P(cl_stdout_stream));
    st = (CL_Stream *)CL_OBJ_TO_PTR(cl_stdout_stream);
    ASSERT_EQ_INT((int)st->direction, CL_STREAM_OUTPUT);

    ASSERT(CL_STREAM_P(cl_stderr_stream));
    st = (CL_Stream *)CL_OBJ_TO_PTR(cl_stderr_stream);
    ASSERT_EQ_INT((int)st->direction, CL_STREAM_OUTPUT);
}

/* --- String input stream tests --- */

TEST(make_string_input_stream)
{
    CL_Obj str = cl_make_string("Hello", 5);
    CL_Obj s;
    int ch;
    CL_GC_PROTECT(str);
    s = cl_make_string_input_stream(str, 0, 5);
    CL_GC_UNPROTECT(1);

    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'H');
    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'e');
    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'l');
    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'l');
    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'o');
}

TEST(string_input_stream_eof)
{
    CL_Obj str = cl_make_string("AB", 2);
    CL_Obj s;
    int ch;
    CL_GC_PROTECT(str);
    s = cl_make_string_input_stream(str, 0, 2);
    CL_GC_UNPROTECT(1);

    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'A');
    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'B');
    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, -1);
}

TEST(string_input_stream_start_end)
{
    CL_Obj str = cl_make_string("Hello World", 11);
    CL_Obj s;
    int ch;
    CL_GC_PROTECT(str);
    s = cl_make_string_input_stream(str, 6, 11);
    CL_GC_UNPROTECT(1);

    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'W');
    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'o');
    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'r');
    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'l');
    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'd');
    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, -1);
}

TEST(string_output_stream_write_chars)
{
    CL_Obj s = cl_make_string_output_stream();
    CL_Obj result;

    cl_stream_write_char(s, 'A');
    cl_stream_write_char(s, 'B');
    cl_stream_write_char(s, 'C');

    CL_GC_PROTECT(s);
    result = cl_get_output_stream_string(s);
    CL_GC_UNPROTECT(1);
    ASSERT(CL_STRING_P(result));
    {
        CL_String *rs = (CL_String *)CL_OBJ_TO_PTR(result);
        ASSERT_EQ_INT((int)rs->length, 3);
        ASSERT_STR_EQ(rs->data, "ABC");
    }
}

TEST(string_output_stream_write_string)
{
    CL_Obj s = cl_make_string_output_stream();
    CL_Obj result;

    cl_stream_write_string(s, "Hello ", 6);
    cl_stream_write_string(s, "World", 5);

    CL_GC_PROTECT(s);
    result = cl_get_output_stream_string(s);
    CL_GC_UNPROTECT(1);
    ASSERT(CL_STRING_P(result));
    {
        CL_String *rs = (CL_String *)CL_OBJ_TO_PTR(result);
        ASSERT_EQ_INT((int)rs->length, 11);
        ASSERT_STR_EQ(rs->data, "Hello World");
    }
}

TEST(get_output_stream_string_resets)
{
    CL_Obj s = cl_make_string_output_stream();
    CL_Obj r1, r2;

    cl_stream_write_string(s, "first", 5);
    CL_GC_PROTECT(s);
    r1 = cl_get_output_stream_string(s);
    CL_GC_UNPROTECT(1);
    {
        CL_String *rs = (CL_String *)CL_OBJ_TO_PTR(r1);
        ASSERT_STR_EQ(rs->data, "first");
    }

    cl_stream_write_string(s, "second", 6);
    CL_GC_PROTECT(s);
    r2 = cl_get_output_stream_string(s);
    CL_GC_UNPROTECT(1);
    {
        CL_String *rs = (CL_String *)CL_OBJ_TO_PTR(r2);
        ASSERT_STR_EQ(rs->data, "second");
    }
}

TEST(unread_char_test)
{
    CL_Obj str = cl_make_string("XY", 2);
    CL_Obj s;
    int ch;
    CL_GC_PROTECT(str);
    s = cl_make_string_input_stream(str, 0, 2);
    CL_GC_UNPROTECT(1);

    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'X');

    cl_stream_unread_char(s, ch);

    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'X');
    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'Y');
}

TEST(peek_char_test)
{
    CL_Obj str = cl_make_string("AB", 2);
    CL_Obj s;
    int ch;
    CL_GC_PROTECT(str);
    s = cl_make_string_input_stream(str, 0, 2);
    CL_GC_UNPROTECT(1);

    ch = cl_stream_peek_char(s);
    ASSERT_EQ_INT(ch, 'A');
    /* Peek should not consume */
    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'A');
    ch = cl_stream_read_char(s);
    ASSERT_EQ_INT(ch, 'B');
}

TEST(close_stream)
{
    CL_Obj s = cl_make_string_output_stream();
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(s);

    ASSERT(st->flags & CL_STREAM_FLAG_OPEN);
    cl_stream_close(s);
    ASSERT(!(st->flags & CL_STREAM_FLAG_OPEN));

    /* Double close should be safe (no-op) */
    cl_stream_close(s);
    ASSERT(!(st->flags & CL_STREAM_FLAG_OPEN));
}

TEST(file_stream_write_read)
{
    const char *path = "/tmp/cl_test_stream_io.tmp";
    PlatformFile wf, rf;
    CL_Obj ws, rs;
    CL_Stream *wst, *rst;
    int ch;

    /* Write via stream API */
    wf = platform_file_open(path, PLATFORM_FILE_WRITE);
    ASSERT(wf != PLATFORM_FILE_INVALID);
    ws = cl_make_stream(CL_STREAM_OUTPUT, CL_STREAM_FILE);
    wst = (CL_Stream *)CL_OBJ_TO_PTR(ws);
    wst->handle_id = (uint32_t)wf;

    cl_stream_write_char(ws, 'H');
    cl_stream_write_char(ws, 'i');
    cl_stream_write_char(ws, '!');
    cl_stream_close(ws);

    /* Read via stream API */
    rf = platform_file_open(path, PLATFORM_FILE_READ);
    ASSERT(rf != PLATFORM_FILE_INVALID);
    rs = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_FILE);
    rst = (CL_Stream *)CL_OBJ_TO_PTR(rs);
    rst->handle_id = (uint32_t)rf;

    ch = cl_stream_read_char(rs);
    ASSERT_EQ_INT(ch, 'H');
    ch = cl_stream_read_char(rs);
    ASSERT_EQ_INT(ch, 'i');
    ch = cl_stream_read_char(rs);
    ASSERT_EQ_INT(ch, '!');
    ch = cl_stream_read_char(rs);
    ASSERT_EQ_INT(ch, -1);
    cl_stream_close(rs);

    remove(path);
}

TEST(charpos_tracking)
{
    CL_Obj s = cl_make_string_output_stream();
    CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(s);

    ASSERT_EQ_INT((int)st->charpos, 0);

    cl_stream_write_char(s, 'A');
    ASSERT_EQ_INT((int)st->charpos, 1);

    cl_stream_write_char(s, 'B');
    ASSERT_EQ_INT((int)st->charpos, 2);

    cl_stream_write_char(s, '\n');
    ASSERT_EQ_INT((int)st->charpos, 0);

    cl_stream_write_string(s, "XYZ", 3);
    ASSERT_EQ_INT((int)st->charpos, 3);

    cl_stream_write_string(s, "ab\ncd", 5);
    ASSERT_EQ_INT((int)st->charpos, 2);

    cl_stream_close(s);
}

TEST(read_line_string_stream)
{
    CL_Obj str = cl_make_string("Hello\nWorld", 11);
    CL_Obj s;
    CL_Obj line;
    CL_String *ls;
    char stack_buf[256];
    char *buf = stack_buf;

    CL_GC_PROTECT(str);
    s = cl_make_string_input_stream(str, 0, 11);
    CL_GC_UNPROTECT(1);

    /* Read first line */
    {
        uint32_t len = 0;
        int ch;
        for (;;) {
            ch = cl_stream_read_char(s);
            if (ch == -1 || ch == '\n') break;
            buf[len++] = (char)ch;
        }
        line = cl_make_string(buf, len);
        ls = (CL_String *)CL_OBJ_TO_PTR(line);
        ASSERT_STR_EQ(ls->data, "Hello");
    }

    /* Read second line */
    {
        uint32_t len = 0;
        int ch;
        for (;;) {
            ch = cl_stream_read_char(s);
            if (ch == -1 || ch == '\n') break;
            buf[len++] = (char)ch;
        }
        line = cl_make_string(buf, len);
        ls = (CL_String *)CL_OBJ_TO_PTR(line);
        ASSERT_STR_EQ(ls->data, "World");
    }
}

/* --- Reader + stream integration tests (Step 6) --- */

TEST(read_from_stream_simple_list)
{
    CL_Obj str = cl_make_string("(+ 1 2)", 7);
    CL_Obj s, result;
    char buf[256];

    CL_GC_PROTECT(str);
    s = cl_make_string_input_stream(str, 0, 7);
    CL_GC_UNPROTECT(1);

    result = cl_read_from_stream(s);
    ASSERT(!cl_reader_eof());
    ASSERT(CL_CONS_P(result));

    /* Print to verify structure */
    cl_prin1_to_string(result, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "(+ 1 2)");
}

TEST(read_from_stream_integer)
{
    CL_Obj str = cl_make_string("42", 2);
    CL_Obj s, result;

    CL_GC_PROTECT(str);
    s = cl_make_string_input_stream(str, 0, 2);
    CL_GC_UNPROTECT(1);

    result = cl_read_from_stream(s);
    ASSERT(!cl_reader_eof());
    ASSERT(CL_FIXNUM_P(result));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(result), 42);
}

TEST(read_from_stream_multiple_exprs)
{
    CL_Obj str = cl_make_string("1 2 3", 5);
    CL_Obj s, r1, r2, r3;

    CL_GC_PROTECT(str);
    s = cl_make_string_input_stream(str, 0, 5);
    CL_GC_UNPROTECT(1);

    r1 = cl_read_from_stream(s);
    ASSERT(!cl_reader_eof());
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r1), 1);

    r2 = cl_read_from_stream(s);
    ASSERT(!cl_reader_eof());
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r2), 2);

    r3 = cl_read_from_stream(s);
    ASSERT(!cl_reader_eof());
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r3), 3);

    /* Next read should hit EOF */
    cl_read_from_stream(s);
    ASSERT(cl_reader_eof());
}

TEST(read_from_stream_empty)
{
    CL_Obj str = cl_make_string("", 0);
    CL_Obj s;

    CL_GC_PROTECT(str);
    s = cl_make_string_input_stream(str, 0, 0);
    CL_GC_UNPROTECT(1);

    cl_read_from_stream(s);
    ASSERT(cl_reader_eof());
}

TEST(read_from_stream_string_literal)
{
    CL_Obj str = cl_make_string("\"hello\"", 7);
    CL_Obj s, result;
    CL_String *rs;

    CL_GC_PROTECT(str);
    s = cl_make_string_input_stream(str, 0, 7);
    CL_GC_UNPROTECT(1);

    result = cl_read_from_stream(s);
    ASSERT(!cl_reader_eof());
    ASSERT(CL_STRING_P(result));
    rs = (CL_String *)CL_OBJ_TO_PTR(result);
    ASSERT_STR_EQ(rs->data, "hello");
}

TEST(read_from_string_backward_compat)
{
    CL_ReadStream stream;
    CL_Obj r1, r2;

    stream.buf = "10 20";
    stream.pos = 0;
    stream.len = 5;
    stream.line = 1;

    r1 = cl_read_from_string(&stream);
    ASSERT(!cl_reader_eof());
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r1), 10);

    /* Position should have advanced */
    ASSERT(stream.pos > 0);

    r2 = cl_read_from_string(&stream);
    ASSERT(!cl_reader_eof());
    ASSERT_EQ_INT(CL_FIXNUM_VAL(r2), 20);
}

int main(void)
{
    test_init();
    setup();

    /* Stream allocation */
    RUN(make_console_input_stream);
    RUN(make_console_output_stream);
    RUN(make_file_stream);
    RUN(make_string_stream);
    RUN(stream_type_name);

    /* GC */
    RUN(stream_gc_survives);
    RUN(stream_gc_marks_string_buf);

    /* Output buffers */
    RUN(outbuf_alloc_free);
    RUN(outbuf_putchar);
    RUN(outbuf_write_string);
    RUN(outbuf_growth);
    RUN(outbuf_reset);

    /* Platform file I/O */
    RUN(platform_file_write_read);
    RUN(platform_file_open_nonexistent);
    RUN(platform_file_append);

    /* typep */
    RUN(stream_typep);

    /* Console streams and standard variables */
    RUN(console_streams_created);
    RUN(standard_stream_variables_bound);
    RUN(standard_input_is_input);
    RUN(standard_output_is_output);
    RUN(interactive_stream_p_console);
    RUN(interactive_stream_p_file);
    RUN(interactive_stream_p_string);
    RUN(console_streams_survive_gc);

    /* Stream I/O (Step 3) */
    RUN(make_string_input_stream);
    RUN(string_input_stream_eof);
    RUN(string_input_stream_start_end);
    RUN(string_output_stream_write_chars);
    RUN(string_output_stream_write_string);
    RUN(get_output_stream_string_resets);
    RUN(unread_char_test);
    RUN(peek_char_test);
    RUN(close_stream);
    RUN(file_stream_write_read);
    RUN(charpos_tracking);
    RUN(read_line_string_stream);

    /* Reader + stream integration (Step 6) */
    RUN(read_from_stream_simple_list);
    RUN(read_from_stream_integer);
    RUN(read_from_stream_multiple_exprs);
    RUN(read_from_stream_empty);
    RUN(read_from_stream_string_literal);
    RUN(read_from_string_backward_compat);

    teardown();
    REPORT();
}
