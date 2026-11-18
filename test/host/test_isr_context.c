/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>
#include <unity.h>

#include "context.h"
#include "event_flag.h"
#include "fakes.h"
#include "mailbox.h"
#include "semaphore.h"
#include "task.h"

enum { PRIORITY = 4, HIGH_PRIORITY = 3, LOW_PRIORITY = 5 };

static ID s_self;
static host_objects_t s_obj;
static T_MSG s_message;
static UW s_task_stack[256] __attribute__((aligned(16)));

static void never_run(INT stacd, void *exinf) {
  (void)stacd;
  (void)exinf;
}

static void fresh(void) {
  s_self = host_make_task(PRIORITY);
  host_set_current(s_self);
  s_obj = host_make_objects();
  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(s_obj.flgid, 5));
  s_message = (T_MSG){0};
}

static ER create_task(void) {
  return tk_cre_tsk(&(T_CTSK){.exinf = 0,
                              .tskatr = TA_HLNG | TA_USERBUF,
                              .task = (FP)never_run,
                              .itskpri = PRIORITY,
                              .stksz = sizeof(s_task_stack),
                              .bufptr = s_task_stack});
}
static ER create_sem(void) {
  return tk_cre_sem(
      &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
}
static ER create_flag(void) {
  return tk_cre_flg(&(T_CFLG){.exinf = 0, .flgatr = TA_WSGL, .iflgptn = 1});
}
static ER create_mailbox(void) {
  return tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TFIFO});
}
static ER delete_sem(void) { return tk_del_sem(s_obj.semid); }
static ER delete_mailbox(void) { return tk_del_mbx(s_obj.mbxid); }
static ER clear_flag(void) { return tk_clr_flg(s_obj.flgid, 0); }
static ER send_mailbox(void) { return tk_snd_mbx(s_obj.mbxid, &s_message); }

typedef struct {
  const char *name;
  ER (*call)(void);
  BOOL creates;
} api_case_t;

static const api_case_t s_calls[] = {
    {"tk_cre_tsk", create_task, TRUE}, {"tk_cre_sem", create_sem, TRUE},
    {"tk_cre_flg", create_flag, TRUE}, {"tk_cre_mbx", create_mailbox, TRUE},
    {"tk_del_sem", delete_sem, FALSE}, {"tk_del_mbx", delete_mailbox, FALSE},
    {"tk_clr_flg", clear_flag, FALSE}, {"tk_snd_mbx", send_mailbox, FALSE},
};

static void check_effect(UINT api, ER result) {
  switch (api) {
  case 0:
    TEST_ASSERT_EQUAL_UINT(TTS_DMT, tkmc_tcbs[result - 1].tskstat);
    break;
  case 1:
    TEST_ASSERT_EQUAL_UINT(result, tkmc_semcbs[result - 1].semid);
    TEST_ASSERT_EQUAL(E_OK, tk_del_sem(result));
    break;
  case 2:
    TEST_ASSERT_EQUAL_UINT(result, tkmc_flgcbs[result - 1].flgid);
    break;
  case 3:
    TEST_ASSERT_EQUAL_UINT(result, tkmc_mbxcbs[result - 1].mbxid);
    TEST_ASSERT_EQUAL(E_OK, tk_del_mbx(result));
    break;
  case 4:
    TEST_ASSERT_EQUAL(E_NOEXS, tk_sig_sem(s_obj.semid, 1));
    break;
  case 5:
    TEST_ASSERT_EQUAL(E_NOEXS, tk_snd_mbx(s_obj.mbxid, &s_message));
    break;
  case 6:
    TEST_ASSERT_EQUAL_UINT(0, tkmc_flgcbs[s_obj.flgid - 1].flgptn);
    break;
  default: {
    T_MSG *received = NULL;
    TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(s_obj.mbxid, &received, TMO_POL));
    TEST_ASSERT_EQUAL_PTR(&s_message, received);
  } break;
  }
}

static void
test_isr_context_optional_calls_succeed_and_restore_interrupt_state(void) {
  for (UINT api = 0; api < HOST_COUNT(s_calls); ++api) {
    for (UINT irq = 0; irq < 2; ++irq) {
      host_reset();
      fresh();
      const host_context_t entered = host_enter(TRUE, !irq);
      ER result = s_calls[api].call();
      TEST_ASSERT_TRUE_MESSAGE(tkmc_in_isr(), s_calls[api].name);
      TEST_ASSERT_EQUAL_UINT_MESSAGE(irq, host_interrupts_enabled(),
                                     s_calls[api].name);
      TEST_ASSERT_TRUE_MESSAGE(s_calls[api].creates ? result > E_OK
                                                    : result == E_OK,
                               s_calls[api].name);
      host_leave(entered);
      check_effect(api, result);
      host_check_kernel_queues();
    }
  }
}

static void test_tk_snd_mbx_in_isr_keeps_fifo_and_priority_order(void) {
  s_self = host_make_task(LOW_PRIORITY);
  host_set_current(s_self);
  ID fifo = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TFIFO});
  ID priority = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TFIFO | TA_MPRI});
  TEST_ASSERT_GREATER_THAN(0, fifo);
  TEST_ASSERT_GREATER_THAN(0, priority);

  static T_MSG fifo_messages[2];
  static T_MSG_PRI priority_messages[4];
  static const PRI priorities[] = {3, 1, 2, 1};
  for (UINT i = 0; i < 4; ++i) {
    priority_messages[i].msgpri = priorities[i];
  }

  tkmc_isr_depth = 1;
  for (UINT i = 0; i < 2; ++i) {
    TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(fifo, &fifo_messages[i]));
    TEST_ASSERT_TRUE(host_interrupts_enabled());
  }
  for (UINT i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(priority, &priority_messages[i].msgque));
    TEST_ASSERT_TRUE(host_interrupts_enabled());
  }
  TEST_ASSERT_TRUE(tkmc_in_isr());
  tkmc_isr_depth = 0;

  T_MSG *received = NULL;
  for (UINT i = 0; i < 2; ++i) {
    TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(fifo, &received, TMO_POL));
    TEST_ASSERT_EQUAL_PTR(&fifo_messages[i], received);
  }
  const UINT order[] = {1, 3, 2, 0};
  for (UINT i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(priority, &received, TMO_POL));
    TEST_ASSERT_EQUAL_PTR(&priority_messages[order[i]].msgque, received);
  }
  host_check_kernel_queues();
}

static void test_tk_snd_mbx_in_isr_wakes_waiter_and_defers_its_switch(void) {
  ID mbxid = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TFIFO});
  ID receiver = host_make_task(HIGH_PRIORITY);
  host_set_current(receiver);
  T_MSG *received = NULL;
  HOST_EXPECT_WAIT(tk_rcv_mbx(mbxid, &received, TMO_FEVR));

  ID sender = host_make_task(LOW_PRIORITY);
  host_set_current(sender);
  static T_MSG message;
  host_snapshot_t before;
  host_take_snapshot(&before);
  tkmc_isr_depth = 1;
  TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, &message));
  TEST_ASSERT_TRUE(tkmc_in_isr());
  TEST_ASSERT_TRUE(host_interrupts_enabled());
  TEST_ASSERT_EQUAL_PTR(&tkmc_tcbs[sender - 1], current);
  TEST_ASSERT_EQUAL_PTR(&tkmc_tcbs[receiver - 1], next);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tkmc_tcbs[receiver - 1].tskstat);
  TEST_ASSERT_EQUAL(E_OK, tkmc_tcbs[receiver - 1].wupcause);
  TEST_ASSERT_EQUAL_PTR(&message, tkmc_tcbs[receiver - 1].winfo.mailbox.msg);
  host_snapshot_t after;
  host_take_snapshot(&after);
  TEST_ASSERT_EQUAL_UINT(before.switch_requests + 1, after.switch_requests);
  tkmc_isr_depth = 0;
  TEST_ASSERT_FALSE(tkmc_in_isr());
  TEST_ASSERT_EQUAL_PTR(&tkmc_tcbs[receiver - 1], next);
  host_check_kernel_queues();
}

void run_isr_context_tests(void) {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_isr_context_optional_calls_succeed_and_restore_interrupt_state);
  RUN_TEST(test_tk_snd_mbx_in_isr_keeps_fifo_and_priority_order);
  RUN_TEST(test_tk_snd_mbx_in_isr_wakes_waiter_and_defers_its_switch);
}
