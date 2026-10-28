/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Tests of task sleeping, waking, releasing a wait and delaying (tk_slp_tsk,
// tk_wup_tsk, tk_rel_wai, tk_dly_tsk).
//
// Each case below spawns a child at priority 2 while the case itself runs at
// priority 4. The child therefore runs first and is already blocked in its
// first sleep when test_spawn() returns, so the case can wake or release it at
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

static const test_case_t s_cases[] = {
    {"sleep_times_out", sleep_times_out},
    {"release_wait", release_wait},
    {"wakeup_multiple", wakeup_multiple},
    {"wakeup_queued_before_sleep", wakeup_queued_before_sleep},
    {"delay_lasts_long_enough", delay_lasts_long_enough},
    {"longest_sleep_counts_its_ticks", longest_sleep_counts_its_ticks},
};

TEST_SUITE_MAIN("task-timer", s_cases, 5000)
