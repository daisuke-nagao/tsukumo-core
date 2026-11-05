/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// How long a mailbox call keeps interrupts disabled. The kernel does not bound
// the number of messages a mailbox holds (the application owns their memory),
// so a call whose work grows with that number would hold interrupts off for as
// long as the application queued. The guest's retired-instruction counter shows
// it: with interrupts disabled throughout, the count between two reads around a
// call is the length of the section.
//
// Each case measures one call on a mailbox that holds QUEUED messages and on an
// empty one, and requires the two counts to differ by no more than SLACK. The
// cases name the calls that are bounded; the one send that is not (a TA_MPRI
// message that goes before some, but not all, of the queued messages) is
// described in kernel/mailbox.c and is not measured here.

#include <tk/tkernel.h>
#include <unity.h>

#include "../runner.h"

enum {
  QUEUED = 200,
  // What the call may cost more with QUEUED messages than with none: the
  // comparison with the last message adds about 40 instructions unoptimized.
  // Each message that a loop in the call visits costs several more (28
  // unoptimized), so QUEUED of them are far above this.
  SLACK = 100,
  QUEUED_PRIORITY = 5
};

// QUEUED messages, and one that the measured send takes.
static T_MSG_PRI s_messages[QUEUED + 1];

static ID create_mailbox(ATR attributes) {
  ID mbxid = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = attributes});
  TEST_ASSERT_GREATER_THAN(E_OK, mbxid);
  return mbxid;
}

static void queue_messages(ID mbxid, UINT count, PRI priority) {
  for (UINT i = 0; i < count; ++i) {
    s_messages[i].msgpri = priority;
    TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, &s_messages[i].msgque));
  }
}

// Waits for a tick edge, so that no tick handler runs inside the measurement
// that follows: the next tick is 10 ms, far more than any call here, away.
static void start_on_a_tick_edge(void) {
  TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(1));
}

// The instructions of tk_del_mbx() on a mailbox with `queued` messages.
static UW instructions_to_delete(UINT queued) {
  ID mbxid = create_mailbox(TA_TFIFO | TA_MPRI);
  queue_messages(mbxid, queued, QUEUED_PRIORITY);

  start_on_a_tick_edge();
  UW before = test_instret();
  ER ercd = tk_del_mbx(mbxid);
  UW used = test_instret() - before;

  TEST_ASSERT_EQUAL(E_OK, ercd);
  return used;
}

// The instructions of one tk_snd_mbx() of `priority` to a TA_MPRI mailbox that
// holds `queued` messages of QUEUED_PRIORITY.
static UW instructions_to_send(UINT queued, PRI priority) {
  ID mbxid = create_mailbox(TA_TFIFO | TA_MPRI);
  queue_messages(mbxid, queued, QUEUED_PRIORITY);
  T_MSG_PRI *probe = &s_messages[QUEUED];
  probe->msgpri = priority;

  start_on_a_tick_edge();
  UW before = test_instret();
  ER ercd = tk_snd_mbx(mbxid, &probe->msgque);
  UW used = test_instret() - before;

  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_EQUAL(E_OK, tk_del_mbx(mbxid));
  return used;
}

static void
deleting_a_mailbox_costs_the_same_with_any_number_of_messages(void) {
  UW empty = instructions_to_delete(0);
  UW full = instructions_to_delete(QUEUED);
  TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(0, empty, "minstret does not count");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      empty + SLACK, full, "tk_del_mbx() depends on the messages queued");
}

static void
a_send_behind_the_last_message_costs_the_same_as_into_an_empty_queue(void) {
  UW empty = instructions_to_send(0, QUEUED_PRIORITY);
  UW behind_equal = instructions_to_send(QUEUED, QUEUED_PRIORITY);
  UW behind_lower = instructions_to_send(QUEUED, QUEUED_PRIORITY + 1);
  TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(0, empty, "minstret does not count");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      empty + SLACK, behind_equal,
      "a send of the queue's last priority depends on the messages queued");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      empty + SLACK, behind_lower,
      "a send behind every queued message depends on the messages queued");
}

static void
a_send_ahead_of_every_message_costs_the_same_as_into_an_empty_queue(void) {
  UW empty = instructions_to_send(0, QUEUED_PRIORITY - 1);
  UW ahead = instructions_to_send(QUEUED, QUEUED_PRIORITY - 1);
  TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(0, empty, "minstret does not count");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      empty + SLACK, ahead,
      "a send ahead of every queued message depends on the messages queued");
}

static const test_case_t s_cases[] = {
    {"deleting_a_mailbox_costs_the_same_with_any_number_of_messages",
     deleting_a_mailbox_costs_the_same_with_any_number_of_messages},
    {"a_send_behind_the_last_message_costs_the_same_as_into_an_empty_queue",
     a_send_behind_the_last_message_costs_the_same_as_into_an_empty_queue},
    {"a_send_ahead_of_every_message_costs_the_same_as_into_an_empty_queue",
     a_send_ahead_of_every_message_costs_the_same_as_into_an_empty_queue},
};

TEST_SUITE_MAIN("mailbox-cost", s_cases, 20000)
