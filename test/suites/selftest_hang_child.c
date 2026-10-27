/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Run by ctest as an expected failure. A child that spins forever after its
// case has returned must time out the case and abort the suite, and the runner
// must keep polling although a task spins: it runs above the spinner.

#include <tk/tkernel.h>
#include <unity.h>

#include "../runner.h"

static void pass_case(void) { TEST_ASSERT_EQUAL(1, 1); }

static void spinning_child(void *arg) {
  (void)arg;
  volatile unsigned int count = 0;
  for (;;) {
    count++;
  }
}

static void hang_child_case(void) {
  test_spawn(spinning_child, TEST_CASE_PRIORITY + 1, 0);
}

static void marker(void) { TEST_ASSERT_EQUAL(1, 1); }

static const test_case_t s_cases[] = {
    {"pass_case", pass_case},
    {"hang_child_case", hang_child_case},
    {"marker", marker},
};

TEST_SUITE_MAIN("selftest-hang-child", s_cases, 1000)
