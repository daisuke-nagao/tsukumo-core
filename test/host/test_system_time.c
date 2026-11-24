/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// tk_set_utc() and tk_get_utc(): a millisecond clock that the kernel timer
// advances by 10 ms per tick. It reads 0 each time the timer starts, counts the
// ticks whose interrupt is still pending when it is read, and a set value
// counts from the moment of the set. Setting it changes no wait and no timer
// register.
//
// Values are written as (high word, low word). Time passes only where a test
// moves host_mtime: host_tick() adds one tick and runs the handler, and a plain
// host_mtime assignment leaves the interrupt pending, as a late one is. After
// boot the next tick edge is HOST_TICK.
//
// The access log of the fake machine holds 256 entries and every read of the
// machine timer is one, so tick() starts each tick with an empty log.

#include <stdio.h>

#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "context.h"
#include "fakes.h"
#include "task.h"
#include "timer.h"

enum {
  MTIMECMP_LOW = CLINT_BASE_ADDRESS + CLINT_MTIMECMP_OFFSET,
  MTIMECMP_HIGH = MTIMECMP_LOW + 4,
  HIGH_WORD_MAX = 0x7fffffff
};

#define LOW_WORD_MAX 0xffffffffu

static const SYSTIM s_sentinel = {.hi = 0x55555555, .lo = 0xaaaaaaaau};

// The task the calls come from.
static void run_a_task(void) {
  host_set_current(host_make_task(HOST_HIGH_PRIORITY));
}

static void tick(UINT ticks) {
  for (UINT i = 0; i < ticks; ++i) {
    host_log_count = 0;
    host_tick(1);
  }
}

static void set_utc(W hi, UW lo) {
  const SYSTIM time = {.hi = hi, .lo = lo};
  TEST_ASSERT_EQUAL_INT(E_OK, tk_set_utc(&time));
}

// Unity's own line number would be this helper's, so the caller's goes in.
static void check_utc(UNITY_LINE_TYPE line, W hi, UW lo) {
  SYSTIM now = s_sentinel;
  UNITY_TEST_ASSERT_EQUAL_INT(E_OK, tk_get_utc(&now), line, "tk_get_utc()");
  UNITY_TEST_ASSERT_EQUAL_HEX32((UW)hi, (UW)now.hi, line, "high word");
  UNITY_TEST_ASSERT_EQUAL_HEX32(lo, now.lo, line, "low word");
}
#define EXPECT_UTC(hi, lo) check_utc(__LINE__, (hi), (lo))

static void test_utc_clock_reads_zero_after_boot(void) {
  run_a_task();
  EXPECT_UTC(0, 0);
}

// The clock counts from the start of the timer, not from the machine time, and
// forgets earlier sets and ticks. Each case starts the timer again on a machine
// that has run for a different time, with the clock dirty, so a clock that is
// reset only at the first boot (or follows the machine time) fails.
static void test_utc_clock_resets_on_timer_restart_whatever_machine_time(void) {
  const UD machine_times[] = {0,
                              1,
                              HOST_TICK - 1,
                              0xffffffffu,
                              ((UD)1 << 32) - HOST_TICK,
                              ((UD)5 << 32) | 0x01234567u,
                              (UD)1 << 40};
  for (UINT i = 0; i < HOST_COUNT(machine_times); ++i) {
    host_reset();
    run_a_task();
    tick(1); // The set is not at tick 0, so a stale set tick shows too.
    set_utc(7, 123456);
    tick(3);
    EXPECT_UTC(7, 123486);

    host_mtime = machine_times[i];
    host_log_count = 0;
    tkmc_init_timer();
    tkmc_start_timer();
    EXPECT_UTC(0, 0);
    tick(1);
    EXPECT_UTC(0, 10);
  }
}

// Time between two tick edges is not visible: the clock moves in steps.
static void
test_tk_get_utc_returns_same_value_for_repeated_reads_within_tick(void) {
  run_a_task();
  set_utc(2, 5000);
  for (UINT i = 0; i < 10; ++i) {
    host_log_count = 0;
    host_mtime += HOST_TICK / 10 - 1;
    EXPECT_UTC(2, 5000);
    EXPECT_UTC(2, 5000);
  }
}

static void test_utc_clock_advances_ten_milliseconds_per_tick(void) {
  static const UW starts[] = {0, 1000};
  for (UINT row = 0; row < HOST_COUNT(starts); ++row) {
    host_reset();
    run_a_task();
    TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
    set_utc(0, starts[row]);
    for (UINT i = 1; i <= 100; ++i) {
      tick(1);
      EXPECT_UTC(0, starts[row] + 10 * i);
    }
  }
}

// An interrupt that comes late counts every tick it missed, once.
static void
test_utc_clock_advances_by_every_tick_late_timer_interrupt_missed(void) {
  run_a_task();
  set_utc(0, 1000);
  host_mtime = 7 * HOST_TICK + 5000;
  host_log_count = 0;
  tkmc_timer_handler();
  EXPECT_UTC(0, 1070);

  host_mtime += 3 * HOST_TICK;
  host_log_count = 0;
  tkmc_timer_handler();
  EXPECT_UTC(0, 1100);
}

static void test_tk_set_utc_keeps_millisecond_remainder(void) {
  run_a_task();
  set_utc(0, 10005);
  EXPECT_UTC(0, 10005);
  tick(1);
  EXPECT_UTC(0, 10015);
  tick(1);
  EXPECT_UTC(0, 10025);
}

static void test_tk_set_utc_moves_clock_forward_and_backward(void) {
  run_a_task();
  set_utc(1, 0);
  tick(1);
  EXPECT_UTC(1, 10);

  set_utc(0, 500);
  EXPECT_UTC(0, 500);
  tick(1);
  EXPECT_UTC(0, 510);

  set_utc(9, 9);
  EXPECT_UTC(9, 9);
  tick(1);
  EXPECT_UTC(9, 19);
}

static void test_tk_set_utc_round_trips_ends_of_utc_range(void) {
  static const SYSTIM values[] = {
      {0, 0},
      {0, 1},
      {0, LOW_WORD_MAX},
      {1, 0},
      {0x12345678, 0x9abcdef0u},
      {HIGH_WORD_MAX, 0},
      {HIGH_WORD_MAX - 1, LOW_WORD_MAX},
      {HIGH_WORD_MAX, LOW_WORD_MAX},
  };
  run_a_task();
  for (UINT i = 0; i < HOST_COUNT(values); ++i) {
    host_log_count = 0;
    set_utc(values[i].hi, values[i].lo);
    EXPECT_UTC(values[i].hi, values[i].lo);
  }
}

static void test_utc_clock_carries_low_word_into_high_word_on_tick(void) {
  run_a_task();
  set_utc(0, LOW_WORD_MAX - 5);
  tick(1);
  EXPECT_UTC(1, 4);

  set_utc(0, LOW_WORD_MAX - 9); // Lands exactly on the boundary.
  tick(1);
  EXPECT_UTC(1, 0);

  set_utc(0x12345678, LOW_WORD_MAX);
  tick(1);
  EXPECT_UTC(0x12345679, 9);
}

// The value is signed in its high word and must not be negative; the refusal
// leaves the clock alone, so it is checked with a clock that is not zero and
// has ticked since the set.
static void test_tk_set_utc_refuses_negative_high_word_and_keeps_clock(void) {
  const SYSTIM refused[] = {
      {-1, 0}, {-1, LOW_WORD_MAX}, {-2147483647 - 1, 0}, {-2147483647 - 1, 7}};
  run_a_task();
  set_utc(3, 1000);
  tick(2);
  for (UINT i = 0; i < HOST_COUNT(refused); ++i) {
    TEST_ASSERT_EQUAL_INT(E_PAR, tk_set_utc(&refused[i]));
    EXPECT_UTC(3, 1020);
  }
  tick(1);
  EXPECT_UTC(3, 1030);
}

// 2^63 - 1 ms is the end of the range. Past it the clock stays at the end,
// whether the ticks come one by one, late in one interrupt, or are only
// pending.
static void test_utc_clock_saturates_at_maximum_and_stays(void) {
  run_a_task();
  set_utc(HIGH_WORD_MAX, LOW_WORD_MAX - 10);
  tick(1); // Reaches the maximum exactly.
  EXPECT_UTC(HIGH_WORD_MAX, LOW_WORD_MAX);
  tick(1);
  EXPECT_UTC(HIGH_WORD_MAX, LOW_WORD_MAX);

  set_utc(HIGH_WORD_MAX, LOW_WORD_MAX - 5);
  tick(1); // Would pass the maximum by 5.
  EXPECT_UTC(HIGH_WORD_MAX, LOW_WORD_MAX);
  tick(3);
  EXPECT_UTC(HIGH_WORD_MAX, LOW_WORD_MAX);

  set_utc(HIGH_WORD_MAX, LOW_WORD_MAX - 20);
  host_mtime += 5 * HOST_TICK + 5000; // Five ticks pending.
  host_log_count = 0;
  EXPECT_UTC(HIGH_WORD_MAX, LOW_WORD_MAX);
  tkmc_timer_handler(); // And one late interrupt for all of them.
  EXPECT_UTC(HIGH_WORD_MAX, LOW_WORD_MAX);
}

static void test_tk_set_utc_after_saturation_restarts_clock(void) {
  run_a_task();
  set_utc(HIGH_WORD_MAX, LOW_WORD_MAX);
  tick(2);
  EXPECT_UTC(HIGH_WORD_MAX, LOW_WORD_MAX);

  set_utc(3, 3);
  EXPECT_UTC(3, 3);
  tick(1);
  EXPECT_UTC(3, 13);
}

// An interrupt that has not run yet has not been counted by the handler, but
// the time has passed: a reader sees it.
static void test_tk_get_utc_counts_ticks_whose_interrupt_is_pending(void) {
  run_a_task();
  set_utc(0, 1000);
  host_mtime = 3 * HOST_TICK + 7; // Edges 1, 2 and 3 are behind, unhandled.
  host_log_count = 0;
  EXPECT_UTC(0, 1030);
  EXPECT_UTC(0, 1030);

  tkmc_timer_handler(); // Counting them now must not add them a second time.
  EXPECT_UTC(0, 1030);
  tick(1);
  EXPECT_UTC(0, 1040);
}

// Edges that passed before a set are not part of the new value, whether the
// handler runs before or after the next edge: only the edges after the set add
// to it, and each is added once.
static void test_tk_set_utc_drops_ticks_pending_before_set(void) {
  for (UINT later = 0; later < 4; ++later) {
    host_reset();
    run_a_task();
    host_mtime = 2 * HOST_TICK + 3000; // Edges 1 and 2 are pending.
    set_utc(0, 50000);
    EXPECT_UTC(0, 50000);

    host_mtime += later * HOST_TICK; // Edges after the set.
    host_log_count = 0;
    EXPECT_UTC(0, 50000 + 10 * later);
    tkmc_timer_handler();
    EXPECT_UTC(0, 50000 + 10 * later);
    tick(1);
    EXPECT_UTC(0, 50000 + 10 * (later + 1));
  }
}

// A read does not take the pending tick away from the handler: the waiter due
// at that tick is released by the interrupt, and the clock never goes back.
static void test_tk_get_utc_leaves_waiter_due_at_pending_tick_to_handler(void) {
  ID waiter = host_sleep_for(2);
  host_run_idle_task();
  set_utc(0, 1000);
  host_mtime = 2 * HOST_TICK; // The waiter's second tick is pending.
  host_log_count = 0;

  host_snapshot_t before;
  host_take_snapshot(&before);
  SYSTIM first = s_sentinel, second = s_sentinel;
  TEST_ASSERT_EQUAL_INT(E_OK, tk_get_utc(&first));
  TEST_ASSERT_EQUAL_INT(E_OK, tk_get_utc(&second));
  host_assert_unchanged(&before, "tk_get_utc() with a wait due");
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, host_tcb(waiter)->tskstat);
  TEST_ASSERT_EQUAL_UINT(2, host_tcb(waiter)->delay_ticks);
  TEST_ASSERT_EQUAL_UINT32(0, first.hi);
  TEST_ASSERT_EQUAL_UINT32(1020, first.lo);
  TEST_ASSERT_EQUAL_UINT32(first.hi, second.hi);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(first.lo, second.lo);

  tkmc_timer_handler();
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(waiter)->tskstat);
  TEST_ASSERT_EQUAL(E_TMOUT, host_tcb(waiter)->wupcause);
  host_check_kernel_queues();
  EXPECT_UTC(0, 1020);
}

// The machine timer is two words. If the low word wraps between the reads, the
// reader reads again (as the timer code does), or it would see a time that is a
// whole 2^32 counts behind or ahead.
static void
test_tk_get_utc_retries_machine_time_read_that_carries_into_high_word(void) {
  run_a_task();
  host_mtime = ((UD)1 << 32) - HOST_TICK;
  tkmc_init_timer();
  tkmc_start_timer(); // The next edge is exactly 2^32.
  host_mtime = ((UD)1 << 32) - 16;
  host_mtime_advance_after_low_read = 0x30; // 2^32 + 0x20 after the low read.
  host_log_count = 0;
  EXPECT_UTC(0, 10);
}

// One interrupt that ends two waits and leaves a third counts all its ticks,
// not one per wait it ended.
static void
test_utc_clock_advances_by_all_ticks_when_handler_releases_several_waits(void) {
  ID first = host_sleep_for(2);
  ID second = host_sleep_for(3);
  ID third = host_sleep_for(6);
  host_run_idle_task();
  set_utc(0, 1000);

  host_mtime = 4 * HOST_TICK;
  host_log_count = 0;
  tkmc_timer_handler();
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(first)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(second)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, host_tcb(third)->tskstat);
  TEST_ASSERT_EQUAL_UINT(2, host_tcb(third)->delay_ticks);
  EXPECT_UTC(0, 1040);
}

// A set in the middle of a tick moves only the clock: the next edge, and the
// 10 ms it adds, come when they were due, and a wait due at that edge ends
// there.
static void test_tk_set_utc_in_middle_of_tick_keeps_tick_edge(void) {
  ID waiter = host_sleep_for(2);
  host_run_idle_task();
  tick(1);
  TEST_ASSERT_EQUAL_UINT(1, host_tcb(waiter)->delay_ticks);

  host_mtime += 5000; // Half way through the second tick.
  host_log_count = 0;
  set_utc(0, 777);
  EXPECT_UTC(0, 777);
  host_mtime = 2 * HOST_TICK - 1;
  EXPECT_UTC(0, 777);
  host_mtime =
      2 * HOST_TICK; // The original edge, not one a tick after the set.
  EXPECT_UTC(0, 787);

  tkmc_timer_handler();
  EXPECT_UTC(0, 787);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(waiter)->tskstat);
  TEST_ASSERT_EQUAL(E_TMOUT, host_tcb(waiter)->wupcause);
}

// The compare register is the handler's, and it takes the ticks that are
// pending as they are: a set or a read in between would move or consume them.
static void
test_tk_set_utc_and_tk_get_utc_write_no_timer_compare_register(void) {
  run_a_task();
  host_mtime = 2 * HOST_TICK + 5; // Two ticks pending.
  host_log_count = 0;
  set_utc(0, 5);
  EXPECT_UTC(0, 5);
  set_utc(4, 4);
  EXPECT_UTC(4, 4);
  TEST_ASSERT_EQUAL_UINT(0, host_write_count(MTIMECMP_LOW));
  TEST_ASSERT_EQUAL_UINT(0, host_write_count(MTIMECMP_HIGH));
  TEST_ASSERT_EQUAL_UINT(0, host_write_count(HOST_MSIP));

  host_log_count = 0;
  tkmc_timer_handler();
  TEST_ASSERT_EQUAL_UINT64(3 * HOST_TICK,
                           (host_last_write(MTIMECMP_HIGH) << 32) |
                               host_last_write(MTIMECMP_LOW));
  EXPECT_UTC(4, 4);
}

// The calls and the contexts they are made in. A context is the task state
// (a running task, an interrupt handler, no current task at all) and the
// interrupt flag the caller has. A call is checked as: its result; that the
// output buffer, the clock, the kernel's tables and queues and the interrupt
// flag are as the call's result says.

typedef enum { API_SET, API_GET } api_t;
typedef enum { INPUT_NULL, INPUT_NEGATIVE, INPUT_VALID } input_t;
typedef enum { CTX_TASK, CTX_ISR, CTX_NO_TASK, CTX_ISR_NO_TASK } context_t;

static const char *const s_api_name[] = {"tk_set_utc", "tk_get_utc"};
static const char *const s_input_name[] = {"NULL", "negative", "valid"};
static const char *const s_context_name[] = {
    "a task", "an ISR", "no current task", "an ISR with no current task"};

// What the clock was set to, what it holds at the call (two ticks counted and
// one pending since the set, so a refused call that moved the tick of the set
// loses them), and a value that is far from it, so that a set that took effect
// cannot be mistaken for a tick or for the old value.
static const SYSTIM s_base = {.hi = 3, .lo = 1000};
static const SYSTIM s_base_now = {.hi = 3, .lo = 1030};
static const SYSTIM s_far = {.hi = 0x12345678, .lo = 0x9abcdef0u};
static const SYSTIM s_negative = {.hi = -5, .lo = 77};

static void check_call(api_t api, input_t input, context_t context, BOOL mie,
                       ER expected) {
  char label[128];
  snprintf(label, sizeof(label), "%s(%s) from %s, interrupts %s",
           s_api_name[api], s_input_name[input], s_context_name[context],
           mie ? "on" : "off");

  host_reset();
  ID self = host_make_task(HOST_HIGH_PRIORITY);
  host_set_current(self);
  set_utc(s_base.hi, s_base.lo);
  tick(2);
  host_mtime += HOST_TICK;

  const SYSTIM request = input == INPUT_NEGATIVE ? s_negative : s_far;
  SYSTIM output = s_sentinel;
  SYSTIM *buffer = api == API_GET ? &output : NULL;
  const SYSTIM *value = api == API_SET ? &request : NULL;
  if (input == INPUT_NULL) {
    buffer = NULL;
    value = NULL;
  }

  const host_context_t entered =
      host_enter(context == CTX_ISR || context == CTX_ISR_NO_TASK, !mie);
  if (context == CTX_NO_TASK || context == CTX_ISR_NO_TASK) {
    current = NULL;
    next = NULL;
  }
  host_log_count = 0;
  host_snapshot_t before;
  host_take_snapshot(&before);

  ER ercd = api == API_SET ? tk_set_utc(value) : tk_get_utc(buffer);
  BOOL interrupts_on = host_interrupts_enabled();

  host_assert_unchanged(&before, label);
  host_leave(entered);
  host_set_current(self);

  TEST_ASSERT_EQUAL_INT_MESSAGE(expected, ercd, label);
  TEST_ASSERT_EQUAL_INT_MESSAGE(mie, interrupts_on, label);
  TEST_ASSERT_EQUAL_INT_MESSAGE(TRUE, host_interrupts_enabled(), label);
  if (api == API_GET && ercd == E_OK) {
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(s_base_now.hi, output.hi, label);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(s_base_now.lo, output.lo, label);
  } else {
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(s_sentinel.hi, output.hi, label);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(s_sentinel.lo, output.lo, label);
  }
  const SYSTIM *now = api == API_SET && ercd == E_OK ? &s_far : &s_base_now;
  SYSTIM clock = s_sentinel;
  TEST_ASSERT_EQUAL_INT_MESSAGE(E_OK, tk_get_utc(&clock), label);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(now->hi, clock.hi, label);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(now->lo, clock.lo, label);
  host_check_kernel_queues();
}

static void check_each_context(api_t api, input_t input, ER expected) {
  for (context_t context = CTX_TASK; context <= CTX_ISR_NO_TASK; ++context) {
    for (UINT mie = 0; mie < 2; ++mie) {
      check_call(api, input, context, mie, expected);
    }
  }
}

// The pointer comes first: a null pointer is E_PAR even where nothing else
// could be done, an ISR and the time before the first task included.
static void
test_utc_calls_refuse_null_pointer_with_e_par_in_every_context(void) {
  check_each_context(API_SET, INPUT_NULL, E_PAR);
  check_each_context(API_GET, INPUT_NULL, E_PAR);
}

static void
test_utc_calls_refuse_negative_high_word_with_e_par_in_every_context(void) {
  check_each_context(API_SET, INPUT_NEGATIVE, E_PAR);
}

// Only a task takes part: in an ISR or without a current task the call is
// refused and nothing changes, the buffer included.
static void test_utc_calls_refuse_isr_or_no_task_with_e_ctx(void) {
  for (api_t api = API_SET; api <= API_GET; ++api) {
    for (context_t context = CTX_ISR; context <= CTX_ISR_NO_TASK; ++context) {
      for (UINT mie = 0; mie < 2; ++mie) {
        check_call(api, INPUT_VALID, context, mie, E_CTX);
      }
    }
  }
}

// Interrupts off is no reason to refuse a task, and the call leaves them as
// it found them.
static void test_utc_calls_succeed_from_task_with_interrupts_on_or_off(void) {
  for (api_t api = API_SET; api <= API_GET; ++api) {
    for (UINT mie = 0; mie < 2; ++mie) {
      check_call(api, INPUT_VALID, CTX_TASK, mie, E_OK);
    }
  }
}

// A wait counts ticks, not time of day. The wait of each call that can time
// out ends at the same tick, with the same cause, whatever happens to the
// clock in between, and a set changes no wait on its own.
typedef enum {
  WAIT_SLP,
  WAIT_DLY,
  WAIT_SEM,
  WAIT_FLG,
  WAIT_MBX,
  WAIT_COUNT
} wait_t;
typedef enum { UTC_UNCHANGED, UTC_FORWARD, UTC_BACKWARD, UTC_COUNT } change_t;

static const char *const s_wait_name[] = {
    "tk_slp_tsk", "tk_dly_tsk", "tk_wai_sem", "tk_wai_flg", "tk_rcv_mbx"};
static const char *const s_change_name[] = {"unchanged", "forward", "backward"};

// 30 ms is 3 ticks, plus the one the first tick comes at any time.
enum { WAIT_MS = 30, WAIT_TICKS = 4 };

static void check_wait_ends_as_if_utc_did_not_change(wait_t wait,
                                                     change_t change) {
  char label[64];
  snprintf(label, sizeof(label), "%s, UTC %s", s_wait_name[wait],
           s_change_name[change]);

  host_reset();
  ID waiter = host_make_task(HOST_HIGH_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY);
  const host_objects_t obj = host_make_objects();
  host_set_current(other);
  set_utc(1, 0); // Room to move in both directions.

  host_set_current(waiter);
  UINT pattern = 0;
  T_MSG *message = NULL;
  switch (wait) {
  case WAIT_SLP:
    HOST_EXPECT_WAIT(tk_slp_tsk(WAIT_MS));
    break;
  case WAIT_DLY:
    HOST_EXPECT_WAIT(tk_dly_tsk(WAIT_MS));
    break;
  case WAIT_SEM:
    HOST_EXPECT_WAIT(tk_wai_sem(obj.semid, 1, WAIT_MS));
    break;
  case WAIT_FLG:
    HOST_EXPECT_WAIT(tk_wai_flg(obj.flgid, 0x1, TWF_ANDW, &pattern, WAIT_MS));
    break;
  default:
    HOST_EXPECT_WAIT(tk_rcv_mbx(obj.mbxid, &message, WAIT_MS));
    break;
  }
  host_set_current(other);

  tick(1);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_WAI, host_tcb(waiter)->tskstat, label);
  host_log_count = 0;
  host_snapshot_t before;
  host_take_snapshot(&before);
  W hi = 1;
  UW lo = 10;
  if (change == UTC_FORWARD) {
    hi = 2;
    lo = 0;
  } else if (change == UTC_BACKWARD) {
    hi = 0;
    lo = 500;
  }
  if (change != UTC_UNCHANGED) {
    set_utc(hi, lo);
    host_assert_unchanged(&before, label);
  }

  for (UINT passed = 2; passed <= WAIT_TICKS; ++passed) {
    tick(1);
    lo += 10;
    TEST_ASSERT_EQUAL_UINT_MESSAGE(passed < WAIT_TICKS ? TTS_WAI : TTS_RDY,
                                   host_tcb(waiter)->tskstat, label);
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(E_TMOUT, host_tcb(waiter)->wupcause, label);
  host_check_kernel_queues();
  EXPECT_UTC(hi, lo);
}

static void test_relative_waits_end_at_same_tick_whatever_utc_is_set(void) {
  for (wait_t wait = WAIT_SLP; wait < WAIT_COUNT; ++wait) {
    for (change_t change = UTC_UNCHANGED; change < UTC_COUNT; ++change) {
      check_wait_ends_as_if_utc_did_not_change(wait, change);
    }
  }
}

void run_system_time_tests(void) {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_utc_clock_reads_zero_after_boot);
  RUN_TEST(test_utc_clock_resets_on_timer_restart_whatever_machine_time);
  RUN_TEST(test_tk_get_utc_returns_same_value_for_repeated_reads_within_tick);
  RUN_TEST(test_utc_clock_advances_ten_milliseconds_per_tick);
  RUN_TEST(test_utc_clock_advances_by_every_tick_late_timer_interrupt_missed);
  RUN_TEST(test_tk_set_utc_keeps_millisecond_remainder);
  RUN_TEST(test_tk_set_utc_moves_clock_forward_and_backward);
  RUN_TEST(test_tk_set_utc_round_trips_ends_of_utc_range);
  RUN_TEST(test_utc_clock_carries_low_word_into_high_word_on_tick);
  RUN_TEST(test_tk_set_utc_refuses_negative_high_word_and_keeps_clock);
  RUN_TEST(test_utc_clock_saturates_at_maximum_and_stays);
  RUN_TEST(test_tk_set_utc_after_saturation_restarts_clock);
  RUN_TEST(test_tk_get_utc_counts_ticks_whose_interrupt_is_pending);
  RUN_TEST(test_tk_set_utc_drops_ticks_pending_before_set);
  RUN_TEST(test_tk_get_utc_leaves_waiter_due_at_pending_tick_to_handler);
  RUN_TEST(
      test_tk_get_utc_retries_machine_time_read_that_carries_into_high_word);
  RUN_TEST(
      test_utc_clock_advances_by_all_ticks_when_handler_releases_several_waits);
  RUN_TEST(test_tk_set_utc_in_middle_of_tick_keeps_tick_edge);
  RUN_TEST(test_tk_set_utc_and_tk_get_utc_write_no_timer_compare_register);
  RUN_TEST(test_utc_calls_refuse_null_pointer_with_e_par_in_every_context);
  RUN_TEST(
      test_utc_calls_refuse_negative_high_word_with_e_par_in_every_context);
  RUN_TEST(test_utc_calls_refuse_isr_or_no_task_with_e_ctx);
  RUN_TEST(test_utc_calls_succeed_from_task_with_interrupts_on_or_off);
  RUN_TEST(test_relative_waits_end_at_same_tick_whatever_utc_is_set);
}
