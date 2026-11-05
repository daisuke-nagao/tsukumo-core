/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// How long the timer interrupt handler runs when several timed waits end in
// the same tick. The handler scans the timer queue twice for every wait it
// ends (kernel/timer.c), so the work grows with the square of the waits that
// end together, and it runs with interrupts disabled. This suite records that
// work. It has no budget to check: the cases report the count, and fail only
// when the measurement is not the one they claim.
//
// The timer trap is the real one. Its C handler is wrapped at link time
// (--wrap=tkmc_timer_handler), so the wrapper reads the guest's
// retired-instruction counter around the handler on the trap path. Reading it
// in the case would add the trap entry and the task switch. The wrapper never
// asserts: it stores what it saw, and the case checks it as a task.
//
// The wrapper watches the children of the case, and keeps the count of the
// handler call in which most of them left their sleep. Other waits can end in
// the same call (the runner polls with its own timed sleep), and the report
// says how many. The case itself blocks without a time limit, so its own sleep
// is never one of them.
//
// Every sleep of a case starts in one tick and has the same time limit, so they
// all end in one handler call. The children wait for a signal and sleep right
// after it; they outrank the case, so the whole burst runs inside the call that
// sends the signal. The check that the sleeps began in one tick compares the
// ticks they have left, so it assumes the queue holds a count of ticks for each
// entry, as it does now; a queue that stores differences would need another
// check.

#include <tk/tkernel.h>
#include <unity.h>

#include "runner.h"
#include "task.h"

extern tkmc_list_head tkmc_timer_queue;
extern void __real_tkmc_timer_handler(void);

enum {
  CHILD_PRIORITY = 2,
  SLEEP_MS = 50,
  // The kernel has CFN_MAX_TSKID tasks; the idle task, the runner and the case
  // take three of them.
  MOST_CHILDREN = CFN_MAX_TSKID - 3
};
_Static_assert(
    CHILD_PRIORITY < TEST_CASE_PRIORITY,
    "a child must preempt the case, to wait before the case goes on");

static ID s_signal;
static ID s_done;
static ID s_children[MOST_CHILDREN];

// What the wrapper saw in the handler call that ended the most sleeps of the
// children. The counts are taken outside the instruction count.
static volatile UINT s_watched; // The children the wrapper looks at.
static volatile UINT s_ended;   // Sleeps of the children that the call ended.
static volatile UINT s_removed; // Entries the call took off the timer queue.
static volatile UW s_cost;      // Instructions of the call.

static UINT queue_length(void) {
  UINT length = 0;
  for (tkmc_list_head *link = tkmc_timer_queue.next; link != &tkmc_timer_queue;
       link = link->next) {
    ++length;
  }
  return length;
}

static UINT sleeping_children(UINT watched) {
  UINT sleeping = 0;
  for (UINT i = 0; i < watched; ++i) {
    const volatile TCB *tcb = &tkmc_tcbs[s_children[i] - 1];
    if (tcb->tskstat == TTS_WAI && tcb->tskwait == TTW_DLY) {
      ++sleeping;
    }
  }
  return sleeping;
}

void __wrap_tkmc_timer_handler(void) {
  UINT watched = s_watched;
  UINT sleeping_before = sleeping_children(watched);
  UINT length_before = queue_length();
  UW before = test_instret();
  __real_tkmc_timer_handler();
  UW used = test_instret() - before;
  UINT ended = sleeping_before - sleeping_children(watched);
  if (ended > s_ended) {
    s_ended = ended;
    s_removed = length_before - queue_length();
    s_cost = used;
  }
}

static void child(void *arg) {
  (void)arg;
  TEST_ASSERT_EQUAL(E_OK, tk_wai_sem(s_signal, 1, TMO_FEVR));
  TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(SLEEP_MS));
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(s_done, 1));
  test_checkpoint();
}

// Every child sleeps, and they all wait for the same number of ticks. One that
// started in an earlier tick has fewer left, so a burst that crossed a tick
// edge is caught here and not taken for a measurement.
static void assert_all_sleep_for_the_same_time(UINT n) {
  UINT ticks = tkmc_tcbs[s_children[0] - 1].delay_ticks;
  for (UINT i = 0; i < n; ++i) {
    const volatile TCB *tcb = &tkmc_tcbs[s_children[i] - 1];
    TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_WAI, tcb->tskstat,
                                   "a child is not waiting");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(TTW_DLY, tcb->tskwait,
                                   "a child is not sleeping");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        ticks, tcb->delay_ticks,
        "the waits did not start in one tick, so they will not end together");
  }
}

static void report(UINT n) {
  UnityPrint("timer-cost: ");
  UnityPrintNumberUnsigned(n);
  UnityPrint(" waits and ");
  UnityPrintNumberUnsigned(s_removed - s_ended);
  UnityPrint(" others ended in one tick: ");
  UnityPrintNumberUnsigned(s_cost);
  UnityPrint(" instructions in the timer handler");
  UNITY_PRINT_EOL();
}

// n timed waits that end in one tick, and the instructions the handler took.
static void measure(UINT n) {
  s_watched = 0;
  s_signal = tk_cre_sem(
      &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = n});
  TEST_ASSERT_GREATER_THAN(E_OK, s_signal);
  s_done = tk_cre_sem(
      &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = n});
  TEST_ASSERT_GREATER_THAN(E_OK, s_done);
  for (UINT i = 0; i < n; ++i) {
    s_children[i] = test_spawn(child, CHILD_PRIORITY, NULL);
  }

  s_ended = 0;
  s_removed = 0;
  s_cost = 0;
  s_watched = n;
  // The children start their sleeps right after the signal. The edge makes the
  // whole burst fit in the tick that follows.
  TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(1));
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(s_signal, n));
  assert_all_sleep_for_the_same_time(n);

  // No time limit, so that the case adds no entry to the timer queue.
  TEST_ASSERT_EQUAL(E_OK, tk_wai_sem(s_done, n, TMO_FEVR));
  s_watched = 0;
  test_join_expect(n);
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(s_signal));
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(s_done));
  TEST_ASSERT_EQUAL_UINT_MESSAGE(n, s_ended,
                                 "no timer interrupt ended all the sleeps");
  TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(0, s_cost, "minstret does not count");
  report(n);
}

static void one_wait_ends_in_a_tick(void) { measure(1); }
static void eight_waits_end_in_one_tick(void) { measure(8); }
static void the_most_waits_end_in_one_tick(void) { measure(MOST_CHILDREN); }

static const test_case_t s_cases[] = {
    {"one_wait_ends_in_a_tick", one_wait_ends_in_a_tick},
    {"eight_waits_end_in_one_tick", eight_waits_end_in_one_tick},
    {"the_most_waits_end_in_one_tick", the_most_waits_end_in_one_tick},
};

TEST_SUITE_MAIN("timer-cost", s_cases, 5000)
