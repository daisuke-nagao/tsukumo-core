/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "runner.h"
#include "timer.h"

enum { MTIME = CLINT_BASE_ADDRESS + CLINT_MTIME_OFFSET };

static UD machine_time(void) {
  UW high, low, next_high;
  do {
    high = in_w(MTIME + 4);
    low = in_w(MTIME);
    next_high = in_w(MTIME + 4);
  } while (high != next_high);
  return ((UD)high << 32) | low;
}

static void delay_across_low_word_wrap(void) {
  UD start = machine_time();
  TEST_ASSERT_EQUAL_UINT32(1, start >> 32);
  TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(300));
  UD end = machine_time();
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2, end >> 32);
  TEST_ASSERT_TRUE(end - start >= (UD)300 * TEST_MTIME_PER_MS);
}

static const test_case_t s_cases[] = {
    {"delay_across_low_word_wrap", delay_across_low_word_wrap},
};

void usermain(int a0) {
  (void)a0;
  UINT intsts;
  DI(intsts);
  // Move time before starting the runner, so its watchdog sees no clock jump.
  out_w(MTIME, 0);
  out_w(MTIME + 4, 1);
  out_w(MTIME, (UW)0 - 100 * TEST_MTIME_PER_MS);
  tkmc_start_timer();
  EI(intsts);
  test_start_runner("timer-wrap", s_cases, sizeof(s_cases) / sizeof(s_cases[0]),
                    5000);
}
