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
// QEMU tests are what check the values a woken task sees.
//
// The PC has 64 bit pointers, and the kernel keeps 32 bit addresses in plain
// integers (for example the entry point and the argument in a new task's
// initial frame). Those round trips are not checked here either.

#ifndef UUID_01A17C3E_ED12_7403_A0D3_A44F99A13F2E
#define UUID_01A17C3E_ED12_7403_A0D3_A44F99A13F2E

#include <setjmp.h>

#include <tk/tkernel.h>

#include "event_flag.h"
#include "list.h"
#include "mailbox.h"
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

// The kernel's timer queue, which timer.h does not declare.
extern tkmc_list_head tkmc_timer_queue;

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

// Resets the fake machine and the kernel to a fresh boot: interrupts enabled,
// registers and log cleared, no current task, and every kernel table
// initialised.
extern void host_reset(void);

// Creates a task at priority prio and starts it (READY), and returns its ID.
extern ID host_make_task(PRI prio);

// Makes the task the running one, as the scheduler would have.
extern void host_set_current(ID tskid);

// Advances the machine timer by n kernel ticks (HOST_TICK counts each), running
// the timer interrupt handler after each.
extern void host_tick(UINT ticks);

// The number of writes to port in the access log, and the last value written.
extern UINT host_write_count(INT port);
extern UD host_last_write(INT port);

extern jmp_buf host_wait_jmp;
extern volatile int host_wait_armed;
extern volatile int host_waited;

// Runs statement, and stops it where the current task would be switched out:
// the task is waiting, a context switch has been requested (the machine
// software interrupt is pending) and interrupts are back on. The test fails if
// the statement returns instead, or if it leaves interrupts off.
#define HOST_EXPECT_WAIT(statement)                                            \
  do {                                                                         \
    host_waited = 0;                                                           \
    if (setjmp(host_wait_jmp) == 0) {                                          \
      host_wait_armed = 1;                                                     \
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

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A17C3E_ED12_7403_A0D3_A44F99A13F2E */
