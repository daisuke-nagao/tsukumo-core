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

#include <tk/tkernel.h>
#include <unity.h>

#include "event_flag.h"
#include "fakes.h"
#include "task.h"

enum { HIGH_PRIORITY = 4, LOW_PRIORITY = 5, WAIT_MS = 100 };

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

static TCB *tcb_of(ID tskid) { return &tkmc_tcbs[tskid - 1]; }

// Makes a task at prio wait for waiptn on the flag, for ever, and returns it.
static ID make_waiter(ID flgid, PRI prio, UINT waiptn, UINT wfmode,
                      UINT *flgptn) {
  ID waiter = host_make_task(prio);
  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_flg(flgid, waiptn, wfmode, flgptn, TMO_FEVR));
  return waiter;
}

void test_clearing_a_flag_keeps_the_bits_of_the_mask(void) {
  ID flgid = create_flag(TA_TFIFO | TA_WMUL, 0x0F);

  TEST_ASSERT_EQUAL(E_OK, tk_clr_flg(flgid, 0x03));
  TEST_ASSERT_EQUAL_HEX32(0x03, read_pattern(flgid));

  // A mask of 0 clears every bit: no poll finds one.
  TEST_ASSERT_EQUAL(E_OK, tk_clr_flg(flgid, 0));
  UINT flgptn = SENTINEL;
  TEST_ASSERT_EQUAL(E_TMOUT, tk_wai_flg(flgid, ~0U, TWF_ORW, &flgptn, TMO_POL));
}

void test_a_one_waiter_flag_refuses_a_second_task_before_the_pattern_check(
    void) {
  ID flgid = create_flag(TA_TFIFO | TA_WSGL, 0);
  UINT first_ptn = SENTINEL;
  make_waiter(flgid, HIGH_PRIORITY, 0x2, TWF_ORW, &first_ptn);
  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(flgid, 0x1)); // Not what the waiter wants.
  host_set_current(host_make_task(LOW_PRIORITY));

  static const TMO limits[] = {TMO_POL, WAIT_MS, TMO_FEVR};
  for (UINT i = 0; i < sizeof(limits) / sizeof(limits[0]); ++i) {
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

void test_a_set_wakes_every_waiter_of_a_multi_waiter_flag_it_satisfies(void) {
  ID flgid = create_flag(TA_TFIFO | TA_WMUL, 0);
  UINT ptn_a = SENTINEL, ptn_b = SENTINEL, ptn_c = SENTINEL;
  ID a = make_waiter(flgid, HIGH_PRIORITY, 0x1, TWF_ORW, &ptn_a);
  ID b = make_waiter(flgid, HIGH_PRIORITY, 0x2, TWF_ORW, &ptn_b);
  ID c = make_waiter(flgid, HIGH_PRIORITY, 0x4, TWF_ORW, &ptn_c);
  host_set_current(host_make_task(LOW_PRIORITY));

  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(flgid, 0x3));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb_of(a)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb_of(b)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb_of(c)->tskstat);
  // The pattern each woken task is handed, as tk_wai_flg() will report it.
  TEST_ASSERT_EQUAL_HEX32(0x3, tcb_of(a)->winfo.event_flag.flgptn);
  TEST_ASSERT_EQUAL_HEX32(0x3, tcb_of(b)->winfo.event_flag.flgptn);
  TEST_ASSERT_EQUAL_PTR(tcb_of(a), next);
  host_check_kernel_queues();
}

void test_a_clear_by_one_waiter_is_seen_by_the_waiters_behind_it(void) {
  ID flgid = create_flag(TA_TFIFO | TA_WMUL, 0);
  UINT ptn_a = SENTINEL, ptn_b = SENTINEL;
  ID a = make_waiter(flgid, HIGH_PRIORITY, 0x1, TWF_ORW | TWF_CLR, &ptn_a);
  ID b = make_waiter(flgid, HIGH_PRIORITY, 0x1, TWF_ORW, &ptn_b);
  host_set_current(host_make_task(LOW_PRIORITY));

  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(flgid, 0x1));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb_of(a)->tskstat);
  TEST_ASSERT_EQUAL_HEX32(0x1, tcb_of(a)->winfo.event_flag.flgptn);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb_of(b)->tskstat);
  TEST_ASSERT_EQUAL_HEX32(0, tkmc_flgcbs[flgid - 1].flgptn);
  host_check_kernel_queues();
}

void test_waiters_of_a_priority_flag_queue_by_priority_then_arrival(void) {
  ID flgid = create_flag(TA_TPRI | TA_WMUL, 0);
  UINT ptn = SENTINEL;
  ID low1 = make_waiter(flgid, LOW_PRIORITY, 0x1, TWF_ORW, &ptn);
  ID high = make_waiter(flgid, HIGH_PRIORITY, 0x1, TWF_ORW, &ptn);
  ID low2 = make_waiter(flgid, LOW_PRIORITY, 0x1, TWF_ORW, &ptn);

  const ID expected[] = {high, low1, low2};
  const tkmc_list_head *queue = &tkmc_flgcbs[flgid - 1].wait_queue;
  const tkmc_list_head *pos = queue->next;
  for (UINT i = 0; i < 3; ++i, pos = pos->next) {
    TEST_ASSERT_EQUAL_PTR(&tcb_of(expected[i])->winfo.wait_queue, pos);
  }
  TEST_ASSERT_EQUAL_PTR(queue, pos);
}
