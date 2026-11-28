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

#include "fakes.h"
#include "timer.h"

static void check_compare(UD expected) {
  TEST_ASSERT_EQUAL_UINT64(expected,
                           (host_last_write(HOST_MTIMECMP_HIGH) << 32) |
                               host_last_write(HOST_MTIMECMP_LOW));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT(3, host_log_count);
  const host_access_t *writes = &host_log[host_log_count - 3];
  const INT ports[] = {HOST_MTIMECMP_LOW, HOST_MTIMECMP_HIGH,
                       HOST_MTIMECMP_LOW};
  const UW values[] = {0xffffffffu, (UW)(expected >> 32), (UW)expected};
  for (UINT i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(writes[i].is_write);
    TEST_ASSERT_EQUAL_INT(ports[i], writes[i].port);
    TEST_ASSERT_EQUAL_UINT(4, writes[i].width);
    TEST_ASSERT_EQUAL_UINT32(values[i], writes[i].value);
  }
}

static void check_mtime_read(UD expected) {
  const INT ports[] = {HOST_MTIME_HIGH, HOST_MTIME_LOW, HOST_MTIME_HIGH};
  const UW values[] = {(UW)(expected >> 32), (UW)expected,
                       (UW)(expected >> 32)};
  UINT reads = 0;
  for (UINT i = 0; i < host_log_count; ++i) {
    const host_access_t *access = &host_log[i];
    if (access->port == HOST_MTIME_HIGH || access->port == HOST_MTIME_LOW) {
      TEST_ASSERT_FALSE(access->is_write);
      TEST_ASSERT_EQUAL_UINT(4, access->width);
      if (reads < HOST_COUNT(ports)) {
        TEST_ASSERT_EQUAL_INT(ports[reads], access->port);
        TEST_ASSERT_EQUAL_UINT32(values[reads], access->value);
      }
      ++reads;
    }
  }
  TEST_ASSERT_EQUAL_UINT(HOST_COUNT(ports), reads);
}

// At boot the first interrupt is set one tick ahead of the machine time.
static void test_timer_arms_compare_one_tick_ahead_at_boot(void) {
  host_log_count = 0; // setUp() has already booted; boot again, with a log.
  host_mtime = 3 * HOST_TICK;
  tkmc_start_timer();
  TEST_ASSERT_EQUAL_UINT(2, host_write_count(HOST_MTIMECMP_LOW));
  TEST_ASSERT_EQUAL_UINT(1, host_write_count(HOST_MTIMECMP_HIGH));
  check_compare(4 * HOST_TICK);
}

// After a tick the next interrupt is one tick away.
static void test_timer_interrupt_sets_next_compare_ahead_of_now(void) {
  host_tick(1);
  check_compare(2 * HOST_TICK);
  host_tick(1);
  check_compare(3 * HOST_TICK);
}

// If the handler runs late, past several ticks, the compare value still ends up
// ahead of the machine time, on the tick grid.
static void test_timer_late_interrupt_skips_to_next_tick_ahead(void) {
  host_mtime = 5 * HOST_TICK + 5000;
  tkmc_timer_handler();
  check_compare(6 * HOST_TICK);
}

static void test_timer_boot_replaces_both_compare_words(void) {
  out_w(HOST_MTIMECMP_HIGH, 0xffffffffu);
  out_w(HOST_MTIMECMP_LOW, 0xffffffffu);
  host_mtime = ((UD)7 << 32) + 12345;
  host_log_count = 0;
  tkmc_start_timer();
  check_compare(host_mtime + HOST_TICK);
}

static void test_timer_compare_crosses_low_word_boundary(void) {
  const UD boundary = (UD)8 << 32;
  const UD starts[] = {boundary - HOST_TICK - 1, boundary - HOST_TICK,
                       boundary - 1, boundary, boundary + 1};
  for (UINT i = 0; i < HOST_COUNT(starts); ++i) {
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

static void test_timer_boot_retries_read_that_carries_into_high_word(void) {
  host_mtime = ((UD)7 << 32) | 0xfffffff0u;
  host_mtime_advance_after_low_read = 0x30;
  host_log_count = 0;
  tkmc_start_timer();
  check_compare(host_mtime + HOST_TICK);
}

static void test_timer_handler_retries_read_that_carries_into_high_word(void) {
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

static void test_timer_handler_reads_mtime_once_for_elapsed_periods(void) {
  const UINT periods[] = {1, 64};
  for (UINT i = 0; i < HOST_COUNT(periods); ++i) {
    host_mtime = 0;
    tkmc_start_timer();
    host_mtime = periods[i] * (UD)HOST_TICK;
    host_log_count = 0;

    tkmc_timer_handler();

    check_mtime_read(host_mtime);
    check_compare((periods[i] + 1) * (UD)HOST_TICK);
  }
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

static void
test_timer_handler_uses_sample_before_same_word_tick_boundary(void) {
  const UD boundary = HOST_TICK;
  host_mtime = 0;
  tkmc_start_timer();
  ID id = host_sleep_for(3);
  host_run_idle_task();

  host_mtime = 2 * boundary - 16;
  host_mtime_advance_after_low_read = 32;
  host_log_count = 0;
  tkmc_timer_handler();
  check_wait(id, 2);
  TEST_ASSERT_EQUAL_UINT64(2 * boundary,
                           (host_last_write(HOST_MTIMECMP_HIGH) << 32) |
                               host_last_write(HOST_MTIMECMP_LOW));
  check_mtime_read(2 * boundary - 16);
  TEST_ASSERT_EQUAL_UINT64(2 * boundary + 16, host_mtime);

  host_log_count = 0;
  tkmc_timer_handler();
  check_wait(id, 1);
  check_compare(3 * boundary);
  check_mtime_read(2 * boundary + 16);
}

// The handler counts the periods that have passed since the last compare value,
// not its own calls: a late interrupt takes off every period it missed, and a
// call that comes before the compare value (MTIP is cleared late after a
// compare write) changes nothing.
static void test_timer_interrupt_counts_every_period_that_has_passed(void) {
  // An 11 tick wait, then one interrupt after `periods`; 0 left is a time-out.
  static const struct {
    UINT periods;
    UINT left;
  } rows[] = {{0, 11}, {1, 10}, {10, 1}, {11, 0}, {14, 0}};
  // From the start of time, and across the low-word wrap of the machine timer.
  const UD starts[] = {0, ((UD)8 << 32) - 5 * HOST_TICK};
  for (UINT s = 0; s < HOST_COUNT(starts); ++s) {
    for (UINT r = 0; r < HOST_COUNT(rows); ++r) {
      host_reset();
      host_mtime = starts[s];
      tkmc_start_timer();
      ID id = host_sleep_for(11);
      host_run_idle_task();
      host_log_count = 0;

      host_mtime = starts[s] + rows[r].periods * HOST_TICK;
      tkmc_timer_handler();
      check_wait(id, rows[r].left);
      if (rows[r].periods == 0) {
        TEST_ASSERT_EQUAL_UINT(0, host_write_count(HOST_MTIMECMP_LOW));
      } else {
        // A time-out also requests a switch, so the compare writes are not
        // the last accesses that check_compare() looks for.
        TEST_ASSERT_EQUAL_UINT64(starts[s] + (rows[r].periods + 1) * HOST_TICK,
                                 (host_last_write(HOST_MTIMECMP_HIGH) << 32) |
                                     host_last_write(HOST_MTIMECMP_LOW));
      }
    }
  }
}

// A second interrupt at the same time, as when MTIP is cleared late, finds no
// period that has passed and changes nothing, the compare value included.
static void test_timer_repeated_interrupt_changes_nothing(void) {
  ID id = host_sleep_for(3);
  host_run_idle_task();
  host_tick(1);
  check_wait(id, 2);

  host_log_count = 0; // before the snapshot, which counts writes in the log
  host_snapshot_t before;
  host_take_snapshot(&before);
  tkmc_timer_handler();
  host_assert_unchanged(&before, "a repeated timer interrupt");
  TEST_ASSERT_EQUAL_UINT(0, host_write_count(HOST_MTIMECMP_LOW));
  TEST_ASSERT_EQUAL_UINT(0, host_write_count(HOST_MTIMECMP_HIGH));
}

// One late interrupt ends only the waits whose time has come, and takes the
// same periods off the others.
static void test_timer_queue_late_interrupt_ends_only_waits_it_reached(void) {
  ID short_wait = host_sleep_for(3);
  ID exact = host_sleep_for(10);
  ID long_wait = host_sleep_for(12);
  host_run_idle_task();

  host_mtime = 10 * HOST_TICK;
  tkmc_timer_handler();
  check_wait(short_wait, 0);
  check_wait(exact, 0);
  check_wait(long_wait, 2);
}

// Periods that passed before a task began to wait, while their interrupt was
// still pending, are not taken off its wait, but are taken off the tasks that
// already waited.
static void
test_timer_queue_new_wait_does_not_count_periods_before_it_began(void) {
  ID earlier = host_sleep_for(10);
  // The compare value is one tick; at four ticks four periods are pending.
  host_mtime = 4 * HOST_TICK;
  ID late = host_sleep_for(3);
  host_run_idle_task();

  tkmc_timer_handler();
  check_wait(late, 3);
  check_wait(earlier, 6);
  host_tick(2);
  check_wait(late, 1);
  host_tick(1);
  check_wait(late, 0);
  check_wait(earlier, 3);
}

// Several tasks can be on the timer queue at once, and each one times out on
// its own tick: one that expires does not hold up or hurry up the others, and
// two that expire together are both woken.
static void test_timer_queue_times_out_each_task_on_its_own_tick(void) {
  ID early = host_make_task(HOST_HIGH_PRIORITY);
  ID late = host_make_task(HOST_HIGH_PRIORITY);
  ID twin = host_make_task(HOST_HIGH_PRIORITY);
  ID semid = host_create_semaphore();

  host_set_current(early);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 10)); // 2 ticks
  host_set_current(late);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 30)); // 4 ticks
  host_set_current(twin);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 10)); // 2 ticks
  host_check_kernel_state();

  host_tick(1);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, host_tcb(early)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, host_tcb(late)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, host_tcb(twin)->tskstat);
  host_tick(1);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(early)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(twin)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, host_tcb(late)->tskstat);
  host_check_kernel_state();
  host_tick(1);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, host_tcb(late)->tskstat);
  host_tick(1);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(late)->tskstat);
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  host_check_kernel_state();
}

// A timed wait records what the task waits for (sleeping, delay, semaphore).
// The kernel tells the waits apart by this field; what each kind lets wake the
// task is not tested here.
static void test_timer_queue_timed_waits_record_what_task_waits_for(void) {
  ID sleeper = host_make_task(HOST_HIGH_PRIORITY);
  ID delayer = host_make_task(HOST_HIGH_PRIORITY);
  ID waiter = host_make_task(HOST_HIGH_PRIORITY);
  ID semid = host_create_semaphore();

  host_set_current(sleeper);
  HOST_EXPECT_WAIT(tk_slp_tsk(50));
  host_set_current(delayer);
  HOST_EXPECT_WAIT(tk_dly_tsk(50));
  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 50));

  TEST_ASSERT_EQUAL_UINT(TTW_SLP, host_tcb(sleeper)->tskwait);
  TEST_ASSERT_EQUAL_UINT(TTW_DLY, host_tcb(delayer)->tskwait);
  TEST_ASSERT_EQUAL_UINT(TTW_SEM, host_tcb(waiter)->tskwait);
  host_check_kernel_state();
}

void run_timer_tests(void) {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_timer_arms_compare_one_tick_ahead_at_boot);
  RUN_TEST(test_timer_interrupt_sets_next_compare_ahead_of_now);
  RUN_TEST(test_timer_late_interrupt_skips_to_next_tick_ahead);
  RUN_TEST(test_timer_boot_replaces_both_compare_words);
  RUN_TEST(test_timer_compare_crosses_low_word_boundary);
  RUN_TEST(test_timer_boot_retries_read_that_carries_into_high_word);
  RUN_TEST(test_timer_handler_retries_read_that_carries_into_high_word);
  RUN_TEST(test_timer_handler_reads_mtime_once_for_elapsed_periods);
  RUN_TEST(test_timer_handler_uses_sample_before_same_word_tick_boundary);
  RUN_TEST(test_timer_interrupt_counts_every_period_that_has_passed);
  RUN_TEST(test_timer_repeated_interrupt_changes_nothing);
  RUN_TEST(test_timer_queue_late_interrupt_ends_only_waits_it_reached);
  RUN_TEST(test_timer_queue_new_wait_does_not_count_periods_before_it_began);
  RUN_TEST(test_timer_queue_times_out_each_task_on_its_own_tick);
  RUN_TEST(test_timer_queue_timed_waits_record_what_task_waits_for);
}
