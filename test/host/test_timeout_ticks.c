/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Every wait call with a time limit turns the milliseconds into 10 ms ticks the
// same way, rounded up plus one, over the whole range of positive times up to
// INT_MAX. The tick count is read from the task as soon as it waits; how a
// count turns into a time-out is
// test_time_limit_ends_wait_after_rounded_up_ticks_plus_one, below.
//
// Waiting for ever, polling and negative times never reach the conversion; they
// are tested in test_wait.c, test_wait_context.c and the QEMU suites.

#include <limits.h>
#include <stdio.h>

#include <tk/tkernel.h>
#include <unity.h>

#include "fakes.h"

static host_objects_t s_obj;
static UINT s_flgptn;
static T_MSG *s_message;

static void call_dly_tsk(TMO ms) { tk_dly_tsk(ms); }
static void call_slp_tsk(TMO ms) { tk_slp_tsk(ms); }
static void call_wai_sem(TMO ms) { tk_wai_sem(s_obj.semid, 1, ms); }
static void call_wai_flg(TMO ms) {
  tk_wai_flg(s_obj.flgid, 0x1, TWF_ORW, &s_flgptn, ms);
}
static void call_rcv_mbx(TMO ms) { tk_rcv_mbx(s_obj.mbxid, &s_message, ms); }

static const struct {
  const char *name;
  void (*call)(TMO ms);
} s_apis[] = {
    {"tk_dly_tsk", call_dly_tsk}, {"tk_slp_tsk", call_slp_tsk},
    {"tk_wai_sem", call_wai_sem}, {"tk_wai_flg", call_wai_flg},
    {"tk_rcv_mbx", call_rcv_mbx},
};

// INT_MAX - 9 is the last time the old `(ms + 9) / 10 + 1` did not overflow.
static const TMO s_times[] = {1, 9, 10, 11, INT_MAX - 9, INT_MAX - 8, INT_MAX};

// Makes the API wait for ms on a fresh kernel, with nothing there to end the
// wait, and checks the tick count the task waits for.
static void check_ticks(UINT api, TMO ms) {
  char label[48];
  snprintf(label, sizeof(label), "%s(%d)", s_apis[api].name, (int)ms);

  host_reset();
  ID self = host_make_task(HOST_HIGH_PRIORITY);
  s_obj = host_make_objects();
  host_set_current(self);

  HOST_EXPECT_WAIT(s_apis[api].call(ms));
  TEST_ASSERT_EQUAL_UINT_MESSAGE((UINT)(((UD)ms + 9) / 10 + 1),
                                 tkmc_tcbs[self - 1].delay_ticks, label);
  host_check_kernel_state();
}

static void test_wait_calls_round_time_limit_up_to_ticks_plus_one(void) {
  for (UINT api = 0; api < HOST_COUNT(s_apis); ++api) {
    for (UINT i = 0; i < HOST_COUNT(s_times); ++i) {
      check_ticks(api, s_times[i]);
    }
  }
}

// A delay is a RELTIM, which is unsigned: tk_dly_tsk() takes the whole range
// up to UINT_MAX ms (about 49.7 days), where a time limit (TMO) stops at
// INT_MAX. The kernel used to take a TMO and refuse the upper half as negative.
static void test_tk_dly_tsk_takes_whole_unsigned_range(void) {
  ID self = host_make_task(HOST_HIGH_PRIORITY);
  host_set_current(self);

  HOST_EXPECT_WAIT(tk_dly_tsk(UINT_MAX));
  TEST_ASSERT_EQUAL_UINT(UINT_MAX / 10 + 1 + 1,
                         tkmc_tcbs[self - 1].delay_ticks);
  host_check_kernel_state();
}

// A wait with a time limit lasts a whole number of ticks: the limit in
// milliseconds rounded up, plus one tick because the first tick comes at any
// time after the wait began. A wait may be longer than asked, never shorter.
static void expect_time_out_after(TMO limit_ms, UINT ticks) {
  host_reset();
  ID waiter = host_make_task(HOST_HIGH_PRIORITY);
  ID semid = host_make_objects().semid;
  TCB *tcb = host_tcb(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, limit_ms));
  host_tick(ticks - 1);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_WAI, tcb->tskstat,
                                 "the wait ended before its time limit");
  host_tick(1);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_RDY, tcb->tskstat,
                                 "the wait outlasted its time limit");
  host_check_kernel_state();
}

static void test_time_limit_ends_wait_after_rounded_up_ticks_plus_one(void) {
  expect_time_out_after(1, 2);
  expect_time_out_after(10, 2);
  expect_time_out_after(11, 3);
  expect_time_out_after(50, 6);
  expect_time_out_after(55, 7);
}

void run_timeout_ticks_tests(void) {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_wait_calls_round_time_limit_up_to_ticks_plus_one);
  RUN_TEST(test_tk_dly_tsk_takes_whole_unsigned_range);
  RUN_TEST(test_time_limit_ends_wait_after_rounded_up_ticks_plus_one);
}
