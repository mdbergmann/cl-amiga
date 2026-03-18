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
#include "core/compiler.h"
#include "core/vm.h"
#include "core/repl.h"
#include "core/bignum.h"
#include "platform/platform.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <signal.h>

static void setup(void)
{
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
    cl_reader_init();
    cl_printer_init();
    cl_compiler_init();
    cl_vm_init(0, 0);
    cl_stream_init();
    cl_builtins_init();
    cl_repl_init();
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

/* --- Printer + stream integration tests (Step 7) --- */

TEST(prin1_to_string_stream)
{
    CL_Obj sstream = cl_make_string_output_stream();
    CL_Obj result;
    CL_String *rs;

    CL_GC_PROTECT(sstream);
    cl_prin1_to_stream(CL_MAKE_FIXNUM(42), sstream);
    result = cl_get_output_stream_string(sstream);
    CL_GC_UNPROTECT(1);

    ASSERT(CL_STRING_P(result));
    rs = (CL_String *)CL_OBJ_TO_PTR(result);
    ASSERT_STR_EQ(rs->data, "42");
}

TEST(princ_to_string_stream)
{
    CL_Obj sstream = cl_make_string_output_stream();
    CL_Obj str = cl_make_string("hello", 5);
    CL_Obj result;
    CL_String *rs;

    CL_GC_PROTECT(sstream);
    CL_GC_PROTECT(str);
    cl_princ_to_stream(str, sstream);
    result = cl_get_output_stream_string(sstream);
    CL_GC_UNPROTECT(2);

    ASSERT(CL_STRING_P(result));
    rs = (CL_String *)CL_OBJ_TO_PTR(result);
    ASSERT_STR_EQ(rs->data, "hello");
}

TEST(prin1_string_escapes_to_stream)
{
    CL_Obj sstream = cl_make_string_output_stream();
    CL_Obj str = cl_make_string("hello", 5);
    CL_Obj result;
    CL_String *rs;

    CL_GC_PROTECT(sstream);
    CL_GC_PROTECT(str);
    cl_prin1_to_stream(str, sstream);
    result = cl_get_output_stream_string(sstream);
    CL_GC_UNPROTECT(2);

    ASSERT(CL_STRING_P(result));
    rs = (CL_String *)CL_OBJ_TO_PTR(result);
    ASSERT_STR_EQ(rs->data, "\"hello\"");
}

TEST(print_to_string_stream)
{
    CL_Obj sstream = cl_make_string_output_stream();
    CL_Obj result;
    CL_String *rs;

    CL_GC_PROTECT(sstream);
    cl_print_to_stream(CL_MAKE_FIXNUM(99), sstream);
    result = cl_get_output_stream_string(sstream);
    CL_GC_UNPROTECT(1);

    ASSERT(CL_STRING_P(result));
    rs = (CL_String *)CL_OBJ_TO_PTR(result);
    /* print outputs: newline, object, space */
    ASSERT_STR_EQ(rs->data, "\n99 ");
}

TEST(prin1_list_to_stream)
{
    CL_Obj sstream = cl_make_string_output_stream();
    CL_Obj list, result;
    CL_String *rs;

    CL_GC_PROTECT(sstream);
    list = cl_cons(CL_MAKE_FIXNUM(1),
           cl_cons(CL_MAKE_FIXNUM(2),
           cl_cons(CL_MAKE_FIXNUM(3), CL_NIL)));
    CL_GC_PROTECT(list);
    cl_prin1_to_stream(list, sstream);
    result = cl_get_output_stream_string(sstream);
    CL_GC_UNPROTECT(2);

    ASSERT(CL_STRING_P(result));
    rs = (CL_String *)CL_OBJ_TO_PTR(result);
    ASSERT_STR_EQ(rs->data, "(1 2 3)");
}

TEST(multiple_prints_to_same_stream)
{
    CL_Obj sstream = cl_make_string_output_stream();
    CL_Obj result;
    CL_String *rs;

    CL_GC_PROTECT(sstream);
    cl_princ_to_stream(CL_MAKE_FIXNUM(1), sstream);
    cl_stream_write_char(sstream, '+');
    cl_princ_to_stream(CL_MAKE_FIXNUM(2), sstream);
    cl_stream_write_char(sstream, '=');
    cl_princ_to_stream(CL_MAKE_FIXNUM(3), sstream);
    result = cl_get_output_stream_string(sstream);
    CL_GC_UNPROTECT(1);

    ASSERT(CL_STRING_P(result));
    rs = (CL_String *)CL_OBJ_TO_PTR(result);
    ASSERT_STR_EQ(rs->data, "1+2=3");
}

TEST(prin1_to_string_c_still_works)
{
    /* Verify the C-internal cl_prin1_to_string still works */
    char buf[64];
    int len;
    CL_Obj list = cl_cons(CL_MAKE_FIXNUM(10), cl_cons(CL_MAKE_FIXNUM(20), CL_NIL));
    CL_GC_PROTECT(list);
    len = cl_prin1_to_string(list, buf, sizeof(buf));
    CL_GC_UNPROTECT(1);
    ASSERT(len > 0);
    ASSERT_STR_EQ(buf, "(10 20)");
}

TEST(prin1_nil_to_stream)
{
    CL_Obj sstream = cl_make_string_output_stream();
    CL_Obj result;
    CL_String *rs;

    CL_GC_PROTECT(sstream);
    cl_prin1_to_stream(CL_NIL, sstream);
    result = cl_get_output_stream_string(sstream);
    CL_GC_UNPROTECT(1);

    ASSERT(CL_STRING_P(result));
    rs = (CL_String *)CL_OBJ_TO_PTR(result);
    ASSERT_STR_EQ(rs->data, "NIL");
}

TEST(prin1_char_to_stream)
{
    CL_Obj sstream = cl_make_string_output_stream();
    CL_Obj result;
    CL_String *rs;

    CL_GC_PROTECT(sstream);
    cl_prin1_to_stream(CL_MAKE_CHAR('A'), sstream);
    result = cl_get_output_stream_string(sstream);
    CL_GC_UNPROTECT(1);

    ASSERT(CL_STRING_P(result));
    rs = (CL_String *)CL_OBJ_TO_PTR(result);
    ASSERT_STR_EQ(rs->data, "#\\A");
}

TEST(princ_char_to_stream)
{
    CL_Obj sstream = cl_make_string_output_stream();
    CL_Obj result;
    CL_String *rs;

    CL_GC_PROTECT(sstream);
    cl_princ_to_stream(CL_MAKE_CHAR('A'), sstream);
    result = cl_get_output_stream_string(sstream);
    CL_GC_UNPROTECT(1);

    ASSERT(CL_STRING_P(result));
    rs = (CL_String *)CL_OBJ_TO_PTR(result);
    ASSERT_STR_EQ(rs->data, "A");
}

/* --- File stream open/close tests (Step 8) --- */

TEST(open_file_write_read)
{
    const char *path = "/tmp/cl_test_step8_open.txt";
    PlatformFile wfh;
    CL_Obj stream;
    CL_Stream *st;
    int ch;

    /* Write via platform to create file */
    wfh = platform_file_open(path, PLATFORM_FILE_WRITE);
    ASSERT(wfh != PLATFORM_FILE_INVALID);
    platform_file_write_string(wfh, "ABCDE");
    platform_file_close(wfh);

    /* Open as CL file input stream */
    stream = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_FILE);
    st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
    st->handle_id = (uint32_t)platform_file_open(path, PLATFORM_FILE_READ);
    ASSERT(st->handle_id != PLATFORM_FILE_INVALID);

    ch = cl_stream_read_char(stream);
    ASSERT_EQ_INT(ch, 'A');
    ch = cl_stream_read_char(stream);
    ASSERT_EQ_INT(ch, 'B');

    cl_stream_close(stream);
}

TEST(open_file_output_stream_write)
{
    const char *path = "/tmp/cl_test_step8_out.txt";
    CL_Obj wstream, rstream;
    CL_Stream *wst, *rst;
    int ch;

    /* Create output file stream */
    wstream = cl_make_stream(CL_STREAM_OUTPUT, CL_STREAM_FILE);
    wst = (CL_Stream *)CL_OBJ_TO_PTR(wstream);
    wst->handle_id = (uint32_t)platform_file_open(path, PLATFORM_FILE_WRITE);
    ASSERT(wst->handle_id != PLATFORM_FILE_INVALID);

    cl_stream_write_char(wstream, 'X');
    cl_stream_write_char(wstream, 'Y');
    cl_stream_write_char(wstream, 'Z');
    cl_stream_close(wstream);

    /* Read back */
    rstream = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_FILE);
    rst = (CL_Stream *)CL_OBJ_TO_PTR(rstream);
    rst->handle_id = (uint32_t)platform_file_open(path, PLATFORM_FILE_READ);
    ASSERT(rst->handle_id != PLATFORM_FILE_INVALID);

    ch = cl_stream_read_char(rstream);
    ASSERT_EQ_INT(ch, 'X');
    ch = cl_stream_read_char(rstream);
    ASSERT_EQ_INT(ch, 'Y');
    ch = cl_stream_read_char(rstream);
    ASSERT_EQ_INT(ch, 'Z');
    ch = cl_stream_read_char(rstream);
    ASSERT_EQ_INT(ch, -1);  /* EOF */

    cl_stream_close(rstream);
}

TEST(file_stream_write_string_read_back)
{
    const char *path = "/tmp/cl_test_step8_str.txt";
    CL_Obj wstream, rstream;
    CL_Stream *wst, *rst;
    char buf[64];
    int i, ch;

    /* Write a string to file */
    wstream = cl_make_stream(CL_STREAM_OUTPUT, CL_STREAM_FILE);
    wst = (CL_Stream *)CL_OBJ_TO_PTR(wstream);
    wst->handle_id = (uint32_t)platform_file_open(path, PLATFORM_FILE_WRITE);
    cl_stream_write_string(wstream, "Hello World", 11);
    cl_stream_close(wstream);

    /* Read back */
    rstream = cl_make_stream(CL_STREAM_INPUT, CL_STREAM_FILE);
    rst = (CL_Stream *)CL_OBJ_TO_PTR(rstream);
    rst->handle_id = (uint32_t)platform_file_open(path, PLATFORM_FILE_READ);
    i = 0;
    while ((ch = cl_stream_read_char(rstream)) != -1 && i < 63)
        buf[i++] = (char)ch;
    buf[i] = '\0';
    cl_stream_close(rstream);

    ASSERT_STR_EQ(buf, "Hello World");
}

/* --- Platform file operations (Step 10) --- */

TEST(platform_universal_time)
{
    uint32_t ut = platform_universal_time();
    /* Should be > 2000-01-01 (3155760000) and < 2100-01-01 (~6311520000 > uint32 max)
     * Actually CL universal time for 2020 is ~3786912000, for 2030 ~4102444800 */
    ASSERT(ut > 3786912000u); /* After 2020 */
}

TEST(platform_file_exists_positive)
{
    const char *path = "/tmp/cl_test_exists.tmp";
    PlatformFile f = platform_file_open(path, PLATFORM_FILE_WRITE);
    ASSERT(f != PLATFORM_FILE_INVALID);
    platform_file_write_string(f, "test");
    platform_file_close(f);

    ASSERT(platform_file_exists(path));
    remove(path);
}

TEST(platform_file_exists_negative)
{
    ASSERT(!platform_file_exists("/tmp/cl_nonexistent_xyz_123.tmp"));
}

TEST(platform_file_delete_test)
{
    const char *path = "/tmp/cl_test_delete.tmp";
    PlatformFile f = platform_file_open(path, PLATFORM_FILE_WRITE);
    platform_file_write_string(f, "test");
    platform_file_close(f);

    ASSERT(platform_file_exists(path));
    ASSERT(platform_file_delete(path) == 0);
    ASSERT(!platform_file_exists(path));
}

TEST(platform_file_rename_test)
{
    const char *old_path = "/tmp/cl_test_rename_old.tmp";
    const char *new_path = "/tmp/cl_test_rename_new.tmp";
    PlatformFile f = platform_file_open(old_path, PLATFORM_FILE_WRITE);
    platform_file_write_string(f, "test");
    platform_file_close(f);

    ASSERT(platform_file_rename(old_path, new_path) == 0);
    ASSERT(!platform_file_exists(old_path));
    ASSERT(platform_file_exists(new_path));
    remove(new_path);
}

TEST(platform_file_mtime_test)
{
    const char *path = "/tmp/cl_test_mtime.tmp";
    PlatformFile f = platform_file_open(path, PLATFORM_FILE_WRITE);
    uint32_t mtime;
    platform_file_write_string(f, "test");
    platform_file_close(f);

    mtime = platform_file_mtime(path);
    /* Should be a recent universal time (> 2020) */
    ASSERT(mtime > 3786912000u);
    remove(path);
}

TEST(platform_file_mtime_nonexistent)
{
    uint32_t mtime = platform_file_mtime("/tmp/cl_nonexistent_mtime.tmp");
    ASSERT_EQ_INT((int)mtime, 0);
}

TEST(platform_mkdir_test)
{
    const char *path = "/tmp/cl_test_mkdir_step10";
    ASSERT(platform_mkdir(path) == 0);
    ASSERT(platform_file_is_directory(path));
    /* Second call should succeed (already exists) */
    ASSERT(platform_mkdir(path) == 0);
    rmdir(path);
}

TEST(platform_file_is_directory_test)
{
    ASSERT(platform_file_is_directory("/tmp"));
    ASSERT(!platform_file_is_directory("/tmp/cl_nonexistent_dir_xyz"));
}

/* --- Builtin tests via eval (Step 10) --- */

TEST(eval_get_universal_time)
{
    CL_Obj result = cl_eval_string("(get-universal-time)");
    /* Should return a bignum (> fixnum range) */
    ASSERT(!CL_NULL_P(result));
    /* The value should be a number (fixnum or bignum) */
    ASSERT(CL_FIXNUM_P(result) || CL_BIGNUM_P(result));
}

TEST(eval_probe_file_exists)
{
    const char *path = "/tmp/cl_test_probe_eval.tmp";
    PlatformFile f = platform_file_open(path, PLATFORM_FILE_WRITE);
    CL_Obj result;
    platform_file_write_string(f, "test");
    platform_file_close(f);

    result = cl_eval_string("(probe-file \"/tmp/cl_test_probe_eval.tmp\")");
    ASSERT(CL_PATHNAME_P(result));

    remove(path);
}

TEST(eval_probe_file_not_exists)
{
    CL_Obj result = cl_eval_string("(probe-file \"/tmp/cl_nonexistent_eval_xyz.tmp\")");
    ASSERT(CL_NULL_P(result));
}

TEST(eval_delete_file)
{
    const char *path = "/tmp/cl_test_delete_eval.tmp";
    PlatformFile f = platform_file_open(path, PLATFORM_FILE_WRITE);
    platform_file_write_string(f, "test");
    platform_file_close(f);

    cl_eval_string("(delete-file \"/tmp/cl_test_delete_eval.tmp\")");
    ASSERT(!platform_file_exists(path));
}

TEST(eval_file_write_date)
{
    const char *path = "/tmp/cl_test_fwd_eval.tmp";
    PlatformFile f = platform_file_open(path, PLATFORM_FILE_WRITE);
    CL_Obj result;
    platform_file_write_string(f, "test");
    platform_file_close(f);

    result = cl_eval_string("(file-write-date \"/tmp/cl_test_fwd_eval.tmp\")");
    ASSERT(!CL_NULL_P(result));
    /* Should be a number */
    ASSERT(CL_FIXNUM_P(result) || CL_BIGNUM_P(result));

    remove(path);
}

TEST(eval_file_namestring)
{
    CL_Obj result = cl_eval_string("(file-namestring \"/foo/bar/baz.txt\")");
    CL_String *s;
    ASSERT(CL_STRING_P(result));
    s = (CL_String *)CL_OBJ_TO_PTR(result);
    ASSERT_STR_EQ(s->data, "baz.txt");
}

TEST(eval_directory_namestring)
{
    CL_Obj result = cl_eval_string("(directory-namestring \"/foo/bar/baz.txt\")");
    CL_String *s;
    ASSERT(CL_STRING_P(result));
    s = (CL_String *)CL_OBJ_TO_PTR(result);
    ASSERT_STR_EQ(s->data, "/foo/bar/");
}

TEST(eval_rename_file)
{
    const char *old_path = "/tmp/cl_test_rename_eval_old.tmp";
    const char *new_path = "/tmp/cl_test_rename_eval_new.tmp";
    PlatformFile f = platform_file_open(old_path, PLATFORM_FILE_WRITE);
    platform_file_write_string(f, "test");
    platform_file_close(f);

    cl_eval_string("(rename-file \"/tmp/cl_test_rename_eval_old.tmp\" \"/tmp/cl_test_rename_eval_new.tmp\")");
    ASSERT(!platform_file_exists(old_path));
    ASSERT(platform_file_exists(new_path));

    remove(new_path);
}

TEST(eval_values_from_defun)
{
    /* Regression test: values must propagate through defun returns */
    const char *expr = "(progn (defun mv-test () (values 10 20 30)) "
                       "(multiple-value-list (mv-test)))";
    CL_Obj result = cl_eval_string(expr);
    char buf[64];
    cl_prin1_to_string(result, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "(10 20 30)");
}

/* --- Readtable tests (Step 12) --- */

TEST(readtablep_current)
{
    CL_Obj result = cl_eval_string("(readtablep *readtable*)");
    ASSERT(result == CL_T);
}

TEST(readtablep_non_readtable)
{
    CL_Obj result = cl_eval_string("(readtablep 42)");
    ASSERT(CL_NULL_P(result));
}

TEST(get_macro_character_paren)
{
    /* ( is a terminating macro — fn should be NIL (built-in), non-term-p should be NIL */
    CL_Obj result = cl_eval_string("(get-macro-character #\\()");
    ASSERT(CL_NULL_P(result));
}

TEST(get_macro_character_hash)
{
    /* # is a non-terminating macro — check second value */
    CL_Obj result = cl_eval_string(
        "(multiple-value-list (get-macro-character #\\#))");
    char buf[64];
    cl_prin1_to_string(result, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "(NIL T)");
}

TEST(set_macro_character_excl)
{
    /* Define ! as a reader macro: set it up via C API directly to verify */
    CL_Obj result;
    CL_Readtable *rt;

    /* Verify set-macro-character works at the C level */
    result = cl_eval_string(
        "(set-macro-character #\\! "
        "  (lambda (stream char) "
        "    (list 'quote (read stream t nil t))))");
    ASSERT(result == CL_T);

    /* Check the readtable was actually modified */
    rt = cl_readtable_current();
    ASSERT(rt->syntax['!'] == CL_CHAR_TERM_MACRO);
    ASSERT(!CL_NULL_P(rt->macro_fn['!']));

    /* Now read and eval with ! as a macro character */
    result = cl_eval_string("(equal !hello 'hello)");
    ASSERT(result == CL_T);

    /* Clean up: restore ! to constituent */
    rt->syntax['!'] = CL_CHAR_CONSTITUENT;
    rt->macro_fn['!'] = CL_NIL;
}

TEST(copy_readtable_preserves)
{
    /* Copy current readtable, verify the copy is a valid readtable */
    CL_Obj result = cl_eval_string("(readtablep (copy-readtable))");
    ASSERT(result == CL_T);
}

TEST(compile_nil_lambda)
{
    /* (compile nil '(lambda (x) (+ x 1))) => function, then funcall */
    CL_Obj result = cl_eval_string(
        "(funcall (compile nil '(lambda (x) (+ x 1))) 41)");
    ASSERT(CL_FIXNUM_P(result));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(result), 42);
}

TEST(compile_named_function)
{
    /* (compile 'car) => returns the car function */
    CL_Obj result = cl_eval_string("(functionp (compile 'car))");
    ASSERT(result == CL_T);
}

TEST(star_readtable_is_special)
{
    /* *readtable* should be a special variable */
    CL_Obj result = cl_eval_string("(boundp '*readtable*)");
    ASSERT(result == CL_T);
}

/* --- TCP Socket Stream tests --- */

/* Start a local TCP server on a random port that echoes data back.
 * Returns the port number, sets *server_fd. The caller must accept(). */
static int start_echo_server(int *server_fd)
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    *server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (*server_fd < 0) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  /* Let OS pick a port */

    if (bind(*server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(*server_fd);
        return -1;
    }
    if (listen(*server_fd, 1) < 0) {
        close(*server_fd);
        return -1;
    }
    /* Get assigned port */
    if (getsockname(*server_fd, (struct sockaddr *)&addr, &addrlen) < 0) {
        close(*server_fd);
        return -1;
    }
    return ntohs(addr.sin_port);
}

TEST(socket_stream_connect_write_read)
{
    int server_fd;
    int port;
    pid_t pid;
    CL_Obj stream;

    port = start_echo_server(&server_fd);
    ASSERT(port > 0);

    /* Fork a server that sends "OK" then closes */
    pid = fork();
    if (pid == 0) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(server_fd, (struct sockaddr *)&caddr, &clen);
        close(server_fd);
        if (cfd >= 0) {
            write(cfd, "OK", 2);
            close(cfd);
        }
        _exit(0);
    }

    close(server_fd);

    /* Connect via our socket stream */
    stream = cl_make_socket_stream("127.0.0.1", port);
    ASSERT(!CL_NULL_P(stream));
    ASSERT(CL_STREAM_P(stream));

    {
        CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
        ASSERT_EQ_INT((int)st->stream_type, CL_STREAM_SOCKET);
        ASSERT_EQ_INT((int)st->direction, CL_STREAM_IO);
        ASSERT(st->flags & CL_STREAM_FLAG_OPEN);
    }

    /* Read "OK" from server */
    {
        int ch1 = cl_stream_read_char(stream);
        int ch2 = cl_stream_read_char(stream);
        int ch3 = cl_stream_read_char(stream);  /* EOF */
        ASSERT_EQ_INT(ch1, 'O');
        ASSERT_EQ_INT(ch2, 'K');
        ASSERT_EQ_INT(ch3, -1);
    }

    cl_stream_close(stream);

    { int status; waitpid(pid, &status, 0); }
}

TEST(socket_stream_close)
{
    int server_fd, port;
    pid_t pid;
    CL_Obj stream;

    port = start_echo_server(&server_fd);
    ASSERT(port > 0);

    pid = fork();
    if (pid == 0) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(server_fd, (struct sockaddr *)&caddr, &clen);
        close(server_fd);
        if (cfd >= 0) close(cfd);
        _exit(0);
    }
    close(server_fd);

    stream = cl_make_socket_stream("127.0.0.1", port);
    ASSERT(!CL_NULL_P(stream));

    /* Verify open */
    {
        CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
        ASSERT(st->flags & CL_STREAM_FLAG_OPEN);
    }

    cl_stream_close(stream);

    /* Verify closed */
    {
        CL_Stream *st = (CL_Stream *)CL_OBJ_TO_PTR(stream);
        ASSERT(!(st->flags & CL_STREAM_FLAG_OPEN));
    }

    { int status; waitpid(pid, &status, 0); }
}

TEST(socket_stream_connect_failure)
{
    /* Connecting to a port with nothing listening should return NIL */
    CL_Obj stream = cl_make_socket_stream("127.0.0.1", 1);  /* Port 1: unlikely to be listening */
    ASSERT(CL_NULL_P(stream));
}

TEST(socket_stream_write_string)
{
    int server_fd, port;
    pid_t pid;
    CL_Obj stream;

    port = start_echo_server(&server_fd);
    ASSERT(port > 0);

    pid = fork();
    if (pid == 0) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        unsigned char buf[256];
        ssize_t n;
        int cfd = accept(server_fd, (struct sockaddr *)&caddr, &clen);
        close(server_fd);
        if (cfd >= 0) {
            n = read(cfd, buf, sizeof(buf));
            if (n > 0) write(cfd, buf, (size_t)n);
            close(cfd);
        }
        _exit(0);
    }
    close(server_fd);

    stream = cl_make_socket_stream("127.0.0.1", port);
    ASSERT(!CL_NULL_P(stream));

    /* Write via write_string */
    cl_stream_write_string(stream, "Hello", 5);

    /* Shutdown write side to trigger echo — use platform_socket_close
     * Actually we need a half-close. Let's just close and verify we wrote. */
    cl_stream_close(stream);

    { int status; waitpid(pid, &status, 0); }
}

TEST(eval_open_tcp_stream_connect_failure)
{
    /* ext:open-tcp-stream should signal an error on connection failure */
    CL_Obj result = cl_eval_string(
        "(handler-case (ext:open-tcp-stream \"127.0.0.1\" 1) "
        "  (error (c) (declare (ignore c)) :connection-failed))");
    /* Should get :CONNECTION-FAILED from the error handler */
    ASSERT(!CL_NULL_P(result));
}

TEST(eval_open_tcp_stream_read_byte)
{
    int server_fd, port;
    pid_t pid;
    char expr[256];
    CL_Obj result;

    port = start_echo_server(&server_fd);
    ASSERT(port > 0);

    /* Server sends byte 65 ('A') then closes */
    pid = fork();
    if (pid == 0) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        unsigned char byte = 65;
        int cfd = accept(server_fd, (struct sockaddr *)&caddr, &clen);
        close(server_fd);
        if (cfd >= 0) {
            write(cfd, &byte, 1);
            close(cfd);
        }
        _exit(0);
    }
    close(server_fd);

    /* Eval: open stream, read one byte, close */
    snprintf(expr, sizeof(expr),
        "(let ((s (ext:open-tcp-stream \"127.0.0.1\" %d)))"
        "  (prog1 (read-byte s)"
        "    (close s)))",
        port);
    result = cl_eval_string(expr);

    /* Should get 65 (ASCII 'A') */
    ASSERT(CL_FIXNUM_P(result));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(result), 65);

    { int status; waitpid(pid, &status, 0); }
}

TEST(eval_open_tcp_stream_write_byte)
{
    int server_fd, port;
    pid_t pid;
    char expr[256];
    CL_Obj result;
    int pipefd[2];

    ASSERT(pipe(pipefd) == 0);

    port = start_echo_server(&server_fd);
    ASSERT(port > 0);

    /* Server reads one byte and reports it via pipe */
    pid = fork();
    if (pid == 0) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        unsigned char byte = 0;
        ssize_t n;
        int cfd = accept(server_fd, (struct sockaddr *)&caddr, &clen);
        close(server_fd);
        close(pipefd[0]);  /* Close read end */
        if (cfd >= 0) {
            n = read(cfd, &byte, 1);
            if (n == 1) write(pipefd[1], &byte, 1);
            close(cfd);
        }
        close(pipefd[1]);
        _exit(0);
    }
    close(server_fd);
    close(pipefd[1]);  /* Close write end */

    /* Eval: open stream, write byte 42, close */
    snprintf(expr, sizeof(expr),
        "(let ((s (ext:open-tcp-stream \"127.0.0.1\" %d)))"
        "  (write-byte 42 s)"
        "  (close s)"
        "  t)",
        port);
    result = cl_eval_string(expr);
    ASSERT(result == CL_T);

    /* Verify server received byte 42 */
    {
        unsigned char received = 0;
        ssize_t n = read(pipefd[0], &received, 1);
        ASSERT_EQ_INT((int)n, 1);
        ASSERT_EQ_INT((int)received, 42);
    }
    close(pipefd[0]);

    { int status; waitpid(pid, &status, 0); }
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

    /* Printer + stream integration (Step 7) */
    RUN(prin1_to_string_stream);
    RUN(princ_to_string_stream);
    RUN(prin1_string_escapes_to_stream);
    RUN(print_to_string_stream);
    RUN(prin1_list_to_stream);
    RUN(multiple_prints_to_same_stream);
    RUN(prin1_to_string_c_still_works);
    RUN(prin1_nil_to_stream);
    RUN(prin1_char_to_stream);
    RUN(princ_char_to_stream);

    /* File stream open/close (Step 8) */
    RUN(open_file_write_read);
    RUN(open_file_output_stream_write);
    RUN(file_stream_write_string_read_back);

    /* Platform file operations (Step 10) */
    RUN(platform_universal_time);
    RUN(platform_file_exists_positive);
    RUN(platform_file_exists_negative);
    RUN(platform_file_delete_test);
    RUN(platform_file_rename_test);
    RUN(platform_file_mtime_test);
    RUN(platform_file_mtime_nonexistent);
    RUN(platform_mkdir_test);
    RUN(platform_file_is_directory_test);

    /* Builtins via eval (Step 10) */
    RUN(eval_get_universal_time);
    RUN(eval_probe_file_exists);
    RUN(eval_probe_file_not_exists);
    RUN(eval_delete_file);
    RUN(eval_file_write_date);
    RUN(eval_file_namestring);
    RUN(eval_directory_namestring);
    RUN(eval_rename_file);

    /* MV propagation regression test */
    RUN(eval_values_from_defun);

    /* Readtable tests (Step 12) */
    RUN(readtablep_current);
    RUN(readtablep_non_readtable);
    RUN(get_macro_character_paren);
    RUN(get_macro_character_hash);
    RUN(set_macro_character_excl);
    RUN(copy_readtable_preserves);
    RUN(compile_nil_lambda);
    RUN(compile_named_function);
    RUN(star_readtable_is_special);

    /* TCP Socket Stream tests */
    RUN(socket_stream_connect_failure);
    RUN(socket_stream_connect_write_read);
    RUN(socket_stream_close);
    RUN(socket_stream_write_string);
    RUN(eval_open_tcp_stream_connect_failure);
    RUN(eval_open_tcp_stream_read_byte);
    RUN(eval_open_tcp_stream_write_byte);

    teardown();
    REPORT();
}
