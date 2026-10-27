/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>
#include <unity.h>

#include "runner.h"
#include "task.h"

enum { CHILD_PRIORITY = 2, RESTARTS = 100, CLEANUP = -1 };
_Static_assert(CHILD_PRIORITY < TEST_CASE_PRIORITY,
               "the child must run before its parent resumes");

static _Alignas(16) UW s_stack[256];
static ID s_gate;
static UINT s_token;
static UINT s_expected;
static volatile UINT s_entries;

static __attribute__((noinline)) UW use_stack(UW seed) {
  volatile UW words[64];
  for (UINT i = 0; i < 64; ++i) {
    words[i] = seed + i;
  }
  UW sum = 0;
  for (UINT i = 0; i < 64; ++i) {
    sum += words[i];
  }
  return sum;
}

static void restarted_child(INT stacd, void *exinf) {
  TEST_ASSERT_EQUAL_PTR(&s_token, exinf);
  TEST_ASSERT_EQUAL(E_TMOUT, tk_slp_tsk(TMO_POL));
  if (stacd == CLEANUP) {
    tk_exd_tsk();
  }
  TEST_ASSERT_EQUAL_INT(s_expected, stacd);
  TEST_ASSERT_EQUAL_UINT32(64 * (UW)stacd + 2016, use_stack((UW)stacd));
  TEST_ASSERT_EQUAL(E_OK, tk_wai_sem(s_gate, 1, TMO_FEVR));
  ++s_entries;
  test_checkpoint();
  if ((stacd & 1) != 0) {
    tk_ext_tsk();
  }
  // Even runs return through the ra installed by tk_sta_tsk().
}

static UINT task_state(ID id) {
  UINT intsts;
  DI(intsts);
  UINT state = tkmc_tcbs[id - 1].tskstat;
  EI(intsts);
  return state;
}

static void restart_preserves_entry_and_arguments(void) {
  s_entries = 0;
  s_gate = tk_cre_sem(&(T_CSEM){.sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
  TEST_ASSERT_GREATER_THAN(0, s_gate);
  ID id = tk_cre_tsk(&(T_CTSK){.exinf = &s_token,
                               .tskatr = TA_HLNG | TA_USERBUF,
                               .task = (FP)restarted_child,
                               .itskpri = CHILD_PRIORITY,
                               .stksz = sizeof(s_stack),
                               .bufptr = s_stack});
  TEST_ASSERT_GREATER_THAN(0, id);
  for (s_expected = 0; s_expected <= RESTARTS; ++s_expected) {
    TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(id, (INT)s_expected));
    TEST_ASSERT_EQUAL_UINT(TTS_WAI, task_state(id));
    // A wakeup while waiting on a semaphore must not survive the next start.
    TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(id));
    TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(s_gate, 1));
    TEST_ASSERT_EQUAL_UINT(TTS_DMT, task_state(id));
    TEST_ASSERT_EQUAL_UINT(s_expected + 1, s_entries);
  }
  TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(id, CLEANUP));
  TEST_ASSERT_EQUAL_UINT(TTS_NOEXS, task_state(id));
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(s_gate));
  test_join_expect(RESTARTS + 1);
}

static const test_case_t s_cases[] = {
    {"restart_preserves_entry_and_arguments",
     restart_preserves_entry_and_arguments},
};

TEST_SUITE_MAIN("task-restart", s_cases, 5000)
