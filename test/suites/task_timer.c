/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Tests of task sleeping, waking, releasing a wait and delaying (tk_slp_tsk,
// tk_wup_tsk, tk_rel_wai, tk_dly_tsk).
//
// Most cases below spawn a child at priority 2 while the case itself runs at
// priority 4. The child therefore runs first and is already blocked in its
// first wait when test_spawn() returns, so the case can wake or release it at
// once. Each child reaches test_checkpoint() only after its last assert, and
// the case expects it, so a child that never ran cannot pass.

#include <tk/tkernel.h>
#include <unity.h>

#include "../runner.h"
#include "task.h"

enum { CHILD_PRIORITY = 2, LATE_CHILD_PRIORITY = 5, SLEEP_MS = 50 };
_Static_assert(
    CHILD_PRIORITY < TEST_CASE_PRIORITY,
    "the child must preempt the case, to block before the case goes on");
_Static_assert(LATE_CHILD_PRIORITY > TEST_CASE_PRIORITY,
               "the child must not run until the case blocks");

// tk_slp_tsk() with nobody to wake the task times out.
static void sleeping_child(void *arg) {
  (void)arg;
  TEST_ASSERT_EQUAL(E_TMOUT, tk_slp_tsk(SLEEP_MS));
  test_checkpoint();
}

static void sleep_times_out(void) {
  test_spawn(sleeping_child, CHILD_PRIORITY, 0);
  test_join_expect(1);
}

// Two tk_rel_wai() calls end two sleeps early; the third sleep is not released
// and times out.
static void released_child(void *arg) {
  (void)arg;
  TEST_ASSERT_EQUAL(E_RLWAI, tk_slp_tsk(SLEEP_MS));
  TEST_ASSERT_EQUAL(E_RLWAI, tk_slp_tsk(SLEEP_MS));
  TEST_ASSERT_EQUAL(E_TMOUT, tk_slp_tsk(SLEEP_MS));
  test_checkpoint();
}

static void release_wait(void) {
  ID child = test_spawn(released_child, CHILD_PRIORITY, 0);
  TEST_ASSERT_EQUAL(E_OK, tk_rel_wai(child));
  TEST_ASSERT_EQUAL(E_OK, tk_rel_wai(child));
  test_join_expect(1);
}

// Three tk_wup_tsk() calls end three sleeps with E_OK; the fourth sleep is not
// woken and times out.
static void woken_child(void *arg) {
  (void)arg;
  TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(SLEEP_MS));
  TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(SLEEP_MS));
  TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(SLEEP_MS));
  TEST_ASSERT_EQUAL(E_TMOUT, tk_slp_tsk(SLEEP_MS));
  test_checkpoint();
}

static void wakeup_multiple(void) {
  ID child = test_spawn(woken_child, CHILD_PRIORITY, 0);
  TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(child));
  TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(child));
  TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(child));
  test_join_expect(1);
}

// Wakeups sent before the task sleeps are queued, and each later tk_slp_tsk()
// consumes one without blocking. The child runs below the case, so all three
// tk_wup_tsk() calls reach it before its first sleep.
static void queued_wakeup_child(void *arg) {
  (void)arg;
  TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(SLEEP_MS));
  TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(SLEEP_MS));
  TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(SLEEP_MS));
  TEST_ASSERT_EQUAL(E_TMOUT, tk_slp_tsk(SLEEP_MS));
  test_checkpoint();
}

static void wakeup_queued_before_sleep(void) {
  ID child = test_spawn(queued_wakeup_child, LATE_CHILD_PRIORITY, 0);
  TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(child));
  TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(child));
  TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(child));
  test_join_expect(1);
}

// tk_dly_tsk() must not return early. Only a lower bound is checked: a stalled
// host makes the delay longer, never shorter, so this cannot be flaky.
static void delay_lasts_long_enough(void) {
  UW start = test_mtime_mid_tick();
  TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(SLEEP_MS));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(SLEEP_MS * TEST_MTIME_PER_MS,
                                      (UW)(test_mtime() - start));
}

// The longest time limit becomes 214748366 ticks. The count is copied with
// interrupts off and checked only after the child is woken: a failed assert
// before that would leave the child asleep and time the suite out. Two ticks
// may pass before the copy; a count that wrapped is far outside. The exact
// rounding is checked on the host (test_timeout_ticks.c).
enum { LONGEST_MS = __INT_MAX__, LONGEST_TICKS = 214748366 };

static void long_sleeper(void *arg) {
  (void)arg;
  TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(LONGEST_MS));
  test_checkpoint();
}

static void longest_sleep_counts_its_ticks(void) {
  ID child = test_spawn(long_sleeper, CHILD_PRIORITY, 0);
  UINT intsts;
  DI(intsts);
  UINT ticks = tkmc_tcbs[child - 1].delay_ticks;
  EI(intsts);
  ER woken = tk_wup_tsk(child);
  TEST_ASSERT_EQUAL(E_OK, woken);
  TEST_ASSERT_UINT_WITHIN(1, LONGEST_TICKS - 1, ticks);
  test_join_expect(1);
}

enum { WAKEUP_LIMIT = 4095 };
static ID s_wakeup_gate;

static void consume_wakeup_limit(void *arg) {
  (void)arg;
  for (UINT i = 0; i < WAKEUP_LIMIT; ++i) {
    TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(TMO_POL));
  }
  TEST_ASSERT_EQUAL(E_TMOUT, tk_slp_tsk(TMO_POL));
  test_checkpoint();
}

static void semaphore_waiting_wakeup_child(void *arg) {
  (void)arg;
  TEST_ASSERT_EQUAL(E_OK, tk_wai_sem(s_wakeup_gate, 1, TMO_FEVR));
  consume_wakeup_limit(NULL);
}

static ER queue_wakeup_limit(ID child) {
  for (UINT i = 0; i < WAKEUP_LIMIT; ++i) {
    TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(child));
  }
  return tk_wup_tsk(child);
}

static void ready_task_refuses_a_wakeup_past_the_limit(void) {
  ID child = test_spawn(consume_wakeup_limit, LATE_CHILD_PRIORITY, 0);
  ER overflow = queue_wakeup_limit(child);
  test_join_expect(1);
  TEST_ASSERT_EQUAL(E_QOVR, overflow);
}

static void semaphore_waiter_refuses_a_wakeup_past_the_limit(void) {
  s_wakeup_gate =
      tk_cre_sem(&(T_CSEM){.sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
  TEST_ASSERT_GREATER_THAN(0, s_wakeup_gate);
  ID child = test_spawn(semaphore_waiting_wakeup_child, CHILD_PRIORITY, 0);
  ER overflow = queue_wakeup_limit(child);
  // Release the child before asserting, so a failed check cannot strand it.
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(s_wakeup_gate, 1));
  test_join_expect(1);
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(s_wakeup_gate));
  TEST_ASSERT_EQUAL(E_QOVR, overflow);
}

// A wake-up request queued during a semaphore wait outlives tk_rel_wai(): the
// released child uses it up with its next sleep, and only that one.
static void released_semaphore_waiter(void *arg) {
  (void)arg;
  TEST_ASSERT_EQUAL(E_RLWAI, tk_wai_sem(s_wakeup_gate, 1, TMO_FEVR));
  TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(TMO_POL));
  TEST_ASSERT_EQUAL(E_TMOUT, tk_slp_tsk(TMO_POL));
  test_checkpoint();
}

static void release_wait_keeps_a_queued_wakeup(void) {
  s_wakeup_gate =
      tk_cre_sem(&(T_CSEM){.sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
  TEST_ASSERT_GREATER_THAN(0, s_wakeup_gate);
  ID child = test_spawn(released_semaphore_waiter, CHILD_PRIORITY, 0);
  ER woken = tk_wup_tsk(child);
  ER released = tk_rel_wai(child);
  // Delete before asserting: if the release failed, E_DLT frees the child.
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(s_wakeup_gate));
  test_join_expect(1);
  TEST_ASSERT_EQUAL(E_OK, woken);
  TEST_ASSERT_EQUAL(E_OK, released);
}

// A task that wakes itself is refused and queues nothing.
static void task_cannot_wake_itself(void) {
  UINT intsts;
  DI(intsts);
  ID self = current->tskid;
  EI(intsts);
  TEST_ASSERT_EQUAL(E_OBJ, tk_wup_tsk(self));
  TEST_ASSERT_EQUAL(E_TMOUT, tk_slp_tsk(TMO_POL));
}

// A timer interrupt held back for ten periods still counts all ten: the child's
// 100 ms sleep (11 ticks) ends about 105 ms after it began, not 195 ms as when
// each interrupt counted one tick. Unlike the other cases this one has an upper
// bound too, since both results are about 45 ms from it.
enum { HELD_BACK_MS = 100 };
static UW s_sleep_start;
static UW s_slept;

static void held_back_sleeper(void *arg) {
  (void)arg;
  TEST_ASSERT_EQUAL(E_TMOUT, tk_slp_tsk(100));
  s_slept = test_mtime() - s_sleep_start;
  test_checkpoint();
}

static void a_held_back_interrupt_counts_every_period(void) {
  // Taken here: test_mtime_mid_tick() delays, and the child must sleep at once.
  s_sleep_start = test_mtime_mid_tick();
  test_spawn(held_back_sleeper, CHILD_PRIORITY, 0);
  UINT intsts;
  DI(intsts);
  while ((UW)(test_mtime() - s_sleep_start) <
         HELD_BACK_MS * TEST_MTIME_PER_MS) {
  }
  EI(intsts);
  test_join_expect(1);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(100 * TEST_MTIME_PER_MS, s_slept);
  TEST_ASSERT_LESS_THAN_UINT32(150 * TEST_MTIME_PER_MS, s_slept);
}

static const test_case_t s_cases[] = {
    {"sleep_times_out", sleep_times_out},
    {"release_wait", release_wait},
    {"wakeup_multiple", wakeup_multiple},
    {"wakeup_queued_before_sleep", wakeup_queued_before_sleep},
    {"delay_lasts_long_enough", delay_lasts_long_enough},
    {"longest_sleep_counts_its_ticks", longest_sleep_counts_its_ticks},
    {"ready_task_refuses_a_wakeup_past_the_limit",
     ready_task_refuses_a_wakeup_past_the_limit},
    {"semaphore_waiter_refuses_a_wakeup_past_the_limit",
     semaphore_waiter_refuses_a_wakeup_past_the_limit},
    {"release_wait_keeps_a_queued_wakeup", release_wait_keeps_a_queued_wakeup},
    {"task_cannot_wake_itself", task_cannot_wake_itself},
    {"a_held_back_interrupt_counts_every_period",
     a_held_back_interrupt_counts_every_period},
};

TEST_SUITE_MAIN("task-timer", s_cases, 5000)
