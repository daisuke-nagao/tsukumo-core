/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Mailboxes: the attributes tk_cre_mbx() accepts, and the order of the tasks
// that wait for a message.
//
// TA_DSNAME and TA_NODISWAI belong to features the kernel does not have
// (TK_SUPPORT_DSNAME and TK_SUPPORT_DISWAI are FALSE); like the other objects,
// a mailbox refuses them with E_RSATR. tk_cre_mbx() used to accept and ignore
// them.

#include <tk/tkernel.h>
#include <unity.h>

#include "fakes.h"
#include "mailbox.h"
#include "task.h"

enum { HIGH_PRIORITY = 4, LOW_PRIORITY = 5 };

static TCB *tcb_of(ID tskid) { return &tkmc_tcbs[tskid - 1]; }

void test_a_mailbox_refuses_the_attributes_of_features_the_kernel_lacks(void) {
  static const ATR refused[] = {TA_TFIFO | TA_DSNAME, TA_TFIFO | TA_NODISWAI};
  for (UINT i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
    host_snapshot_t before;
    host_take_snapshot(&before);
    TEST_ASSERT_EQUAL(E_RSATR,
                      tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = refused[i]}));
    TEST_ASSERT_TRUE(host_interrupts_enabled());
    host_assert_unchanged(&before, "a refused tk_cre_mbx()");
  }
}

// Three receivers wait on a TA_TPRI mailbox, the one of higher priority in the
// middle. Three messages then go out in the order high, first low, second low.
void test_receivers_of_a_priority_mailbox_queue_by_priority_then_arrival(void) {
  ID mbxid = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TPRI | TA_MFIFO});
  TEST_ASSERT_GREATER_THAN(0, mbxid);
  T_MSG *out[3] = {0};
  const PRI prios[] = {LOW_PRIORITY, HIGH_PRIORITY, LOW_PRIORITY};
  ID receiver[3];
  for (UINT i = 0; i < 3; ++i) {
    receiver[i] = host_make_task(prios[i]);
    host_set_current(receiver[i]);
    HOST_EXPECT_WAIT(tk_rcv_mbx(mbxid, &out[i], TMO_FEVR));
  }
  host_set_current(host_make_task(LOW_PRIORITY + 1));

  static T_MSG messages[3];
  for (UINT i = 0; i < 3; ++i) {
    TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, &messages[i]));
  }
  const ID expected[] = {receiver[1], receiver[0], receiver[2]};
  for (UINT i = 0; i < 3; ++i) {
    TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb_of(expected[i])->tskstat);
    TEST_ASSERT_EQUAL_PTR(&messages[i], tcb_of(expected[i])->winfo.mailbox.msg);
  }
  host_check_kernel_queues();
}
