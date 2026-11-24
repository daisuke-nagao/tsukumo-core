/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Event flags with more than one waiter, and what tk_clr_flg() clears.
//
// tk_clr_flg(flgid, clrptn) keeps the bits of clrptn (flgptn &= clrptn), as
// μT-Kernel defines it: the bits that are 0 in clrptn are cleared. The kernel
// used to clear the bits that are 1 in clrptn.
//
// A flag that allows one waiter (TA_WSGL) refuses a second one with E_OBJ as
// soon as it finds a waiter, before it looks at the pattern: the second task
// gets E_OBJ even when its pattern is set, and a poll gets E_OBJ too. The
// kernel used to serve a set pattern first, and could clear it (TWF_CLR) from
// under the task that waited.
//
// A flag that allows many waiters (TA_WMUL) wakes every waiter whose pattern a
// set meets, in queue order, and a clear one waiter asked for is seen by the
// waiters behind it. Under TA_TPRI the queue is ordered by priority, then by
// arrival.

#include <sys/interrupt.h>
#include <tk/tkernel.h>
#include <unity.h>

#include "event_flag.h"
#include "fakes.h"
#include "task.h"

enum { WAIT_MS = 100 };

static const UINT SENTINEL = 0xdeadbeefU;

static ID create_flag(ATR flgatr, UINT iflgptn) {
  ID flgid =
      tk_cre_flg(&(T_CFLG){.exinf = 0, .flgatr = flgatr, .iflgptn = iflgptn});
  TEST_ASSERT_GREATER_THAN(0, flgid);
  return flgid;
}

static UINT read_pattern(ID flgid) {
  UINT flgptn = SENTINEL;
  TEST_ASSERT_EQUAL(E_OK, tk_wai_flg(flgid, ~0U, TWF_ORW, &flgptn, TMO_POL));
  return flgptn;
}

// Makes a task at prio wait for waiptn on the flag, for ever, and returns it.
static ID make_waiter(ID flgid, PRI prio, UINT waiptn, UINT wfmode,
                      UINT *flgptn) {
  ID waiter = host_make_task(prio);
  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_flg(flgid, waiptn, wfmode, flgptn, TMO_FEVR));
  return waiter;
}

static void check_null_flag_creation(void) {
  const BOOL interrupts = host_interrupts_enabled();
  host_snapshot_t before;
  host_take_snapshot(&before);
  TEST_ASSERT_EQUAL(E_PAR, tk_cre_flg(NULL));
  TEST_ASSERT_EQUAL_UINT(interrupts, host_interrupts_enabled());
  host_assert_unchanged(&before, "a NULL flag creation packet");
  host_check_kernel_queues();
}

static void test_tk_cre_flg_refuses_null_packet_and_keeps_free_slots(void) {
  for (UINT disabled = 0; disabled < 2; ++disabled) {
    host_reset();
    UINT intsts;
    if (disabled) {
      DI(intsts);
    }
    for (UINT i = 0; i < CFN_MAX_FLGID; ++i) {
      check_null_flag_creation();
    }
    for (UINT i = 0; i < CFN_MAX_FLGID; ++i) {
      TEST_ASSERT_EQUAL(i + 1, create_flag(TA_WSGL, 0));
    }
    check_null_flag_creation();
    TEST_ASSERT_EQUAL(E_LIMIT, tk_cre_flg(&(T_CFLG){.flgatr = TA_WSGL}));
    if (disabled) {
      EI(intsts);
    }
  }
}

static void check_null_output(ID flgid, UINT mode, TMO tmout, ER expected) {
  const BOOL interrupts = host_interrupts_enabled();
  host_snapshot_t before;
  host_take_snapshot(&before);
  TEST_ASSERT_EQUAL(expected, tk_wai_flg(flgid, 0x1, mode, NULL, tmout));
  TEST_ASSERT_EQUAL_UINT(interrupts, host_interrupts_enabled());
  host_assert_unchanged(&before, "a NULL flag output pointer");
  host_check_kernel_queues();
}

static void test_tk_wai_flg_null_output_neither_clears_nor_waits(void) {
  const TMO limits[] = {TMO_POL, WAIT_MS, TMO_FEVR};
  const UINT modes[] = {TWF_ORW, TWF_ORW | TWF_CLR, TWF_ORW | TWF_BITCLR};
  for (UINT pattern = 0; pattern < 2; ++pattern) {
    host_reset();
    ID flgid = create_flag(TA_WSGL, pattern);
    host_set_current(host_make_task(HOST_HIGH_PRIORITY));
    for (UINT disabled = 0; disabled < 2; ++disabled) {
      UINT intsts;
      if (disabled) {
        DI(intsts);
      }
      for (UINT t = 0; t < HOST_COUNT(limits); ++t) {
        for (UINT m = 0; m < HOST_COUNT(modes); ++m) {
          check_null_output(flgid, modes[m], limits[t], E_PAR);
        }
      }
      if (disabled) {
        EI(intsts);
      }
    }
    UINT output = SENTINEL;
    TEST_ASSERT_EQUAL(pattern ? E_OK : E_TMOUT,
                      tk_wai_flg(flgid, 0x1, TWF_ORW, &output, TMO_POL));
    TEST_ASSERT_EQUAL_HEX32(pattern ? pattern : SENTINEL, output);
  }
}

static void test_tk_wai_flg_checks_null_output_after_id_before_existence(void) {
  host_set_current(host_make_task(HOST_HIGH_PRIORITY));
  const ID bad_ids[] = {0, -1, CFN_MAX_FLGID + 1};
  for (UINT disabled = 0; disabled < 2; ++disabled) {
    UINT intsts;
    if (disabled) {
      DI(intsts);
    }
    for (UINT i = 0; i < HOST_COUNT(bad_ids); ++i) {
      check_null_output(bad_ids[i], TWF_ORW, TMO_POL, E_ID);
    }
    check_null_output(1, TWF_ORW, TMO_POL, E_PAR); // In range, not created.
    if (disabled) {
      EI(intsts);
    }
  }
}

static void test_tk_wai_flg_checks_null_output_before_single_waiter_rule(void) {
  ID flgid = create_flag(TA_WSGL, 0x1);
  UINT output = SENTINEL;
  make_waiter(flgid, HOST_HIGH_PRIORITY, 0x2, TWF_ORW, &output);
  host_set_current(host_make_task(HOST_LOW_PRIORITY));
  const TMO limits[] = {TMO_POL, WAIT_MS, TMO_FEVR};
  for (UINT disabled = 0; disabled < 2; ++disabled) {
    UINT intsts;
    if (disabled) {
      DI(intsts);
    }
    for (UINT i = 0; i < HOST_COUNT(limits); ++i) {
      check_null_output(flgid, TWF_ORW | TWF_CLR, limits[i], E_PAR);
    }
    if (disabled) {
      EI(intsts);
    }
  }
  TEST_ASSERT_EQUAL_HEX32(SENTINEL, output);
}

static void test_tk_clr_flg_keeps_bits_of_mask(void) {
  host_set_current(host_make_task(4));
  ID flgid = create_flag(TA_TFIFO | TA_WMUL, 0x0F);

  TEST_ASSERT_EQUAL(E_OK, tk_clr_flg(flgid, 0x03));
  TEST_ASSERT_EQUAL_HEX32(0x03, read_pattern(flgid));

  // A mask of 0 clears every bit: no poll finds one.
  TEST_ASSERT_EQUAL(E_OK, tk_clr_flg(flgid, 0));
  UINT flgptn = SENTINEL;
  TEST_ASSERT_EQUAL(E_TMOUT, tk_wai_flg(flgid, ~0U, TWF_ORW, &flgptn, TMO_POL));
}

static void test_tk_wai_flg_refuses_second_waiter_before_pattern_check(void) {
  ID flgid = create_flag(TA_TFIFO | TA_WSGL, 0);
  UINT first_ptn = SENTINEL;
  make_waiter(flgid, HOST_HIGH_PRIORITY, 0x2, TWF_ORW, &first_ptn);
  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(flgid, 0x1)); // Not what the waiter wants.
  host_set_current(host_make_task(HOST_LOW_PRIORITY));

  static const TMO limits[] = {TMO_POL, WAIT_MS, TMO_FEVR};
  for (UINT i = 0; i < HOST_COUNT(limits); ++i) {
    UINT flgptn = SENTINEL;
    host_snapshot_t before;
    host_take_snapshot(&before);
    TEST_ASSERT_EQUAL(
        E_OBJ, tk_wai_flg(flgid, 0x1, TWF_ORW | TWF_CLR, &flgptn, limits[i]));
    TEST_ASSERT_TRUE(host_interrupts_enabled());
    TEST_ASSERT_EQUAL_HEX32(SENTINEL, flgptn);
    host_assert_unchanged(&before, "a second waiter on a TA_WSGL flag");
  }
  TEST_ASSERT_EQUAL_HEX32(0x1, tkmc_flgcbs[flgid - 1].flgptn);
}

static void
test_tk_set_flg_wakes_every_satisfied_waiter_of_multi_waiter_flag(void) {
  ID flgid = create_flag(TA_TFIFO | TA_WMUL, 0);
  UINT ptn_a = SENTINEL, ptn_b = SENTINEL, ptn_c = SENTINEL;
  ID a = make_waiter(flgid, HOST_HIGH_PRIORITY, 0x1, TWF_ORW, &ptn_a);
  ID b = make_waiter(flgid, HOST_HIGH_PRIORITY, 0x2, TWF_ORW, &ptn_b);
  ID c = make_waiter(flgid, HOST_HIGH_PRIORITY, 0x4, TWF_ORW, &ptn_c);
  host_set_current(host_make_task(HOST_LOW_PRIORITY));

  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(flgid, 0x3));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(a)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(b)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, host_tcb(c)->tskstat);
  // The pattern each woken task is handed, as tk_wai_flg() will report it.
  TEST_ASSERT_EQUAL_HEX32(0x3, host_tcb(a)->winfo.event_flag.flgptn);
  TEST_ASSERT_EQUAL_HEX32(0x3, host_tcb(b)->winfo.event_flag.flgptn);
  TEST_ASSERT_EQUAL_PTR(host_tcb(a), next);
  host_check_kernel_queues();
}

static void
test_tk_wai_flg_clear_by_one_waiter_reaches_waiters_behind_it(void) {
  ID flgid = create_flag(TA_TFIFO | TA_WMUL, 0);
  UINT ptn_a = SENTINEL, ptn_b = SENTINEL;
  ID a = make_waiter(flgid, HOST_HIGH_PRIORITY, 0x1, TWF_ORW | TWF_CLR, &ptn_a);
  ID b = make_waiter(flgid, HOST_HIGH_PRIORITY, 0x1, TWF_ORW, &ptn_b);
  host_set_current(host_make_task(HOST_LOW_PRIORITY));

  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(flgid, 0x1));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(a)->tskstat);
  TEST_ASSERT_EQUAL_HEX32(0x1, host_tcb(a)->winfo.event_flag.flgptn);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, host_tcb(b)->tskstat);
  TEST_ASSERT_EQUAL_HEX32(0, tkmc_flgcbs[flgid - 1].flgptn);
  host_check_kernel_queues();
}

static void
test_event_flag_priority_waiters_queue_by_priority_then_arrival(void) {
  ID flgid = create_flag(TA_TPRI | TA_WMUL, 0);
  UINT ptn = SENTINEL;
  ID low1 = make_waiter(flgid, HOST_LOW_PRIORITY, 0x1, TWF_ORW, &ptn);
  ID high = make_waiter(flgid, HOST_HIGH_PRIORITY, 0x1, TWF_ORW, &ptn);
  ID low2 = make_waiter(flgid, HOST_LOW_PRIORITY, 0x1, TWF_ORW, &ptn);

  const ID expected[] = {high, low1, low2};
  const tkmc_list_head *queue = &tkmc_flgcbs[flgid - 1].wait_queue;
  const tkmc_list_head *pos = queue->next;
  for (UINT i = 0; i < 3; ++i, pos = pos->next) {
    TEST_ASSERT_EQUAL_PTR(&host_tcb(expected[i])->winfo.wait_queue, pos);
  }
  TEST_ASSERT_EQUAL_PTR(queue, pos);
}

// TWF_ANDW waits for every bit of its pattern. A flag that holds only some of
// them satisfies neither a poll nor a waiter, whatever other bits it holds;
// one that holds them all satisfies both. With a pattern of one bit, AND and
// OR would behave the same, so the pattern has three.
static void test_tk_wai_flg_andw_needs_every_bit_of_pattern(void) {
  ID setter = host_make_task(HOST_LOW_PRIORITY);
  host_set_current(setter);
  ID flgid = create_flag(TA_TFIFO | TA_WMUL, 0x5);
  UINT ptn = SENTINEL;
  TEST_ASSERT_EQUAL(E_TMOUT, tk_wai_flg(flgid, 0x7, TWF_ANDW, &ptn, TMO_POL));

  UINT got = SENTINEL;
  ID waiter = make_waiter(flgid, HOST_HIGH_PRIORITY, 0x7, TWF_ANDW, &got);
  host_set_current(setter);
  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(flgid, 0x8));
  TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_WAI, host_tcb(waiter)->tskstat,
                                 "a flag without bit 1 woke the AND waiter");
  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(flgid, 0x2));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(waiter)->tskstat);
  TEST_ASSERT_EQUAL_HEX32(0xf, host_tcb(waiter)->winfo.event_flag.flgptn);
  host_check_kernel_queues();

  host_set_current(setter);
  TEST_ASSERT_EQUAL(E_OK, tk_wai_flg(flgid, 0x7, TWF_ANDW, &ptn, TMO_POL));
  TEST_ASSERT_EQUAL_HEX32(0xf, ptn);
}

// TWF_BITCLR clears only the bits of the pattern, both when a wait is
// satisfied at once and when a set wakes the waiter (two separate paths in
// the kernel). The other bits of the flag stay.
static void test_tk_wai_flg_bitclr_clears_only_bits_of_pattern(void) {
  ID setter = host_make_task(HOST_LOW_PRIORITY);
  host_set_current(setter);
  ID flgid = create_flag(TA_TFIFO | TA_WMUL, 0xf3);
  UINT ptn = SENTINEL;
  TEST_ASSERT_EQUAL(
      E_OK, tk_wai_flg(flgid, 0x3, TWF_ANDW | TWF_BITCLR, &ptn, TMO_POL));
  TEST_ASSERT_EQUAL_HEX32(0xf3, ptn);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xf0, read_pattern(flgid),
                                  "the poll path cleared other bits");

  UINT got = SENTINEL;
  ID waiter =
      make_waiter(flgid, HOST_HIGH_PRIORITY, 0x3, TWF_ORW | TWF_BITCLR, &got);
  host_set_current(setter);
  // 0x4 is outside the pattern: it must stay, and a kernel that cleared the
  // bits just set instead of the pattern's would take it away.
  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(flgid, 0x5));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(waiter)->tskstat);
  TEST_ASSERT_EQUAL_HEX32(0xf5, host_tcb(waiter)->winfo.event_flag.flgptn);
  host_set_current(setter);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xf4, read_pattern(flgid),
                                  "the wake path cleared other bits");
  host_check_kernel_queues();
}

typedef struct {
  UINT wait_mode;
  UINT initial_pattern;
  TMO timeout;
  const char *label;
} combined_clear_case_t;

static void check_combined_clear_mode(const combined_clear_case_t *test_case,
                                      BOOL interrupts_off) {
  host_reset();
  ID current_task = host_make_task(HOST_LOW_PRIORITY);
  ID flgid = create_flag(TA_TFIFO | TA_WMUL, test_case->initial_pattern);

  // Keep an unrelated timed waiter on the flag while the invalid call runs.
  // Its wait queue entry, timer, task state, next task and dispatch count must
  // all survive a refused request.
  UINT waiter_pattern = SENTINEL;
  ID waiter = host_make_task(HOST_HIGH_PRIORITY);
  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_flg(flgid, 0x80, TWF_ORW, &waiter_pattern, WAIT_MS));
  host_set_current(current_task);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, host_tcb(waiter)->tskstat);
  TEST_ASSERT_TRUE(host_in_queue(&tkmc_flgcbs[flgid - 1].wait_queue,
                                 &host_tcb(waiter)->winfo.wait_queue));
  TEST_ASSERT_TRUE(host_in_queue(&tkmc_timer_queue, &host_tcb(waiter)->head));
  host_check_kernel_state();

  UINT intsts = 0;
  if (interrupts_off) {
    DI(intsts);
  }
  const BOOL interrupts_before = host_interrupts_enabled();
  UINT output = SENTINEL;
  host_snapshot_t before;
  host_take_snapshot(&before);

  ER ercd = E_SYS;
  BOOL waited = FALSE;
  host_waited = 0;
  if (setjmp(host_wait_jmp) == 0) {
    host_arm_wait();
    ercd = tk_wai_flg(flgid, 0x3, test_case->wait_mode | TWF_CLR | TWF_BITCLR,
                      &output, test_case->timeout);
  } else {
    waited = TRUE;
  }
  host_wait_armed = 0;

  TEST_ASSERT_FALSE_MESSAGE(waited, test_case->label);
  TEST_ASSERT_EQUAL_MESSAGE(E_PAR, ercd, test_case->label);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(SENTINEL, output, test_case->label);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(interrupts_before, host_interrupts_enabled(),
                                 test_case->label);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(SENTINEL, waiter_pattern,
                                  "the existing timed wait wrote its output");
  host_assert_unchanged(&before, test_case->label);
  if (interrupts_off) {
    EI(intsts);
  }
  host_check_kernel_state();
}

static void test_tk_wai_flg_rejects_combined_clear_modes_without_waiting(void) {
  UINT output = SENTINEL;
  TEST_ASSERT_EQUAL(E_ID,
                    tk_wai_flg(0, 0x3, TWF_CLR | TWF_BITCLR, &output, TMO_POL));
  TEST_ASSERT_EQUAL_HEX32(SENTINEL, output);

  static const combined_clear_case_t cases[] = {
      {TWF_ANDW, 0x7, TMO_POL, "AND poll with extra matched bits"},
      {TWF_ORW, 0x7, TMO_POL, "OR poll with extra matched bits"},
      {TWF_ANDW, 0x1, TMO_POL, "unmatched AND poll"},
      {TWF_ORW, 0x4, TMO_POL, "unmatched OR poll"},
      {TWF_ANDW, 0x1, WAIT_MS, "unmatched finite AND wait"},
      {TWF_ORW, 0x4, WAIT_MS, "unmatched finite OR wait"},
  };

  for (UINT disabled = 0; disabled < 2; ++disabled) {
    for (UINT i = 0; i < HOST_COUNT(cases); ++i) {
      check_combined_clear_mode(&cases[i], disabled != 0);
    }
  }
}

void run_event_flag_tests(void) {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_tk_clr_flg_keeps_bits_of_mask);
  RUN_TEST(test_tk_wai_flg_refuses_second_waiter_before_pattern_check);
  RUN_TEST(test_tk_set_flg_wakes_every_satisfied_waiter_of_multi_waiter_flag);
  RUN_TEST(test_tk_wai_flg_clear_by_one_waiter_reaches_waiters_behind_it);
  RUN_TEST(test_event_flag_priority_waiters_queue_by_priority_then_arrival);
  RUN_TEST(test_tk_cre_flg_refuses_null_packet_and_keeps_free_slots);
  RUN_TEST(test_tk_wai_flg_null_output_neither_clears_nor_waits);
  RUN_TEST(test_tk_wai_flg_checks_null_output_after_id_before_existence);
  RUN_TEST(test_tk_wai_flg_checks_null_output_before_single_waiter_rule);
  RUN_TEST(test_tk_wai_flg_andw_needs_every_bit_of_pattern);
  RUN_TEST(test_tk_wai_flg_bitclr_clears_only_bits_of_pattern);
  RUN_TEST(test_tk_wai_flg_rejects_combined_clear_modes_without_waiting);
}
