/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// The timer interrupt handler keeps the machine timer's compare register ahead
// of the machine time. If it did not, the interrupt would stay pending and fire
// again at once.

#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "fakes.h"
#include "timer.h"

extern void tkmc_timer_handler(void);

enum { MTIMECMP_LOW = CLINT_BASE_ADDRESS + CLINT_MTIMECMP_OFFSET };

// At boot the first interrupt is set one tick ahead of the machine time.
void test_the_kernel_arms_the_timer_one_tick_ahead_at_boot(void) {
  host_log_count = 0; // setUp() has already booted; boot again, with a log.
  host_mtime = 3 * HOST_TICK;
  tkmc_start_timer();
  TEST_ASSERT_EQUAL_UINT(1, host_write_count(MTIMECMP_LOW));
  TEST_ASSERT_EQUAL_UINT(4 * HOST_TICK, host_last_write(MTIMECMP_LOW));
}

// After a tick the next interrupt is one tick away.
void test_timer_interrupt_sets_the_next_compare_value_ahead_of_now(void) {
  host_tick(1);
  TEST_ASSERT_EQUAL_UINT(2 * HOST_TICK, host_last_write(MTIMECMP_LOW));
  host_tick(1);
  TEST_ASSERT_EQUAL_UINT(3 * HOST_TICK, host_last_write(MTIMECMP_LOW));
}

// If the handler runs late, past several ticks, the compare value still ends up
// ahead of the machine time, on the tick grid.
void test_a_late_timer_interrupt_skips_to_the_next_tick_ahead(void) {
  host_mtime = 5 * HOST_TICK + 5000;
  tkmc_timer_handler();
  TEST_ASSERT_EQUAL_UINT(6 * HOST_TICK, host_last_write(MTIMECMP_LOW));
}
