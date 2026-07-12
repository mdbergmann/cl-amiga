/* Tests for the TTY primitives behind EXT:TTY-RAW-MODE / EXT:TTY-SIZE and
 * the console-stream LISTEN / READ-CHAR-NO-HANG availability probe.
 *
 * Motivated by cl-tuition (TEA-style TUI library): without raw mode the
 * cooked tty echoes every keypress (arrow keys as literal ^[[A text) onto
 * the drawn frame, input only arrives after Enter, and READ-CHAR-NO-HANG
 * on the console never reported availability at all — so a TUI's input
 * loop starved forever.
 *
 * The tests run against a real pseudo-terminal: the pty slave is dup2'd
 * over stdin, which is exactly how the primitives see a user's terminal.
 * (The test runner starts us with stdin at /dev/null, so the pty is also
 * what makes raw mode *possible* here.)  POSIX host only — the Amiga leg
 * lives in tests/amiga/run-tests.lisp.
 */

#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/reader.h"
#include "core/printer.h"
#include "core/compiler.h"
#include "core/vm.h"
#include "core/builtins.h"
#include "core/repl.h"
#include "core/stream.h"
#include "core/thread.h"
#include "platform/platform.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int pty_master = -1;
static int pty_slave = -1;
static int saved_stdin = -1;

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
    cl_stream_init();   /* binds *STANDARD-INPUT* &co (the LISTEN tests) */
    cl_builtins_init();
    cl_repl_init();
}

static void teardown(void)
{
    cl_mem_shutdown();
    platform_shutdown();
}

/* Route stdin through a fresh pty slave.  Returns 0 on success. */
static int pty_stdin_setup(void)
{
    struct winsize ws;

    pty_master = posix_openpt(O_RDWR | O_NOCTTY);
    if (pty_master < 0) return -1;
    if (grantpt(pty_master) != 0 || unlockpt(pty_master) != 0) return -1;
    pty_slave = open(ptsname(pty_master), O_RDWR | O_NOCTTY);
    if (pty_slave < 0) return -1;

    /* Give the pty a definite size so the size query has ground truth. */
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = 132;
    ws.ws_row = 43;
    if (ioctl(pty_slave, TIOCSWINSZ, &ws) != 0) return -1;

    saved_stdin = dup(STDIN_FILENO);
    if (saved_stdin < 0) return -1;
    if (dup2(pty_slave, STDIN_FILENO) < 0) return -1;
    return 0;
}

static void pty_stdin_restore(void)
{
    if (saved_stdin >= 0) {
        dup2(saved_stdin, STDIN_FILENO);
        close(saved_stdin);
        saved_stdin = -1;
    }
    if (pty_slave >= 0) { close(pty_slave); pty_slave = -1; }
    if (pty_master >= 0) { close(pty_master); pty_master = -1; }
}

/* Poll the availability probe for up to ~1s — pty delivery is fast but
 * not synchronous with the master-side write(). */
static int wait_char_avail(void)
{
    int i;
    for (i = 0; i < 100; i++) {
        if (platform_tty_char_avail())
            return 1;
        usleep(10000);
    }
    return 0;
}

/* Eval a string and return the prin1 representation of the result
 * (same helper as test_format.c). */
static const char *eval_print(const char *str)
{
    static char buf[1024];
    int err;

    CL_CATCH(err);
    if (err == CL_ERR_NONE) {
        CL_Obj result = cl_eval_string(str);
        cl_prin1_to_string(result, buf, sizeof(buf));
        CL_UNCATCH();
        return buf;
    } else {
        CL_UNCATCH();
        cl_vm.sp = 0;
        cl_vm.fp = 0;
        snprintf(buf, sizeof(buf), "ERROR:%d", err);
        return buf;
    }
}

TEST(raw_mode_toggles_termios)
{
    struct termios t;

    ASSERT_EQ_INT(platform_tty_raw_active(), 0);
    ASSERT_EQ_INT(platform_tty_raw(1), 0);
    ASSERT_EQ_INT(platform_tty_raw_active(), 1);

    /* Raw really means raw: no canonical mode, no echo, no signals. */
    ASSERT_EQ_INT(tcgetattr(STDIN_FILENO, &t), 0);
    ASSERT((t.c_lflag & (tcflag_t)ICANON) == 0);
    ASSERT((t.c_lflag & (tcflag_t)ECHO) == 0);
    ASSERT((t.c_lflag & (tcflag_t)ISIG) == 0);

    /* Enabling twice is idempotent. */
    ASSERT_EQ_INT(platform_tty_raw(1), 0);

    ASSERT_EQ_INT(platform_tty_raw(0), 0);
    ASSERT_EQ_INT(platform_tty_raw_active(), 0);

    /* Disable restored the saved cooked state. */
    ASSERT_EQ_INT(tcgetattr(STDIN_FILENO, &t), 0);
    ASSERT((t.c_lflag & (tcflag_t)ICANON) != 0);
    ASSERT((t.c_lflag & (tcflag_t)ECHO) != 0);

    /* Disabling twice is idempotent too. */
    ASSERT_EQ_INT(platform_tty_raw(0), 0);
}

TEST(raw_read_avail_and_pushback)
{
    ASSERT_EQ_INT(platform_tty_raw(1), 0);

    /* Nothing typed yet. */
    ASSERT_EQ_INT(platform_tty_char_avail(), 0);

    /* One keypress arrives without any Enter. */
    ASSERT_EQ_INT((int)write(pty_master, "x", 1), 1);
    ASSERT_EQ_INT(wait_char_avail(), 1);
    ASSERT_EQ_INT(platform_getchar(), 'x');
    ASSERT_EQ_INT(platform_tty_char_avail(), 0);

    /* Pushback is visible to the probe and comes back first. */
    platform_ungetchar('y');
    ASSERT_EQ_INT(platform_tty_char_avail(), 1);
    ASSERT_EQ_INT(platform_getchar(), 'y');
    ASSERT_EQ_INT(platform_tty_char_avail(), 0);

    ASSERT_EQ_INT(platform_tty_raw(0), 0);
}

TEST(tty_size_reports_winsize)
{
    int cols = 0, rows = 0;
    ASSERT_EQ_INT(platform_tty_size(&cols, &rows), 0);
    ASSERT_EQ_INT(cols, 132);
    ASSERT_EQ_INT(rows, 43);
}

/* The Lisp surface: EXT builtins plus the LISTEN / READ-CHAR-NO-HANG
 * console wiring a TUI input loop depends on. */
TEST(lisp_tty_builtins_and_input_loop)
{
    ASSERT_STR_EQ(eval_print("(ext:tty-p)"), "T");
    ASSERT_STR_EQ(eval_print("(ext:tty-size)"), "(132 . 43)");

    ASSERT_STR_EQ(eval_print("(ext:tty-raw-mode t)"), "T");

    /* No input pending: the input loop's poll must not block, and per
     * CLHS it returns NIL (":no char yet" — the :none eof-value is only
     * for end of file). */
    ASSERT_STR_EQ(eval_print("(listen)"), "NIL");
    ASSERT_STR_EQ(eval_print("(read-char-no-hang *standard-input* nil :none)"),
                  "NIL");

    /* A keypress (no Enter) becomes visible to LISTEN and readable. */
    ASSERT_EQ_INT((int)write(pty_master, "q", 1), 1);
    ASSERT_EQ_INT(wait_char_avail(), 1);
    ASSERT_STR_EQ(eval_print("(listen)"), "T");
    ASSERT_STR_EQ(eval_print("(read-char-no-hang *standard-input* nil :none)"),
                  "#\\q");
    ASSERT_STR_EQ(eval_print("(read-char-no-hang *standard-input* nil :none)"),
                  "NIL");

    /* *TERMINAL-IO* is bidirectional (CLHS) — it was historically bound to
     * the output-only stdout singleton, so (read-char t) saw instant EOF
     * and a TUI's input loop reading from *terminal-io* starved. */
    ASSERT_STR_EQ(eval_print("(input-stream-p *terminal-io*)"), "T");
    ASSERT_STR_EQ(eval_print("(output-stream-p *terminal-io*)"), "T");
    ASSERT_EQ_INT((int)write(pty_master, "w", 1), 1);
    ASSERT_EQ_INT(wait_char_avail(), 1);
    ASSERT_STR_EQ(eval_print("(listen *terminal-io*)"), "T");
    ASSERT_STR_EQ(eval_print("(read-char t)"), "#\\w");

    /* UNREAD-CHAR keeps LISTEN truthful (stream-level pushback). */
    ASSERT_EQ_INT((int)write(pty_master, "z", 1), 1);
    ASSERT_EQ_INT(wait_char_avail(), 1);
    ASSERT_STR_EQ(eval_print("(unread-char (read-char))"), "NIL");
    ASSERT_STR_EQ(eval_print("(listen)"), "T");
    ASSERT_STR_EQ(eval_print("(read-char)"), "#\\z");

    /* UNREAD-CHAR pushback on *STANDARD-INPUT* must be visible through
     * *TERMINAL-IO* — they now share the same backing console-input stream
     * (cl_stream_init makes *TERMINAL-IO* a two-way-stream over
     * *STANDARD-INPUT* and *STANDARD-OUTPUT* rather than an independent console
     * stream object). Before that fix, the pushback lived in a private
     * unread_char slot invisible across the two stream objects: LISTEN on
     * *terminal-io* reported NIL despite a pending char, and READ-CHAR on
     * *terminal-io* blocked waiting for genuinely new input. */
    ASSERT_EQ_INT((int)write(pty_master, "v", 1), 1);
    ASSERT_EQ_INT(wait_char_avail(), 1);
    ASSERT_STR_EQ(eval_print(
        "(unread-char (read-char *standard-input*) *standard-input*)"),
        "NIL");
    ASSERT_STR_EQ(eval_print("(listen *terminal-io*)"), "T");
    ASSERT_STR_EQ(eval_print("(read-char *terminal-io*)"), "#\\v");

    ASSERT_STR_EQ(eval_print("(ext:tty-raw-mode nil)"), "T");
}

TEST(non_tty_fails_gracefully)
{
    int devnull = open("/dev/null", O_RDONLY);
    int hold = dup(STDIN_FILENO);
    ASSERT(devnull >= 0 && hold >= 0);
    dup2(devnull, STDIN_FILENO);

    ASSERT_EQ_INT(platform_tty_raw(1), -1);
    ASSERT_EQ_INT(platform_tty_raw_active(), 0);
    ASSERT_STR_EQ(eval_print("(ext:tty-p)"), "NIL");
    ASSERT_STR_EQ(eval_print("(ext:tty-raw-mode t)"), "NIL");

    dup2(hold, STDIN_FILENO);
    close(hold);
    close(devnull);
}

int main(void)
{
    setup();

    if (pty_stdin_setup() != 0) {
        /* No pty available (exotic CI sandbox): report loudly but don't
         * fail the suite on infrastructure, not code. */
        printf("SKIP test_tty: could not allocate a pseudo-terminal\n");
        pty_stdin_restore();
        teardown();
        return 0;
    }

    RUN(raw_mode_toggles_termios);
    RUN(raw_read_avail_and_pushback);
    RUN(tty_size_reports_winsize);
    RUN(lisp_tty_builtins_and_input_loop);
    RUN(non_tty_fails_gracefully);

    pty_stdin_restore();
    teardown();
    REPORT();
}
