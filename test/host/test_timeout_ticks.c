/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Every wait call with a time limit turns the milliseconds into 10 ms ticks the
// same way, rounded up plus one, over the whole range of positive times up to
// INT_MAX. The tick count is read from the task as soon as it waits; how a
// count turns into a time-out is test_wait.c's
// test_a_time_limit_is_rounded_up_to_whole_ticks_plus_one.
//
// Waiting for ever, polling and negative times never reach the conversion; they
// are tested in test_wait.c, test_wait_context.c and the QEMU suites.

#include <limits.h>
#include <stdio.h>

#include <tk/tkernel.h>
#include <unity.h>

#include "fakes.h"

enum { PRIORITY = 4 };

static ID s_semid;
static ID s_flgid;
static ID s_mbxid;
static UINT s_flgptn;
static T_MSG *s_message;

static void call_dly_tsk(TMO ms) { tk_dly_tsk(ms); }
static void call_slp_tsk(TMO ms) { tk_slp_tsk(ms); }
static void call_wai_sem(TMO ms) { tk_wai_sem(s_semid, 1, ms); }
static void call_wai_flg(TMO ms) {
  tk_wai_flg(s_flgid, 0x1, TWF_ORW, &s_flgptn, ms);
}
static void call_rcv_mbx(TMO ms) { tk_rcv_mbx(s_mbxid, &s_message, ms); }

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
  ID self = host_make_task(PRIORITY);
  s_semid = tk_cre_sem(
      &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
  s_flgid = tk_cre_flg(&(T_CFLG){.exinf = 0, .flgatr = TA_TFIFO});
  s_mbxid = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TFIFO});
  TEST_ASSERT_GREATER_THAN_MESSAGE(0, s_semid, label);
  TEST_ASSERT_GREATER_THAN_MESSAGE(0, s_flgid, label);
  TEST_ASSERT_GREATER_THAN_MESSAGE(0, s_mbxid, label);
  host_set_current(self);

  HOST_EXPECT_WAIT(s_apis[api].call(ms));
  TEST_ASSERT_EQUAL_UINT_MESSAGE((UINT)(((UD)ms + 9) / 10 + 1),
                                 tkmc_tcbs[self - 1].delay_ticks, label);
}

void test_every_wait_rounds_its_time_limit_up_to_ticks_plus_one(void) {
  for (UINT api = 0; api < sizeof(s_apis) / sizeof(s_apis[0]); ++api) {
    for (UINT i = 0; i < sizeof(s_times) / sizeof(s_times[0]); ++i) {
      check_ticks(api, s_times[i]);
    }
  }
}
