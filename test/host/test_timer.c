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
#include "task.h"
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

// The handler counts the periods that have passed since the last compare value,
// not its own calls: a late interrupt takes off every period it missed, and a
// call that comes before the compare value (MTIP is cleared late after a
// compare write) changes nothing.

enum { WAITER_PRIORITY = 4, IDLE_PRIORITY = 5 };

// A task that sleeps for ticks (2 or more), counted as tkmc_ms_to_ticks() does.
static ID sleep_for(UINT ticks) {
  ID id = host_make_task(WAITER_PRIORITY);
  host_set_current(id);
  HOST_EXPECT_WAIT(tk_slp_tsk((TMO)((ticks - 1) * 10)));
  return id;
}

// Makes a task that never waits the running one, so the queues are complete.
static void run_idle_task(void) {
  host_set_current(host_make_task(IDLE_PRIORITY));
}

static void check_wait(ID id, UINT left) {
  const TCB *tcb = &tkmc_tcbs[id - 1];
  if (left == 0) {
    TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
    TEST_ASSERT_EQUAL(E_TMOUT, tcb->wupcause);
  } else {
    TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb->tskstat);
    TEST_ASSERT_EQUAL_UINT(left, tcb->delay_ticks);
  }
  host_check_kernel_queues();
}

void test_a_timer_interrupt_counts_every_period_that_has_passed(void) {
  // An 11 tick wait, then one interrupt after `periods`; 0 left is a time-out.
  static const struct {
    UINT periods;
    UINT left;
  } rows[] = {{0, 11}, {1, 10}, {10, 1}, {11, 0}, {14, 0}};
  // From the start of time, and across the low-word wrap of the machine timer.
  const UD starts[] = {0, ((UD)8 << 32) - 5 * HOST_TICK};
  for (UINT s = 0; s < sizeof(starts) / sizeof(starts[0]); ++s) {
    for (UINT r = 0; r < sizeof(rows) / sizeof(rows[0]); ++r) {
      host_reset();
      host_mtime = starts[s];
      tkmc_start_timer();
      ID id = sleep_for(11);
      run_idle_task();
      host_log_count = 0;

      host_mtime = starts[s] + rows[r].periods * HOST_TICK;
      tkmc_timer_handler();
      check_wait(id, rows[r].left);
      if (rows[r].periods == 0) {
        TEST_ASSERT_EQUAL_UINT(0, host_write_count(MTIMECMP_LOW));
      } else {
        // A time-out also requests a switch, so the compare writes are not
        // the last accesses that check_compare() looks for.
        TEST_ASSERT_EQUAL_UINT64(starts[s] + (rows[r].periods + 1) * HOST_TICK,
                                 (host_last_write(MTIMECMP_HIGH) << 32) |
                                     host_last_write(MTIMECMP_LOW));
      }
    }
  }
}

// A second interrupt at the same time, as when MTIP is cleared late, finds no
// period that has passed and changes nothing, the compare value included.
void test_a_repeated_timer_interrupt_changes_nothing(void) {
  ID id = sleep_for(3);
  run_idle_task();
  host_tick(1);
  check_wait(id, 2);

  host_log_count = 0; // before the snapshot, which counts writes in the log
  host_snapshot_t before;
  host_take_snapshot(&before);
  tkmc_timer_handler();
  host_assert_unchanged(&before, "a repeated timer interrupt");
  TEST_ASSERT_EQUAL_UINT(0, host_write_count(MTIMECMP_LOW));
  TEST_ASSERT_EQUAL_UINT(0, host_write_count(MTIMECMP_HIGH));
}

// One late interrupt ends only the waits whose time has come, and takes the
// same periods off the others.
void test_a_late_timer_interrupt_ends_only_the_waits_it_reached(void) {
  ID short_wait = sleep_for(3);
  ID exact = sleep_for(10);
  ID long_wait = sleep_for(12);
  run_idle_task();

  host_mtime = 10 * HOST_TICK;
  tkmc_timer_handler();
  check_wait(short_wait, 0);
  check_wait(exact, 0);
  check_wait(long_wait, 2);
}

// Periods that passed before a task began to wait, while their interrupt was
// still pending, are not taken off its wait, but are taken off the tasks that
// already waited.
void test_a_new_wait_does_not_count_periods_from_before_it_began(void) {
  ID earlier = sleep_for(10);
  // The compare value is one tick; at four ticks four periods are pending.
  host_mtime = 4 * HOST_TICK;
  ID late = sleep_for(3);
  run_idle_task();

  tkmc_timer_handler();
  check_wait(late, 3);
  check_wait(earlier, 6);
  host_tick(2);
  check_wait(late, 1);
  host_tick(1);
  check_wait(late, 0);
  check_wait(earlier, 3);
}
