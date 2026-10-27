/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Run by ctest as a plain suite: it must pass and the guest must exit with
// status 0, which no failing selftest can show. Each case asserts after
// test_join(), so a test_join() that returns early, or a runner that loses a
// child, leaves a flag unset and fails the case. The runner would wait for the
// children anyway, so only an assert before the case ends can tell.

#include <tk/tkernel.h>
#include <unity.h>

#include "../runner.h"

typedef struct {
  TMO delay_ms;
  volatile int done;
} job_t;

static void finish_after_delay(void *arg) {
  job_t *job = arg;
  tk_dly_tsk(job->delay_ms);
  job->done = 1;
}

static job_t g_single = {50, 0};
static job_t g_long_first = {150, 0};
static job_t g_short_second = {50, 0};
static job_t g_short_first = {50, 0};
static job_t g_long_second = {150, 0};

static void join_waits_for_child(void) {
  test_spawn(finish_after_delay, 2, &g_single);
  test_join();
  TEST_ASSERT_EQUAL(1, g_single.done);
}

// The child that ends last is spawned first: a runner that tracks only the
// last child would stop waiting too early.
static void join_long_child_first(void) {
  test_spawn(finish_after_delay, 2, &g_long_first);
  test_spawn(finish_after_delay, 2, &g_short_second);
  test_join();
  TEST_ASSERT_EQUAL(1, g_long_first.done);
  TEST_ASSERT_EQUAL(1, g_short_second.done);
}

// The child that ends last is spawned last: a runner that skips the last child
// would stop waiting too early.
static void join_long_child_last(void) {
  test_spawn(finish_after_delay, 2, &g_short_first);
  test_spawn(finish_after_delay, 2, &g_long_second);
  test_join();
  TEST_ASSERT_EQUAL(1, g_short_first.done);
  TEST_ASSERT_EQUAL(1, g_long_second.done);
}

static const test_case_t s_cases[] = {
    {"join_waits_for_child", join_waits_for_child},
    {"join_long_child_first", join_long_child_first},
    {"join_long_child_last", join_long_child_last},
};

TEST_SUITE_MAIN("selftest-pass", s_cases, 5000)
