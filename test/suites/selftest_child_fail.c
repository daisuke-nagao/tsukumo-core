/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Run by ctest as an expected failure. A failed assert in a child task has to
// fail the case that spawned it, end only the child, and leave the later cases
// alone.

#include <tk/tkernel.h>
#include <unity.h>

#include "runner.h"

// The children run as soon as they start: above the case, below the runner.
enum { CHILD_PRIORITY = 2 };
_Static_assert(TEST_RUNNER_PRIORITY < CHILD_PRIORITY &&
                   CHILD_PRIORITY < TEST_CASE_PRIORITY,
               "a child must outrank the case and not the runner");

static volatile int g_child_after_fail;

static void failing_child(void *arg) {
  (void)arg;
  TEST_ASSERT_EQUAL(0, 1);
  g_child_after_fail++;
}

static void child_fail_case(void) {
  test_spawn(failing_child, CHILD_PRIORITY, 0);
  test_join();
}

static void child_abort_effective(void) {
  TEST_ASSERT_EQUAL(0, g_child_after_fail);
}

static void pass_case(void) { TEST_ASSERT_EQUAL(1, 1); }

static const test_case_t s_cases[] = {
    CASE(child_fail_case),
    CASE(child_abort_effective),
    CASE(pass_case),
};

TEST_SUITE_MAIN("selftest-child-fail", s_cases, 5000)
