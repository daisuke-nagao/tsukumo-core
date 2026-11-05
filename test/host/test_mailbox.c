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

#include <stdio.h>

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

// The order of the messages of a TA_MPRI mailbox: ascending msgpri (1 is the
// highest priority), and in the order they were sent among equal msgpri. A
// message that goes at the tail takes a shortcut of its own, so the sequences
// mix runs of repeated and rising priorities, which take it, with random ones,
// which go between queued messages.
enum { MAX_MESSAGES = 40, PRIORITIES = 5, SEQUENCES = 300 };

// A fixed generator, so a failing run can be reproduced from the seed.
static UINT next_random(UINT *state) {
  *state = *state * 1664525u + 1013904223u;
  return *state >> 8;
}

void test_a_priority_mailbox_delivers_by_priority_then_arrival(void) {
  host_set_current(host_make_task(LOW_PRIORITY));
  static T_MSG_PRI messages[MAX_MESSAGES];

  for (UINT seed = 1; seed <= SEQUENCES; ++seed) {
    char where[32];
    snprintf(where, sizeof(where), "seed %u", seed);
    UINT state = seed;
    ID mbxid = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TFIFO | TA_MPRI});
    TEST_ASSERT_GREATER_THAN(0, mbxid);

    UINT length = next_random(&state) % (MAX_MESSAGES + 1);
    PRI previous = 1;
    for (UINT i = 0; i < length; ++i) {
      PRI priority = (PRI)(next_random(&state) % PRIORITIES + 1);
      if (next_random(&state) % 2 != 0) {
        priority = previous + (PRI)(next_random(&state) % 2);
        priority = priority > PRIORITIES ? PRIORITIES : priority;
      }
      messages[i].msgpri = priority;
      previous = priority;
      TEST_ASSERT_EQUAL_MESSAGE(E_OK, tk_snd_mbx(mbxid, &messages[i].msgque),
                                where);
    }

    // The model: stable by priority.
    for (PRI priority = 1; priority <= PRIORITIES; ++priority) {
      for (UINT i = 0; i < length; ++i) {
        if (messages[i].msgpri != priority) {
          continue;
        }
        T_MSG *received = NULL;
        TEST_ASSERT_EQUAL_MESSAGE(E_OK, tk_rcv_mbx(mbxid, &received, TMO_POL),
                                  where);
        TEST_ASSERT_EQUAL_PTR_MESSAGE(&messages[i].msgque, received, where);
      }
    }
    T_MSG *none = NULL;
    TEST_ASSERT_EQUAL_MESSAGE(E_TMOUT, tk_rcv_mbx(mbxid, &none, TMO_POL),
                              where);
    TEST_ASSERT_EQUAL_MESSAGE(E_OK, tk_del_mbx(mbxid), where);
  }
}
