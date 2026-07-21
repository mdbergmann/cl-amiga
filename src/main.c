#include "platform/platform.h"
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
#include "core/stream.h"
#include "core/debugger.h"
#include "core/repl.h"
#include "core/color.h"
#include "jit/jit.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#ifdef PLATFORM_POSIX
#include <locale.h>
#include <pthread.h>
#include <execinfo.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#endif

#ifdef PLATFORM_POSIX
/* Crash handler on alternate stack for stack overflow debugging */
/* Use fixed size — SIGSTKSZ is not a compile-time constant on glibc 2.34+ */
#define CRASH_ALT_STACK_SIZE 16384
static char crash_alt_stack[CRASH_ALT_STACK_SIZE];

/* Defined in vm.c — dump last N VM opcodes for crash diagnostics */
extern void vm_trace_dump(void);
/* dbg_last_op/ip/fp/code are now macros from thread.h (CL_Thread fields) */

static void crash_handler(int sig, siginfo_t *info, void *ctx)
{
    char buf[512];
    int len;
    (void)ctx;
    /* Canary: first thing in handler, before any pointer dereference */
    {
        const char canary[] = "\n[CRASH] handler entered, sig=";
        char sigbuf[8];
        (void)write(2, canary, sizeof(canary) - 1);
        sigbuf[0] = '0' + (sig / 10);
        sigbuf[1] = '0' + (sig % 10);
        sigbuf[2] = '\n';
        (void)write(2, sigbuf, 3);
    }
    len = snprintf(buf, sizeof(buf),
                   "\n[FATAL] Signal %d at addr=%p, vm.fp=%d/%d, vm.sp=%d/%u\n",
                   sig, info ? info->si_addr : NULL,
                   cl_vm.fp, cl_vm.frame_size, cl_vm.sp, cl_vm.stack_size);
    (void)write(2, buf, len);
    {
        unsigned long long tid = 0;
#if defined(__APPLE__)
        pthread_threadid_np(NULL, &tid);
#elif defined(__linux__)
        tid = (unsigned long long)syscall(SYS_gettid);
#endif
        len = snprintf(buf, sizeof(buf),
                       "[FATAL] thread tid=%llu CT=%p\n",
                       tid, (void *)cl_current_thread);
        (void)write(2, buf, len);
    }
    len = snprintf(buf, sizeof(buf),
                   "[FATAL] arena=%p arena_size=0x%08x bump=0x%08x\n",
                   (void *)cl_heap.arena, (unsigned)cl_heap.arena_size, (unsigned)cl_heap.bump);
    (void)write(2, buf, len);
    len = snprintf(buf, sizeof(buf),
                   "[FATAL] last_op=0x%02x last_ip=%u last_fp=%d last_code=%p\n",
                   (unsigned)dbg_last_op, (unsigned)dbg_last_ip,
                   dbg_last_fp, (void *)dbg_last_code);
    (void)write(2, buf, len);
    /* Print current frame info */
    if (cl_vm.fp > 0) {
        CL_Frame *f = &cl_vm.frames[cl_vm.fp - 1];
        CL_Bytecode *bc = NULL;
        if (CL_CLOSURE_P(f->bytecode)) {
            CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(f->bytecode);
            bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
        } else if (CL_BYTECODE_P(f->bytecode)) {
            bc = (CL_Bytecode *)CL_OBJ_TO_PTR(f->bytecode);
        }
        if (bc) {
            len = snprintf(buf, sizeof(buf),
                           "[FATAL] frame: ip=%u/%u code=%p name=%s src=%s:%u\n",
                           f->ip, bc->code_len, (void *)bc->code,
                           (bc->name != CL_NIL && CL_SYMBOL_P(bc->name))
                               ? cl_symbol_name(bc->name) : "<anon>",
                           bc->source_file ? bc->source_file : "?",
                           bc->source_line);
            (void)write(2, buf, len);
        }
        /* Print last builtin called */
        {
            extern volatile const char *last_builtin_name;
            extern volatile void *last_builtin_fptr;
            extern volatile CL_Obj last_builtin_obj;
            len = snprintf(buf, sizeof(buf),
                           "[FATAL] last_builtin: %s fptr=%p obj=0x%08x\n",
                           last_builtin_name ? last_builtin_name : "(null)",
                           last_builtin_fptr,
                           (unsigned)last_builtin_obj);
            (void)write(2, buf, len);
        }
    }
    /* Dump all VM frames for backtrace */
    {
        int fi;
        for (fi = cl_vm.fp - 1; fi >= 0; fi--) {
            CL_Frame *ff = &cl_vm.frames[fi];
            CL_Bytecode *fbc = NULL;
            if (CL_CLOSURE_P(ff->bytecode)) {
                CL_Closure *cc = (CL_Closure *)CL_OBJ_TO_PTR(ff->bytecode);
                fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(cc->bytecode);
            } else if (CL_BYTECODE_P(ff->bytecode)) {
                fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(ff->bytecode);
            }
            if (fbc) {
                len = snprintf(buf, sizeof(buf),
                    "[BT] frame[%d] ip=%u/%u name=%s src=%s:%u\n",
                    fi, ff->ip, fbc->code_len,
                    (fbc->name != CL_NIL && CL_SYMBOL_P(fbc->name))
                        ? cl_symbol_name(fbc->name) : "<anon>",
                    fbc->source_file ? fbc->source_file : "?",
                    fbc->source_line);
            } else {
                len = snprintf(buf, sizeof(buf),
                    "[BT] frame[%d] ip=%u bytecode=0x%08x\n",
                    fi, ff->ip, ff->bytecode);
            }
            (void)write(2, buf, len);
        }
    }
    /* Dump VM stack around crash point */
    {
        int si;
        int start = cl_vm.sp - 8;
        int end_s = cl_vm.sp + 2;
        if (start < 0) start = 0;
        if (end_s > (int)cl_vm.stack_size) end_s = (int)cl_vm.stack_size;
        for (si = start; si < end_s; si++) {
            CL_Obj v = cl_vm.stack[si];
            int is_heap = CL_HEAP_P(v);
            int in_bounds = is_heap && (v < cl_heap.arena_size);
            len = snprintf(buf, sizeof(buf),
                "[STACK] [%d] = 0x%08x (heap=%d inbounds=%d type=%d)\n",
                si, (unsigned)v, is_heap, in_bounds,
                in_bounds ? (int)CL_HDR_TYPE(CL_OBJ_TO_PTR(v)) : -1);
            (void)write(2, buf, len);
        }
    }
    vm_trace_dump();
#ifdef PLATFORM_POSIX
    /* Native C backtrace */
    {
        void *frames[40];
        int nframes = backtrace(frames, 40);
        const char hdr[] = "=== Native C backtrace ===\n";
        (void)write(2, hdr, sizeof(hdr) - 1);
        backtrace_symbols_fd(frames, nframes, 2);
    }
#endif
    _exit(128 + sig);
}

static void install_crash_handler(void)
{
    stack_t ss;
    struct sigaction sa;
    ss.ss_sp = crash_alt_stack;
    ss.ss_size = CRASH_ALT_STACK_SIZE;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_ONSTACK | SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    /* Ignore SIGPIPE: writing to a socket whose peer has closed the
     * connection must fail the write with EPIPE, not kill the process.
     * Network clients (e.g. drakma over usocket) routinely hit this when a
     * server closes a keep-alive connection mid-exchange. */
    signal(SIGPIPE, SIG_IGN);
}
#endif

static void print_usage(void)
{
    platform_write_string(
        "Usage: clamiga [options]\n"
        "Options:\n"
        "  --heap <size>    Heap arena size (default: 4M)\n"
        "  --vm-stack <size>   VM value stack size (default: 64K)\n"
        "  --frames <n>     Max call frame depth (default: 256)\n"
        "  --batch          Batch mode (no prompts, read from stdin)\n"
        "  --load <file>    Load Lisp file before REPL (multiple allowed)\n"
        "  --eval <expr>    Evaluate expression before REPL (multiple allowed)\n"
        "  --script <file>  Load file and exit (no REPL)\n"
        "  --non-interactive Process options and exit (no REPL)\n"
        "  --no-userinit    Skip user init file (~/.clamigarc)\n"
        "  --color          Force color output\n"
        "  --no-color       Disable color output\n"
        "  --no-jit         Disable the m68k JIT (functions stay bytecode-only)\n"
        "  --boot-log       Print boot phase timings (\"; [boot] ...\")\n"
        "  --help           Show this help message\n"
        "\n"
        "Sizes accept K, M, G suffixes (e.g. 8M, 512K, 1G).\n"
    );
}

/* Parse a size string with optional K/M/G suffix. Returns 0 on error. */
static uint32_t parse_size(const char *str)
{
    uint32_t val = 0;
    const char *p = str;

    if (!str || !*str) return 0;

    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }

    if (p == str) return 0; /* no digits */

    switch (*p) {
    case 'K': case 'k': val *= 1024; p++; break;
    case 'M': case 'm': val *= 1024 * 1024; p++; break;
    case 'G': case 'g': val *= 1024 * 1024 * 1024; p++; break;
    case '\0': break;
    default: return 0; /* bad suffix */
    }

    if (*p != '\0') return 0; /* trailing garbage */

    return val;
}

/* Max number of --load/--eval actions */
#define MAX_ACTIONS 32

typedef struct {
    int is_eval; /* 0 = load file, 1 = eval string */
    const char *arg;
} CLAction;

/* Evaluate --eval in CL-USER context (not whatever *package* was left by --load) */
static CL_Obj eval_string_in_cl_user(const char *str)
{
    CL_Obj saved_pkg = cl_current_package;
    CL_Obj result;
    cl_current_package = cl_package_cl_user;
    result = cl_eval_string(str);
    cl_current_package = saved_pkg;
    return result;
}

int main(int argc, char *argv[])
{
    int batch = 0;
    int non_interactive = 0;
    int color_set = 0;
    int no_userinit = 0;
    int script = 0;
    int no_jit = 0;
    int boot_log = 0;
    const char *script_file = NULL;
    CLAction actions[MAX_ACTIONS];
    int action_count = 0;
    int i;
    uint32_t heap_size = 0;
    uint32_t stack_entries = 0;
    int frame_count = 0;

#ifdef PLATFORM_POSIX
    /* Enable Unicode character classification.  Try a UTF-8 locale explicitly
     * before falling back to LC_CTYPE from the environment — stock containers
     * (e.g. ubuntu:24.04) leave LANG unset, where setlocale("") yields the
     * POSIX `C` locale and iswalpha/iswupper return 0 for non-ASCII chars.
     * `C.UTF-8` is universally available on glibc and modern macOS. */
    if (!setlocale(LC_CTYPE, "C.UTF-8") &&
        !setlocale(LC_CTYPE, "en_US.UTF-8")) {
        setlocale(LC_CTYPE, "");
    }
#ifndef __SANITIZE_ADDRESS__
#if !__has_feature(address_sanitizer)
    install_crash_handler();
#endif
#endif
#endif

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--batch") == 0) {
            batch = 1;
        } else if (strcmp(argv[i], "--color") == 0) {
            cl_repl_color = 1;
            color_set = 1;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            cl_repl_color = 0;
            color_set = 1;
        } else if (strcmp(argv[i], "--non-interactive") == 0) {
            non_interactive = 1;
        } else if (strcmp(argv[i], "--no-jit") == 0) {
            no_jit = 1;
        } else if (strcmp(argv[i], "--boot-log") == 0) {
            boot_log = 1;
        } else if (strcmp(argv[i], "--no-userinit") == 0) {
            no_userinit = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            exit(0);
        } else if (strcmp(argv[i], "--load") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --load requires a file argument\n");
                print_usage();
                exit(1);
            }
            if (action_count < MAX_ACTIONS) {
                actions[action_count].is_eval = 0;
                actions[action_count].arg = argv[++i];
                action_count++;
            }
        } else if (strcmp(argv[i], "--eval") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --eval requires an expression argument\n");
                print_usage();
                exit(1);
            }
            if (action_count < MAX_ACTIONS) {
                actions[action_count].is_eval = 1;
                actions[action_count].arg = argv[++i];
                action_count++;
            }
        } else if (strcmp(argv[i], "--script") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --script requires a file argument\n");
                print_usage();
                exit(1);
            }
            script = 1;
            script_file = argv[++i];
        } else if (strcmp(argv[i], "--heap") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --heap requires a size argument\n");
                print_usage();
                exit(1);
            }
            heap_size = parse_size(argv[++i]);
            if (heap_size == 0) {
                fprintf(stderr, "Error: invalid heap size '%s'\n", argv[i]);
                print_usage();
                exit(1);
            }
        } else if (strcmp(argv[i], "--vm-stack") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --vm-stack requires a size argument\n");
                print_usage();
                exit(1);
            }
            {
                uint32_t stack_bytes = parse_size(argv[++i]);
                if (stack_bytes == 0) {
                    fprintf(stderr, "Error: invalid stack size '%s'\n", argv[i]);
                    print_usage();
                    exit(1);
                }
                stack_entries = stack_bytes / 4; /* each entry is uint32_t */
            }
        } else if (strcmp(argv[i], "--frames") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --frames requires a number\n");
                print_usage();
                exit(1);
            }
            {
                uint32_t n = parse_size(argv[++i]);
                if (n == 0) {
                    fprintf(stderr, "Error: invalid frame count '%s'\n", argv[i]);
                    print_usage();
                    exit(1);
                }
                frame_count = (int)n;
            }
        } else if (strncmp(argv[i], "--", 2) == 0) {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            print_usage();
            exit(1);
        } else {
            /* Bare file argument: treat as --load */
            if (action_count < MAX_ACTIONS) {
                actions[action_count].is_eval = 0;
                actions[action_count].arg = argv[i];
                action_count++;
            }
        }
    }

    /* Default: color on for interactive, off for batch/script/non-interactive */
    if (!color_set)
        cl_repl_color = !(batch || script || non_interactive);

    /* Boot progress lines ("; [boot] ...") are opt-in via --boot-log —
     * useful on a slow Amiga boot or when chasing a startup-time regression,
     * noise otherwise (and piped tests match stdout exactly). */
    cl_quiet_boot = !boot_log;

    platform_init();
    /* Anchor GET-INTERNAL-REAL-TIME at process start so Lisp code can
     * measure launch-to-here directly (boot/load profiling). */
    cl_internal_time_init();
    cl_thread_init();  /* Must be first — sets up CT for all other init */

    /* Initialize C stack base for overflow detection */
    cl_c_stack_base = (char *)&batch;

    /* Initialize subsystems in dependency order */
    cl_error_init();
    /* Validate the setjmp-overrun guard before any CL_CATCH / NLX frame is
     * used (MorphOS PPC setjmp writes past sizeof(jmp_buf) — see types.h). */
    cl_setjmp_overrun_check();
    cl_mem_init(heap_size ? heap_size : CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
    cl_reader_init();
    cl_printer_init();
    cl_compiler_init();

    /* CLAMIGA_FORCE_SPEED=0..3 pins (optimize (speed N)) for the whole
     * process, overriding declaim/declare — the peephole differential
     * harness runs the same corpus at 0 and 3 and compares results. */
    {
        char fsbuf[8];
        const char *fs = platform_getenv("CLAMIGA_FORCE_SPEED", fsbuf,
                                         (int)sizeof(fsbuf));
        if (fs && fs[0] >= '0' && fs[0] <= '3' && fs[1] == '\0') {
            extern int cl_optimize_force_speed;
            cl_optimize_force_speed = fs[0] - '0';
        }
    }
    cl_jit_init();
    if (no_jit) cl_jit_set_active(0);
    cl_vm_init(stack_entries, frame_count);
    cl_stream_init();
    cl_builtins_init();
    cl_debugger_init();
    cl_repl_init_no_userinit(no_userinit);

#ifdef DEBUG_GC_STRESS
    /* Enable per-allocation forced compaction only after boot, so the FASL/
     * boot load runs at normal speed but --load/--eval and the REPL exercise
     * every allocation under a moving GC. */
    if (getenv("CLAMIGA_GC_STRESS")) {
        extern int cl_gc_stress_ready;
        cl_gc_stress_ready = 1;
    }
#endif

    if (script) {
        /* Script/batch: execute --load/--eval actions before mode entry */
        for (i = 0; i < action_count; i++) {
            int err; CL_CATCH(err);
            if (err == CL_ERR_NONE) {
                if (actions[i].is_eval) {
                    eval_string_in_cl_user(actions[i].arg);
                } else {
                    cl_load_file(actions[i].arg);
                }
                CL_UNCATCH();
            } else if (err == CL_ERR_EXIT) {
                CL_UNCATCH();
                goto shutdown;
            } else {
                cl_error_print();
                cl_vm.sp = 0;
                cl_vm.fp = 0;
                CL_UNCATCH();
            }
        }

        /* Script mode: load file and exit */
        {
            int err; CL_CATCH(err);
            if (err == CL_ERR_NONE) {
                cl_load_file(script_file);
                CL_UNCATCH();
            } else if (err == CL_ERR_EXIT) {
                CL_UNCATCH();
                goto shutdown;
            } else {
                cl_error_print();
                cl_vm.sp = 0;
                cl_vm.fp = 0;
                CL_UNCATCH();
            }
        }
    } else if (non_interactive) {
        /* Non-interactive: execute --load/--eval actions and exit */
        for (i = 0; i < action_count; i++) {
            int err; CL_CATCH(err);
            if (err == CL_ERR_NONE) {
                if (actions[i].is_eval) {
                    eval_string_in_cl_user(actions[i].arg);
                } else {
                    cl_load_file(actions[i].arg);
                }
                CL_UNCATCH();
            } else if (err == CL_ERR_EXIT) {
                CL_UNCATCH();
                goto shutdown;
            } else {
                cl_error_print();
                cl_vm.sp = 0;
                cl_vm.fp = 0;
                CL_UNCATCH();
            }
        }
    } else if (batch) {
        /* Batch: execute --load/--eval actions before batch REPL */
        for (i = 0; i < action_count; i++) {
            int err; CL_CATCH(err);
            if (err == CL_ERR_NONE) {
                if (actions[i].is_eval) {
                    eval_string_in_cl_user(actions[i].arg);
                } else {
                    cl_load_file(actions[i].arg);
                }
                CL_UNCATCH();
            } else if (err == CL_ERR_EXIT) {
                CL_UNCATCH();
                goto shutdown;
            } else {
                cl_error_print();
                cl_vm.sp = 0;
                cl_vm.fp = 0;
                CL_UNCATCH();
            }
        }
        cl_repl_batch();
    } else {
        /* Drain residual CLI data from stdin (AmigaOS leaks command line to Input()) */
        platform_drain_input();
        platform_write_string("\n");
        /* Line 1:   )))     \\ */
        cl_color_set(CL_COLOR_LIGHT_BLUE);
        platform_write_string("  )))     ");
        cl_color_set(CL_COLOR_RED);
        platform_write_string("\\\\\n");
        /* Line 2:  )))       \\          CL-Amiga v<version> */
        cl_color_set(CL_COLOR_LIGHT_BLUE);
        platform_write_string(" )))       ");
        cl_color_set(CL_COLOR_RED);
        platform_write_string("\\\\          ");
        cl_color_set(CL_COLOR_DIM_CYAN);
        platform_write_string("CL-Amiga v" CL_VERSION_STRING "\n");
        /* Line 3: )))         \\ */
        cl_color_set(CL_COLOR_LIGHT_BLUE);
        platform_write_string(")))         ");
        cl_color_set(CL_COLOR_RED);
        platform_write_string("\\\\\n");
        /* Line 4: )))         /\\        Common Lisp for AmigaOS 3+ */
        cl_color_set(CL_COLOR_LIGHT_BLUE);
        platform_write_string(")))         ");
        cl_color_set(CL_COLOR_RED);
        platform_write_string("//\\\\        ");
        cl_color_set(CL_COLOR_DIM_CYAN);
        platform_write_string("Common Lisp for AmigaOS 3+\n");
        /* Line 5:  )))       //  \\ */
        cl_color_set(CL_COLOR_LIGHT_BLUE);
        platform_write_string(" )))       ");
        cl_color_set(CL_COLOR_RED);
        platform_write_string("//  \\\\\n");
        /* Line 6:   )))     //    \\ */
        cl_color_set(CL_COLOR_LIGHT_BLUE);
        platform_write_string("  )))     ");
        cl_color_set(CL_COLOR_RED);
        platform_write_string("//    \\\\\n");
        cl_color_reset();
        platform_write_string("\nType (quit) to exit.\n\n");

        /* Interactive: execute --load/--eval actions after banner */
        for (i = 0; i < action_count; i++) {
            int err; CL_CATCH(err);
            if (err == CL_ERR_NONE) {
                if (actions[i].is_eval) {
                    eval_string_in_cl_user(actions[i].arg);
                } else {
                    cl_load_file(actions[i].arg);
                }
                CL_UNCATCH();
            } else if (err == CL_ERR_EXIT) {
                CL_UNCATCH();
                goto shutdown;
            } else {
                cl_error_print();
                cl_vm.sp = 0;
                cl_vm.fp = 0;
                CL_UNCATCH();
            }
        }

        cl_repl();
    }

shutdown:
#ifdef DEBUG_SHUTDOWN
#define SHUTDOWN_TRACE(s) platform_write_string("[shutdown] " s "\n")
#else
#define SHUTDOWN_TRACE(s) ((void)0)
#endif
    SHUTDOWN_TRACE("enter shutdown");
    cl_stream_shutdown();
    SHUTDOWN_TRACE("stream done");
    cl_vm_shutdown();
    SHUTDOWN_TRACE("vm done");
    /* platform_shutdown() must run BEFORE cl_thread_shutdown(): on AmigaOS it
     * tears down the socket reactor via sock_call(), which wraps its WaitPort
     * in cl_gc_enter/leave_safe_region().  Those need gc_mutex/gc_condvar,
     * which cl_thread_shutdown() destroys (and NULLs).  Running it after would
     * call ObtainSemaphore(NULL) and hang the process at exit once the reactor
     * has been started (i.e. after any socket op). */
    platform_shutdown();
    SHUTDOWN_TRACE("platform done");
    cl_thread_shutdown();
    SHUTDOWN_TRACE("thread done");

    /* If worker threads are still running, cl_thread_shutdown() deliberately
     * LEAKED the GC/thread primitives rather than yank them out from under
     * live code (see thread.c).  For the same reason we must NOT free the
     * arena here: those workers keep reading the arena and symbol table (e.g.
     * a spinner evaluating (loop until *stop* ...) calls cl_symbol_value,
     * which dereferences arena-relative offsets).  cl_mem_shutdown() would
     * free it out from under them, and returning from main then runs glibc's
     * post-main teardown while the same threads are still live — either way a
     * SIGSEGV (or hang) in teardown.  Skip the free and terminate the process
     * immediately via _exit(); the OS reclaims the arena and everything else.
     * This mirrors the Amiga fast-exit path below, but is required on every
     * platform whenever workers outlive the main thread.
     *
     * No cl_thread_restore_main_tls() call needed here: _exit() — unlike
     * exit()/abort() — intentionally bypasses the crt0 post-main teardown
     * that re-reads tc_UserData (that bypass is why _exit() is used on the
     * non-MorphOS Amiga path below too), so a stale tc_UserData can't be
     * observed after it.  Restoring it here would instead be actively
     * harmful: cl_get_current_thread()'s fast path only trusts
     * cl_main_thread_ptr when cl_thread_count<=1, so while workers are still
     * registered (as here) it resolves CT via this task's TLS — corrupting
     * that TLS out from under a live crash handler or worker safepoint is
     * exactly the kind of "yank shared state out from under live code" this
     * whole branch exists to avoid (see the leaked-primitives comment in
     * cl_thread_shutdown). */
    if (cl_thread_count > 0) {
        SHUTDOWN_TRACE("workers still running — fast _exit, arena left to OS");
        fflush(NULL);
        _exit(cl_exit_code);
    }

    cl_mem_shutdown();
    SHUTDOWN_TRACE("mem done");

#if defined(PLATFORM_AMIGA) && !defined(PLATFORM_MORPHOS)
    /* m68k AmigaOS (-noixemul): every clamiga-owned resource is already
     * released above, but *returning* from main runs the C runtime's post-main
     * teardown (atexit handlers + fclose of the buffered stdio streams bound to
     * the console), which hangs — the process is left frozen in the Task list
     * and the Shell never regains control.  Since our own cleanup is complete
     * and the OS reclaims the rest on process exit, flush any pending C stdio
     * and terminate via _exit(), which hands the return code back to DOS
     * without running the hanging teardown. */
    SHUTDOWN_TRACE("calling fflush(NULL)");
    fflush(NULL);
    SHUTDOWN_TRACE("fflush done — calling _exit");
    _exit(cl_exit_code);
    SHUTDOWN_TRACE("_exit returned (should never happen)");
#elif defined(PLATFORM_MORPHOS)
    /* MorphOS PPC: the _exit() workaround above was masking the setjmp jmp_buf
     * overrun that corrupted every NLX/error frame throughout the run (fixed
     * 2026-07-08, commit 253f6b7).  With that gone, return from main normally
     * and let crt0 run its teardown — the process was hanging in exec/Wait()
     * inside _exit, not in the C runtime cleanup.  Flush stdio first, then fall
     * through to the return below. */
    SHUTDOWN_TRACE("calling fflush(NULL)");
    fflush(NULL);
    /* Restore the main task's tc_UserData before returning: cl_thread_init
     * overwrote it with our CL_Thread*, and MorphOS's -noixemul crt0 re-reads
     * tc_UserData during its post-main teardown.  Leaving our pointer there is
     * what froze the machine after "returning from main". */
    cl_thread_restore_main_tls();
    SHUTDOWN_TRACE("fflush done, TLS restored — returning from main");
#endif
#undef SHUTDOWN_TRACE

    return cl_exit_code;
}
