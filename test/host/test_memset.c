/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <unity.h>

#include "../memset_matrix.h"

// tkmc_memset() builds a word from the fill value and writes it 32 bytes at a
// time. Building the word by shifting the value was undefined behaviour for
// values of 0x80 and up, and kernel/memset.c now multiplies instead. A return
// to the shift would leave the bytes right, and only UBSan would see it, so the
// test runs on the PC, where UBSan is on and stops at the first finding.
static void test_tkmc_memset_fills_exactly_requested_range(void) {
  memset_matrix_check();
}

void run_memset_tests(void) {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_tkmc_memset_fills_exactly_requested_range);
}
