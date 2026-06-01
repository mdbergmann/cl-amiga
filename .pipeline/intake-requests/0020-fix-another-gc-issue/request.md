---
id: 0020-fix-another-gc-issue
type: bug
status: ready
title: fix another gc issue
---

# fix another gc issue

oot cause found — and it's a clean signature. Here's what the sample shows across threads at 255% CPU:

  ┌────────────────────┬───────────────────────────────────────────────────────────────────────────────────────────────────────────┬───────────────────────────────────────────┐
  │       Thread       │                                                   Stack                                                   │                   State                   │
  ├────────────────────┼───────────────────────────────────────────────────────────────────────────────────────────────────────────┼───────────────────────────────────────────┤
  │ GC worker          │ print_obj → print_list → … → cl_make_string_output_stream → cl_gc → cl_gc_stop_the_world                  │ wants to GC, waiting for stop-the-world   │
  ├────────────────────┼───────────────────────────────────────────────────────────────────────────────────────────────────────────┼───────────────────────────────────────────┤
  │ 3× mailbox workers │ call_builtin → bi_condition_wait churning between __psynch_cvwait / __psynch_cvbroad / __psynch_mutexwait │ livelocking — wait→wake→broadcast→re-wait │
  ├────────────────────┼───────────────────────────────────────────────────────────────────────────────────────────────────────────┼───────────────────────────────────────────┤
  │ socket reader      │ bi_read_byte → platform_socket_read → read                                                                │ parked fine (the earlier GC-safe fix)     │
  ├────────────────────┼───────────────────────────────────────────────────────────────────────────────────────────────────────────┼───────────────────────────────────────────┤
  │ accept             │ bi_socket_accept → __accept                                                                               │ fine                                      │
  └────────────────────┴───────────────────────────────────────────────────────────────────────────────────────────────────────────┴───────────────────────────────────────────┘

  The bug: bi_condition_wait is not GC-safe

  One thread tries to GC (cl_gc_stop_the_world) — triggered by cl_make_string_output_stream allocating while printing the error/list on the way into SLDB. To do so it must stop all threads. But the
  worker threads parked in bi_condition_wait (Lisp CONDITION-WAIT, which backs SLY's :spawn mailbox receive) never reach a GC safepoint cleanly — instead they spin in a tight cond_wait ⇄
  cond_broadcast ⇄ mutex_wait loop. The STW never completes, so 3-4 cores peg forever.

  This is the condition-variable analogue of the already-fixed clamiga-stw-gc-blocking-syscall-deadlock (where socket read/accept were made GC-safe). read/accept are now parked safely — but
  bi_condition_wait was never given the same treatment.

  Why it needed ~10 reps / your (room) showed it

  room reported 0 GC collections — the heap (65% full) just hadn't hit its first GC yet. Each disassemble→SLDB→condition-print allocates; after enough of that the first GC fires, and because it fires
  while :spawn worker threads are blocked in condition-wait, it livelocks. So it's not disassemble per se — it's the first GC that happens while a mailbox thread is parked in condition-wait.
  disassemble just reliably allocates (printing the type-error + backtrace) and your session had the heap primed.

  The fix (clamiga C side, builtins_thread.c)

  Make bi_condition_wait GC-safe, exactly like the socket-read/accept fix: register the thread as blocked-at-safepoint before entering pthread_cond_wait, so cl_gc_stop_the_world can proceed without
  it, then re-synchronize with the GC on wakeup. A bigger --heap only postpones the first GC; it can't avoid it.
