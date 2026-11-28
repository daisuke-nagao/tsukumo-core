/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Run by ctest as an expected failure. A failed assert has to end only its own
// case: fail_case counts what runs after its failed assert, and the next case
// fails if that counter moved, which shows the abort took effect.

#include <tk/tkernel.h>
#include <unity.h>

#include "runner.h"

static volatile int g_after_fail;

static void fail_case(void) {
  TEST_ASSERT_EQUAL(0, 1);
  g_after_fail++;
}

static void abort_effective(void) { TEST_ASSERT_EQUAL(0, g_after_fail); }

static void pass_case(void) { TEST_ASSERT_EQUAL(1, 1); }

static const test_case_t s_cases[] = {
    CASE(fail_case),
    CASE(abort_effective),
    CASE(pass_case),
};

TEST_SUITE_MAIN("selftest-fail", s_cases, 5000)
