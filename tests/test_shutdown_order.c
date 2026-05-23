/*
 * test_shutdown_order.c — Regression for the AmigaOS shutdown hang.
 *
 * Bug: main()'s exit sequence destroyed the GC coordination primitives
 * (gc_mutex/gc_condvar, which cl_thread_shutdown() frees and NULLs) BEFORE
 * platform_shutdown() tore down the socket reactor.  On AmigaOS the reactor
 * teardown marshals a request via sock_call(), which wraps its WaitPort in
 * cl_gc_enter_safe_region()/cl_gc_leave_safe_region().  Those locked the
 * now-NULL gc_mutex (ObtainSemaphore(NULL)) and hung the process at exit —
 * FS-UAE never quit once any socket test had started the reactor.
 *
 * Two defenses, both introduced with this fix:
 *   1. cl_gc_enter/leave_safe_region() are no-ops when gc_mutex is NULL.
 *   2. (src/main.c) platform_shutdown() now runs before cl_thread_shutdown().
 *
 * This test exercises defense #1: after cl_thread_shutdown() has torn down GC
 * coordination, a safe-region pair must complete without touching the
 * destroyed mutex.  Without the guard this faults / deadlocks; with it the
 * calls return and we reach the assertion.
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
#include "core/thread.h"
#include "platform/platform.h"

static void full_init(void)
{
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(4 * 1024 * 1024);
    cl_package_init();
    cl_symbol_init();
    cl_reader_init();
    cl_printer_init();
    cl_compiler_init();
    cl_vm_init(0, 0);
    cl_builtins_init();
    cl_repl_init();
}

TEST(safe_region_noop_after_gc_coordination_destroyed)
{
    full_init();

    /* Mirror the shutdown ordering that triggered the hang: GC coordination
     * is gone, but a safe-region pair still runs (as platform_shutdown's
     * reactor teardown does on Amiga). */
    cl_thread_shutdown();          /* frees + NULLs gc_mutex / gc_condvar */
    cl_gc_enter_safe_region();     /* must NOT lock the destroyed mutex */
    cl_gc_leave_safe_region();
    ASSERT(1);                     /* reached here => no hang / fault */

    /* Finish teardown (mirrors main.c order with platform_shutdown already
     * accounted for above; cl_thread_shutdown runs exactly once). */
    cl_vm_shutdown();
    cl_mem_shutdown();
    platform_shutdown();
}

int main(void)
{
    test_init();
    RUN(safe_region_noop_after_gc_coordination_destroyed);
    REPORT();
}
