/*
 * test_stream.c — Tests for stream heap type and platform file I/O
 */

#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/stream.h"
#include "platform/platform.h"
#include <string.h>

static void setup(void)
{
    platform_init();
    cl_error_init();
    cl_mem_init(CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
    cl_stream_init();
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

    teardown();
    REPORT();
}
