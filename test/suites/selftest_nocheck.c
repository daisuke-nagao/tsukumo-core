/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Run by ctest as an expected failure. A child that ends without reaching its
// checkpoint must fail test_join_expect(): without that check a child that
// never asserted anything would let its case pass.

#include <tk/tkernel.h>
#include <unity.h>

#include "../runner.h"

static void silent_child(void *arg) { (void)arg; }

static void no_checkpoint(void) {
  test_spawn(silent_child, 2, 0);
  test_join_expect(1);
}

static void pass_case(void) { TEST_ASSERT_EQUAL(1, 1); }

static const test_case_t s_cases[] = {
    {"no_checkpoint", no_checkpoint},
    {"pass_case", pass_case},
};

TEST_SUITE_MAIN("selftest-nocheck", s_cases, 5000)
