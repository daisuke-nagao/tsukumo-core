/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// The timer interrupt handler keeps the machine timer's compare register ahead
// of the machine time. If it did not, the interrupt would stay pending and fire
// again at once.

#include <sys/ioport.h>
#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "fakes.h"
#include "timer.h"

extern void tkmc_timer_handler(void);

enum {
  MTIMECMP_LOW = CLINT_BASE_ADDRESS + CLINT_MTIMECMP_OFFSET,
  MTIMECMP_HIGH = MTIMECMP_LOW + 4
};

static void check_compare(UD expected) {
  TEST_ASSERT_EQUAL_UINT64(expected, (host_last_write(MTIMECMP_HIGH) << 32) |
                                         host_last_write(MTIMECMP_LOW));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT(3, host_log_count);
  const host_access_t *writes = &host_log[host_log_count - 3];
  const INT ports[] = {MTIMECMP_LOW, MTIMECMP_HIGH, MTIMECMP_LOW};
  const UW values[] = {0xffffffffu, (UW)(expected >> 32), (UW)expected};
  for (UINT i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(writes[i].is_write);
    TEST_ASSERT_EQUAL_INT(ports[i], writes[i].port);
    TEST_ASSERT_EQUAL_UINT(4, writes[i].width);
    TEST_ASSERT_EQUAL_UINT32(values[i], writes[i].value);
  }
}

// At boot the first interrupt is set one tick ahead of the machine time.
void test_the_kernel_arms_the_timer_one_tick_ahead_at_boot(void) {
  host_log_count = 0; // setUp() has already booted; boot again, with a log.
  host_mtime = 3 * HOST_TICK;
  tkmc_start_timer();
  TEST_ASSERT_EQUAL_UINT(2, host_write_count(MTIMECMP_LOW));
  TEST_ASSERT_EQUAL_UINT(1, host_write_count(MTIMECMP_HIGH));
  check_compare(4 * HOST_TICK);
}

// After a tick the next interrupt is one tick away.
void test_timer_interrupt_sets_the_next_compare_value_ahead_of_now(void) {
  host_tick(1);
  check_compare(2 * HOST_TICK);
  host_tick(1);
  check_compare(3 * HOST_TICK);
}

// If the handler runs late, past several ticks, the compare value still ends up
// ahead of the machine time, on the tick grid.
void test_a_late_timer_interrupt_skips_to_the_next_tick_ahead(void) {
  host_mtime = 5 * HOST_TICK + 5000;
  tkmc_timer_handler();
  check_compare(6 * HOST_TICK);
}

void test_timer_boot_replaces_both_compare_words(void) {
  out_w(MTIMECMP_HIGH, 0xffffffffu);
  out_w(MTIMECMP_LOW, 0xffffffffu);
  host_mtime = ((UD)7 << 32) + 12345;
  host_log_count = 0;
  tkmc_start_timer();
  check_compare(host_mtime + HOST_TICK);
}

void test_timer_compare_crosses_the_low_word_boundary(void) {
  const UD boundary = (UD)8 << 32;
  const UD starts[] = {boundary - HOST_TICK - 1, boundary - HOST_TICK,
                       boundary - 1, boundary, boundary + 1};
  for (UINT i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
    host_mtime = starts[i];
    host_log_count = 0;
    tkmc_start_timer();
    check_compare(starts[i] + HOST_TICK);

    host_mtime = starts[i] + HOST_TICK;
    host_log_count = 0;
    tkmc_timer_handler();
    check_compare(starts[i] + 2 * HOST_TICK);
    // One elapsed period must not turn into thousands of catch-up iterations.
    TEST_ASSERT_LESS_OR_EQUAL_UINT(24, host_log_count);
  }
}

void test_timer_boot_retries_a_read_that_carries_into_the_high_word(void) {
  host_mtime = ((UD)7 << 32) | 0xfffffff0u;
  host_mtime_advance_after_low_read = 0x30;
  host_log_count = 0;
  tkmc_start_timer();
  check_compare(host_mtime + HOST_TICK);
}

void test_timer_handler_retries_a_read_that_carries_into_the_high_word(void) {
  const UD boundary = (UD)8 << 32;
  host_mtime = boundary - 2 * HOST_TICK;
  tkmc_start_timer();
  host_mtime = boundary - 16;
  host_mtime_advance_after_low_read = 0x30;
  host_log_count = 0;
  tkmc_timer_handler();
  check_compare(boundary + HOST_TICK);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(24, host_log_count);
}
