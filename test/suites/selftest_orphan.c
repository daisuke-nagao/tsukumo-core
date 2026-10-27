/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Run by ctest as an expected failure. A child that outlives its case and then
// fails an assert must fail that case, not the next one: the runner waits for
// every child the case spawned, even if the case does not call test_join().

#include <tk/tkernel.h>
#include <unity.h>

#include "../runner.h"

static void late_failing_child(void *arg) {
  (void)arg;
  // Longer than the runner's polling interval, so the case really ends first.
  tk_dly_tsk(100);
  TEST_ASSERT_EQUAL(0, 1);
}

static void orphan_case1(void) { test_spawn(late_failing_child, 2, 0); }

static void orphan_case2(void) { TEST_ASSERT_EQUAL(1, 1); }

static const test_case_t s_cases[] = {
    {"orphan_case1", orphan_case1},
    {"orphan_case2", orphan_case2},
};

TEST_SUITE_MAIN("selftest-orphan", s_cases, 5000)
