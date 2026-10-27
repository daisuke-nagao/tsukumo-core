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

#include "../runner.h"

static volatile int g_child_after_fail;

static void failing_child(void *arg) {
  (void)arg;
  TEST_ASSERT_EQUAL(0, 1);
  g_child_after_fail++;
}

static void child_fail_case(void) {
  test_spawn(failing_child, 2, 0);
  test_join();
}

static void child_abort_effective(void) {
  TEST_ASSERT_EQUAL(0, g_child_after_fail);
}

static void pass_case(void) { TEST_ASSERT_EQUAL(1, 1); }

static const test_case_t s_cases[] = {
    {"child_fail_case", child_fail_case},
    {"child_abort_effective", child_abort_effective},
    {"pass_case", pass_case},
};

TEST_SUITE_MAIN("selftest-child-fail", s_cases, 5000)
