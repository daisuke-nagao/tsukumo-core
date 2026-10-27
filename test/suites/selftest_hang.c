/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Run by ctest as an expected failure. A case that never ends has to be
// reported as a timeout and has to abort the suite: marker must never run.

#include <tk/tkernel.h>
#include <unity.h>

#include "../runner.h"

static void pass_case(void) { TEST_ASSERT_EQUAL(1, 1); }

static void hang_case(void) {
  for (;;) {
    tk_dly_tsk(10);
  }
}

static void marker(void) { TEST_ASSERT_EQUAL(1, 1); }

static const test_case_t s_cases[] = {
    {"pass_case", pass_case},
    {"hang_case", hang_case},
    {"marker", marker},
};

// A short limit keeps the expected timeout from slowing the ctest run.
TEST_SUITE_MAIN("selftest-hang", s_cases, 1000)
