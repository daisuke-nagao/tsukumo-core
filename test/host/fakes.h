/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// The fake machine the PC unit tests run the kernel on: the interrupt flag, a
// few device registers and a log of every access to them.
//
// What it cannot do: switch stacks. A task that blocks on the target is
// switched out inside EI(); here the call that blocked is abandoned with
// longjmp at that point (HOST_EXPECT_WAIT), and the test then looks at the
// kernel's queues. The code a blocking call runs after it wakes up (reading the
// wake-up cause, copying out the result) never runs on the PC, so the
// QEMU tests are what check the values a woken task sees. For the same reason
// a task cannot end here: its switch (__launch_task()) fails the test.
//
// The PC has 64 bit pointers, and the kernel keeps 32 bit addresses in plain
// integers (for example the entry point and the argument in a new task's
// initial frame). Those round trips are not checked here either.

#ifndef UUID_01A17C3E_ED12_7403_A0D3_A44F99A13F2E
#define UUID_01A17C3E_ED12_7403_A0D3_A44F99A13F2E

#include <setjmp.h>

#include <tk/tkernel.h>

#include "asm/rv32/address.h"
#include "event_flag.h"
#include "list.h"
#include "mailbox.h"
#include "panic.h"
#include "semaphore.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct {
  INT port;
  UINT width;
  UD value;
  BOOL is_write;
  BOOL interrupts_enabled;
} host_access_t;

// One kernel tick (10 ms) in machine timer counts (10 MHz).
enum { HOST_TICK = 100000 };

// A test that makes more accesses than this fails, rather than a log that
// silently stops growing and answers from a truncated history.
enum { HOST_LOG_MAX = 256 };

extern host_access_t host_log[HOST_LOG_MAX];
extern UINT host_log_count;

// The full machine timer; MMIO reads return the selected 32 bit word.
extern UD host_mtime;

// Advance time once, immediately after the next low-word read, to test a
// carry between the kernel's high/low/high reads. Reset to zero after use.
extern UD host_mtime_advance_after_low_read;

// Whether interrupts are enabled. Every kernel call must leave them as it
// found them, on every path out.
extern BOOL host_interrupts_enabled(void);

// The kernel's timer queue and timer interrupt handler, which timer.h does not
// declare.
extern tkmc_list_head tkmc_timer_queue;
extern void tkmc_timer_handler(void);

// The kernel's object tables, which their headers do not declare.
extern SEMCB tkmc_semcbs[CFN_MAX_SEMID];
extern FLGCB tkmc_flgcbs[CFN_MAX_FLGID];
extern MBXCB tkmc_mbxcbs[CFN_MAX_MBXID];

// Everything the kernel keeps, as bytes: every task, ready queue, the timer
// queue, every semaphore, event flag and mailbox, the current and next task,
// and the number of context switch requests (writes to the software interrupt
// register; its level is no use, setup may have left it set).
// host_check_kernel_queues() is not enough to show that a call changed nothing:
// it accepts a task that is waiting without being on any queue.
typedef struct {
  unsigned char tcbs[sizeof(tkmc_tcbs)];
  unsigned char ready[sizeof(tkmc_ready_queue)];
  unsigned char timer[sizeof(tkmc_timer_queue)];
  unsigned char sems[sizeof(tkmc_semcbs)];
  unsigned char flgs[sizeof(tkmc_flgcbs)];
  unsigned char mbxs[sizeof(tkmc_mbxcbs)];
  TCB *current;
  TCB *next;
  UINT switch_requests;
} host_snapshot_t;

extern void host_take_snapshot(host_snapshot_t *s);

// Fails the running test, with what in the message, unless the kernel is as
// the snapshot found it.
extern void host_assert_unchanged(const host_snapshot_t *s, const char *what);

// Fails the running test unless the kernel's queues are consistent: every queue
// (ready, timer and the wait queue of every existing semaphore, event flag and
// mailbox) is a well formed ring, a task is on at most one of the ready and
// timer queues, a READY or RUNNING task is on the ready queue of its priority
// and waits for nothing and has no time limit, a waiting task is on no ready
// queue and is on the timer queue exactly when it has a time limit, and every
// task on the wait queue of an object is waiting for that kind of object.
extern void host_check_kernel_queues(void);

// host_check_kernel_queues(), and fails the running test if the call before
// left interrupts off.
extern void host_check_kernel_state(void);

// Resets the fake machine and the kernel to a fresh boot: interrupts enabled,
// registers and log cleared, no current task, and every kernel table
// initialised.
extern void host_reset(void);

// Creates a task at priority prio and starts it (READY), and returns its ID.
extern ID host_make_task(PRI prio);

// Makes the task the running one, as the scheduler would have.
extern void host_set_current(ID tskid);

// The priorities most tests use: a task and one it outranks.
enum { HOST_HIGH_PRIORITY = 4, HOST_LOW_PRIORITY = 5 };

// The machine software interrupt register. Every write to it is a context
// switch request.
enum { HOST_MSIP = CLINT_BASE_ADDRESS + CLINT_MSIP_OFFSET };

#define HOST_COUNT(array) (sizeof(array) / sizeof((array)[0]))

static inline TCB *host_tcb(ID tskid) { return &tkmc_tcbs[tskid - 1]; }

// Whether node is linked on the ring that head starts.
extern BOOL host_in_queue(const tkmc_list_head *head,
                          const tkmc_list_head *node);

// Makes a task at HOST_HIGH_PRIORITY the running one and lets it sleep for
// ticks (2 or more), counted as tkmc_ms_to_ticks() does. Returns its ID.
extern ID host_sleep_for(UINT ticks);

// Makes a task at HOST_LOW_PRIORITY that never waits the running one, so the
// queues are complete.
extern void host_run_idle_task(void);

// A small linear congruential generator, so that a seed gives the same
// sequence on every run.
extern UINT host_next_random(UINT *state);

// One semaphore (no units, at most one), one event flag (no bits set) and one
// mailbox, each TA_TFIFO. The test fails if one cannot be made.
typedef struct {
  ID semid;
  ID flgid;
  ID mbxid;
} host_objects_t;
extern host_objects_t host_make_objects(void);

// Two tasks at HOST_HIGH_PRIORITY, the first one running, and
// host_make_objects(). The priorities are equal so that a call that wrongly
// yields, unlinks a task or touches an object changes a snapshot.
extern void host_two_tasks_and_objects(void);

// Creates semaphores until the kernel refuses with E_LIMIT, and returns how
// many it made. Nothing deletes them. A free list that does not end fails the
// test instead of hanging it.
extern UINT host_count_creatable_semaphores(void);

// The execution context of a call: in_isr is the state the RV32 trap entries
// set (tkmc_isr_depth), whatever the interrupt flag says; interrupts_off is a
// task's own DI() or the MIE a handler left off. host_leave() goes back to a
// task with interrupts on.
typedef struct {
  BOOL interrupts_off;
  UINT intsts;
} host_context_t;
extern host_context_t host_enter(BOOL in_isr, BOOL interrupts_off);
extern void host_leave(host_context_t context);

// Advances the machine timer by n kernel ticks (HOST_TICK counts each), running
// the timer interrupt handler after each.
extern void host_tick(UINT ticks);

// The number of writes to port in the access log, and the last value written.
extern UINT host_write_count(INT port);
extern UD host_last_write(INT port);

// The number of accesses to port in the access log, of writes or of reads.
extern UINT host_access_count(INT port, BOOL is_write);

// The PLIC: pending sources are claimed only when enabled with nonzero
// priority; highest priority wins, then lowest source ID. Threshold does not
// change a claim result.
extern void host_plic_set_enabled(UINT src, BOOL on);
extern BOOL host_plic_is_enabled(UINT src);
extern void host_plic_raise(UINT src);
// Whether src is pending, read through the pending register as the kernel
// reads it (the read is logged).
extern BOOL host_plic_pending(UINT src);
extern UW host_plic_threshold(void);
extern void host_plic_set_threshold(UW value);
extern UW host_plic_threshold_highest(void);
extern void host_plic_set_priority(UINT src, UW priority);
// How many ignored completions have occurred since the last claim read.
extern UINT host_plic_ignored_completions(void);
// The port of the claim and completion register, for host_last_write().
extern INT host_plic_claim_port(void);
// The port of the priority register of src.
extern INT host_plic_priority_port(UINT src);

// A TA_HLNG handler that counts its calls in host_handler_calls and keeps the
// interrupt number of the last one in host_handler_intno, and a definition of
// it for tk_def_int(). host_reset() sets both to 0.
extern UINT host_handler_calls;
extern UINT host_handler_intno;
extern void host_counting_handler(UINT intno);
extern const T_DINT host_counting_hlng;

extern jmp_buf host_wait_jmp;
extern volatile int host_wait_armed;
extern volatile int host_waited;

// Arms HOST_EXPECT_WAIT, and clears a software interrupt request an earlier
// call may have left: on the target the switch would have taken it already,
// and a request the statement itself does not make must not count.
extern void host_arm_wait(void);

// Runs statement, and stops it where the current task would be switched out:
// the task is waiting, a context switch has been requested by the statement
// (the machine software interrupt is pending) and interrupts are back on. The
// test fails if the statement returns instead, or if it leaves interrupts off.
#define HOST_EXPECT_WAIT(statement)                                            \
  do {                                                                         \
    host_waited = 0;                                                           \
    if (setjmp(host_wait_jmp) == 0) {                                          \
      host_arm_wait();                                                         \
      statement;                                                               \
    } else {                                                                   \
      host_waited = 1;                                                         \
    }                                                                          \
    host_wait_armed = 0;                                                       \
    TEST_ASSERT_TRUE_MESSAGE(host_waited,                                      \
                             #statement " returned without "                   \
                                        "switching the task out");             \
    TEST_ASSERT_TRUE(host_interrupts_enabled());                               \
  } while (0)

// The PC does not compile kernel/panic.c, so fakes.c stands in for
// tkmc_fatal_context(); the QEMU tests check the real one. The stand-in aborts
// the test run unless HOST_CATCH_FATAL_CONTEXT is around the call.
typedef struct {
  BOOL stopped;         // The call ended in tkmc_fatal_context().
  tkmc_context_api api; // The API it named.
  BOOL interrupts_off;  // Interrupts were off when it was called.
} host_fatal_t;

extern jmp_buf host_fatal_jmp;
extern volatile BOOL host_fatal_armed;
extern volatile host_fatal_t host_fatal;

// Runs statement, and stops it where it calls tkmc_fatal_context(). result
// (a host_fatal_t) says whether, and how, it got there.
#define HOST_CATCH_FATAL_CONTEXT(result, statement)                            \
  do {                                                                         \
    host_fatal = (host_fatal_t){FALSE, TKMC_CONTEXT_NONE, FALSE};              \
    host_fatal_armed = TRUE;                                                   \
    if (setjmp(host_fatal_jmp) == 0) {                                         \
      statement;                                                               \
    }                                                                          \
    host_fatal_armed = FALSE;                                                  \
    (result) = host_fatal;                                                     \
  } while (0)

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A17C3E_ED12_7403_A0D3_A44F99A13F2E */
