/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// tk_set_utc() and tk_get_utc() on the target: the clock counts the kernel's
// ticks, a set value takes effect at once and moves no wait, and an interrupt
// handler is refused. The exact tick counting (pending ticks, saturation, the
// call matrix) is checked on the host (test_system_time.c).
//
// The suite runs under QEMU's instruction counting and on the host clock, so
// the checks are on ranges, and every case starts in the middle of a tick: a
// time between n and n+1 ticks long cannot then be told from n+1 by a tick edge
// that comes a moment early.

#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "context.h"
#include "runner.h"

extern void __real_tkmc_timer_handler(void);

enum { CHILD_PRIORITY = 2, WAIT_MS = 50, SLACK_MS = 2000 };
_Static_assert(CHILD_PRIORITY < TEST_CASE_PRIORITY,
               "the child must preempt the case, to block before the case goes "
               "on");

static const SYSTIM s_sentinel = {.hi = 0x55555555, .lo = 0xaaaaaaaau};

static UD value_of(const SYSTIM *time) {
  return ((UD)(UW)time->hi << 32) | time->lo;
}

static UD read_utc(void) {
  SYSTIM now = s_sentinel;
  TEST_ASSERT_EQUAL(E_OK, tk_get_utc(&now));
  return value_of(&now);
}

static void set_utc(W hi, UW lo) {
  const SYSTIM time = {.hi = hi, .lo = lo};
  TEST_ASSERT_EQUAL(E_OK, tk_set_utc(&time));
}

// The clock is at least `from` and has moved by less than `window_ms` since,
// compared on the 64 bit value (Unity has no 64 bit assert on this target).
static void expect_utc_within(UD from, UW window_ms, const char *message) {
  UD now = read_utc();
  TEST_ASSERT_TRUE_MESSAGE(now >= from, message);
  TEST_ASSERT_TRUE_MESSAGE(now - from < window_ms, message);
}

static BOOL irq_enabled(void) {
  UINT status;
  __asm__ volatile("csrr %0, mstatus" : "=r"(status)::"memory");
  return (status & MSTATUS_MIE) != 0;
}

// Keep this case first: it is the only one that looks at the clock before a
// set. The clock reads 0 when the timer starts, so by now it has run for little
// more than the runner's start. The machine timer also starts near 0 on QEMU,
// so this rules out only gross errors; the PC unit tests check the start
// exactly. The bound is loose for the host clock, where a stalled host
// stretches the time.
static void utc_is_small_before_any_set(void) {
  test_mtime_mid_tick();
  UD now = read_utc();
  TEST_ASSERT_TRUE_MESSAGE(now < 5000, "the clock does not start near zero");
}

// A value is read back as set, apart from the ticks since. The values cover
// both words and the end of the low word.
static void set_value_is_read_back(void) {
  static const SYSTIM values[] = {
      {0, 0},
      {0, 123456},
      {0, 0xfffffff0u},
      {1, 0},
      {0x12345678, 0x9abcdef0u},
      {0x7ffffffe, 0xffffff00u},
  };
  for (UINT i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
    test_mtime_mid_tick();
    set_utc(values[i].hi, values[i].lo);
    expect_utc_within(value_of(&values[i]), SLACK_MS,
                      "set value not read back");
  }
}

// After a delay the clock has moved by the delay, as ticks, from a set value
// that is just short of the end of the low word, so the carry is checked too.
// A tick adds 10 ms, so a delay of 100 ms moves it by at least 100 ms. The
// upper bounds of this suite only catch gross errors (SLACK_MS): on the host
// clock a stalled host makes one late interrupt count several ticks.
static void clock_advances_by_a_delay_across_the_low_word_boundary(void) {
  const UD start = 0xffffffffu - 5;
  test_mtime_mid_tick();
  set_utc(0, (UW)start);
  TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(100));
  UD now = read_utc();
  TEST_ASSERT_EQUAL_HEX32(1, (UW)(now >> 32));
  TEST_ASSERT_TRUE_MESSAGE(now >= start + 100, "the clock advanced too little");
  TEST_ASSERT_TRUE_MESSAGE(now < start + 100 + SLACK_MS,
                           "the clock advanced too much");
}

static void clock_advances_by_a_delay_after_a_set_to_a_smaller_value(void) {
  test_mtime_mid_tick();
  set_utc(5, 0);
  test_mtime_mid_tick();
  set_utc(0, 40);
  TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(100));
  UD now = read_utc();
  TEST_ASSERT_TRUE_MESSAGE(now >= 140, "the clock advanced too little");
  TEST_ASSERT_TRUE_MESSAGE(now < 140 + SLACK_MS, "the clock advanced too much");
}

// A wait of each kind, in a child that has a higher priority than the case, so
// that it blocks before the case goes on and sets the clock under it.
enum { WAIT_SLP, WAIT_DLY, WAIT_SEM, WAIT_FLG, WAIT_MBX, WAIT_KINDS };
static const char *const s_wait_name[WAIT_KINDS] = {
    "tk_slp_tsk", "tk_dly_tsk", "tk_wai_sem", "tk_wai_flg", "tk_rcv_mbx"};

static ID s_semid, s_flgid, s_mbxid;
static volatile UINT s_kind;
static volatile ER s_result;
static volatile UW s_end;

static void waiting_child(void *arg) {
  (void)arg;
  UINT pattern = 0;
  T_MSG *message = NULL;
  ER result = E_SYS;
  switch (s_kind) {
  case WAIT_SLP:
    result = tk_slp_tsk(WAIT_MS);
    break;
  case WAIT_DLY:
    result = tk_dly_tsk(WAIT_MS);
    break;
  case WAIT_SEM:
    result = tk_wai_sem(s_semid, 1, WAIT_MS);
    break;
  case WAIT_FLG:
    result = tk_wai_flg(s_flgid, 0x1, TWF_ANDW, &pattern, WAIT_MS);
    break;
  default:
    result = tk_rcv_mbx(s_mbxid, &message, WAIT_MS);
    break;
  }
  s_result = result;
  s_end = test_mtime();
  test_checkpoint();
}

// The wait lasts its time limit and ends with the cause it has without a clock
// change, whatever the clock is set to while it waits.
static void wait_ignores_a_set_of_the_clock(W hi, UW lo) {
  s_semid =
      tk_cre_sem(&(T_CSEM){.sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
  s_flgid = tk_cre_flg(&(T_CFLG){.flgatr = TA_WSGL, .iflgptn = 0});
  s_mbxid = tk_cre_mbx(&(T_CMBX){.mbxatr = TA_TFIFO});
  TEST_ASSERT_GREATER_THAN(0, s_semid);
  TEST_ASSERT_GREATER_THAN(0, s_flgid);
  TEST_ASSERT_GREATER_THAN(0, s_mbxid);

  for (UINT kind = 0; kind < WAIT_KINDS; ++kind) {
    set_utc(1, 0);
    s_kind = kind;
    s_result = E_SYS;
    UW start = test_mtime_mid_tick();
    test_spawn(waiting_child, CHILD_PRIORITY, NULL);
    set_utc(hi, lo);
    test_join_expect(kind + 1); // the checkpoints add up over the case
    TEST_ASSERT_EQUAL_MESSAGE(kind == WAIT_DLY ? E_OK : E_TMOUT, s_result,
                              s_wait_name[kind]);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(
        WAIT_MS * TEST_MTIME_PER_MS, (UW)(s_end - start), s_wait_name[kind]);
    // The new value counted on from the set, not from before it.
    UD now = read_utc();
    UD from = ((UD)(UW)hi << 32) | lo;
    TEST_ASSERT_TRUE_MESSAGE(now >= from + WAIT_MS, s_wait_name[kind]);
    TEST_ASSERT_TRUE_MESSAGE(now < from + WAIT_MS + SLACK_MS,
                             s_wait_name[kind]);
  }
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(s_semid));
  TEST_ASSERT_EQUAL(E_OK, tk_del_mbx(s_mbxid));
}

static void waits_end_normally_when_the_clock_is_set_forward(void) {
  wait_ignores_a_set_of_the_clock(2, 0);
}

static void waits_end_normally_when_the_clock_is_set_backward(void) {
  wait_ignores_a_set_of_the_clock(0, 500);
}

// The calls in a real timer interrupt handler: the pointer and the sign are
// checked first, then the context; the buffer and the clock stay as they were.
// The handler is wrapped at link time (see CMakeLists.txt), and the calls are
// made right after the real one, in the state the handler's caller left.
static const SYSTIM s_rejected = {.hi = 0x1234, .lo = 0x5678};
static const SYSTIM s_negative = {.hi = -1, .lo = 0};
static volatile BOOL s_armed, s_observed, s_in_isr, s_mie_before, s_mie_after;
static volatile ER s_null_set, s_null_get, s_negative_set, s_valid_set,
    s_valid_get;
static SYSTIM s_isr_output;

void __wrap_tkmc_timer_handler(void) {
  __real_tkmc_timer_handler();
  if (!s_armed) {
    return;
  }
  s_armed = FALSE;
  s_in_isr = tkmc_in_isr();
  s_mie_before = irq_enabled();
  s_isr_output = s_sentinel;
  s_null_set = tk_set_utc(NULL);
  s_null_get = tk_get_utc(NULL);
  s_negative_set = tk_set_utc(&s_negative);
  s_valid_set = tk_set_utc(&s_rejected);
  s_valid_get = tk_get_utc(&s_isr_output);
  s_mie_after = irq_enabled();
  s_observed = TRUE;
}

static void calls_in_a_real_interrupt_handler_are_refused(void) {
  const UD start = 1000000;
  test_mtime_mid_tick();
  set_utc(0, (UW)start);
  s_armed = TRUE;
  UW started = test_mtime();
  while (!s_observed &&
         (UW)(test_mtime() - started) < 2000 * TEST_MTIME_PER_MS) {
  }
  TEST_ASSERT_TRUE_MESSAGE(s_observed, "no timer interrupt");
  TEST_ASSERT_TRUE(s_in_isr);
  TEST_ASSERT_EQUAL(E_PAR, s_null_set);
  TEST_ASSERT_EQUAL(E_PAR, s_null_get);
  TEST_ASSERT_EQUAL(E_PAR, s_negative_set);
  TEST_ASSERT_EQUAL(E_CTX, s_valid_set);
  TEST_ASSERT_EQUAL(E_CTX, s_valid_get);
  TEST_ASSERT_EQUAL_HEX32(s_sentinel.hi, s_isr_output.hi);
  TEST_ASSERT_EQUAL_HEX32(s_sentinel.lo, s_isr_output.lo);
  TEST_ASSERT_EQUAL_INT(s_mie_before, s_mie_after);
  // Neither rejected value reached the clock: it is still near the set value,
  // not at the rejected one.
  expect_utc_within(start, SLACK_MS, "a refused call changed the clock");
}

static const test_case_t s_cases[] = {
    {"utc_is_small_before_any_set", utc_is_small_before_any_set},
    {"set_value_is_read_back", set_value_is_read_back},
    {"clock_advances_by_a_delay_across_the_low_word_boundary",
     clock_advances_by_a_delay_across_the_low_word_boundary},
    {"clock_advances_by_a_delay_after_a_set_to_a_smaller_value",
     clock_advances_by_a_delay_after_a_set_to_a_smaller_value},
    {"waits_end_normally_when_the_clock_is_set_forward",
     waits_end_normally_when_the_clock_is_set_forward},
    {"waits_end_normally_when_the_clock_is_set_backward",
     waits_end_normally_when_the_clock_is_set_backward},
    {"calls_in_a_real_interrupt_handler_are_refused",
     calls_in_a_real_interrupt_handler_are_refused},
};

TEST_SUITE_MAIN("system-time", s_cases, 5000)
