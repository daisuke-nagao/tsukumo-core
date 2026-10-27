/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "../memset_matrix.h"

// tkmc_memset() builds a word from the fill value and writes it 32 bytes at a
// time. The shift that builds the word is undefined behaviour for values of
// 0x80 and up, and only UBSan sees it (the bytes come out right), so the test
// runs on the PC, where UBSan is on and stops at the first finding.
void test_memset_fills_exactly_the_requested_range(void) {
  memset_matrix_check();
}
