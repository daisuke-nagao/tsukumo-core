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
// The calls whose work does not depend on the messages queued each measure one
// call on a mailbox that holds QUEUED messages and on an empty one, and
// require the two counts to differ by no more than SLACK.
//
// The one send whose work does (a TA_MPRI message that goes before some, but
// not all, of the queued messages; see insert_msg_pri() in kernel/mailbox.c)
// is held to a budget instead: with QUEUED messages queued it must take at most
// BUDGET_INSTRUCTIONS. QUEUED is the most messages per mailbox for which the
// kernel promises that; it is a promise, not a limit, because the specification
// puts none on the messages a mailbox can queue. See
// docs/developer/notes/mailbox-di-budget.md.
//
// Every send is followed by a drain of the mailbox, which checks that the
// message went where the case means it to. A case that measured a send that
// took another path would otherwise pass for the wrong reason.

#include <tk/tkernel.h>
#include <unity.h>

#include "../runner.h"

enum {
  QUEUED = 256,
  // What the call may cost more with queued messages than with none: the
  // comparison with the last message adds 16 instructions unoptimized, and a
  // send ahead of every message also visits one message, 41 in all. Each
  // further message that a loop in the call visits costs 27 more, so QUEUED of
  // them are far above this.
  SLACK = 100,
  // The most instructions the send into the middle of the queue may take: a
  // tenth of a kernel tick (10 ms) at -icount shift 5, which holds 312500
  // instructions, the same budget as the timer handler's in qemu-timer-cost.
  // It is a count, not a share of a tick, so it does not follow the shift.
  BUDGET_INSTRUCTIONS = 31250,
  // Queued messages are of QUEUED_PRIORITY, then of BEHIND_PRIORITY. A message
  // of BETWEEN_PRIORITY goes after the first and before the second; a message
  // of AHEAD_PRIORITY goes before all of them.
  AHEAD_PRIORITY = 4,
  QUEUED_PRIORITY = 5,
  BETWEEN_PRIORITY = 6,
  BEHIND_PRIORITY = 7
};

// QUEUED messages, and one that the measured send takes.
static T_MSG_PRI s_messages[QUEUED + 1];

static ID create_mailbox(ATR attributes) {
  ID mbxid = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = attributes});
  TEST_ASSERT_GREATER_THAN(E_OK, mbxid);
  return mbxid;
}

// Queues `count` messages: the first `high` of QUEUED_PRIORITY, the others of
// BEHIND_PRIORITY. Each goes behind or at the end of the queue, so none scans.
static void queue_messages(ID mbxid, UINT count, UINT high) {
  for (UINT i = 0; i < count; ++i) {
    s_messages[i].msgpri = i < high ? QUEUED_PRIORITY : BEHIND_PRIORITY;
    TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, &s_messages[i].msgque));
  }
}

// Receives every message of the mailbox and checks that `probe` came out
// `expected_index`th, among `count` messages in all.
static void assert_probe_was_queued_at(ID mbxid, const T_MSG *probe, UINT count,
                                       UINT expected_index) {
  UINT received = 0;
  UINT found_at = count;
  T_MSG *msg = NULL;

  while (received <= count && tk_rcv_mbx(mbxid, &msg, TMO_POL) == E_OK) {
    if (msg == probe) {
      found_at = received;
    }
    ++received;
  }
  TEST_ASSERT_EQUAL_UINT_MESSAGE(count, received,
                                 "the mailbox did not hold every message");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(expected_index, found_at,
                                 "the measured send went to another position");
}

// Waits for a tick edge, so that no tick handler runs inside the measurement
// that follows: the next tick is 10 ms, far more than any call here, away.
static void start_on_a_tick_edge(void) {
  TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(1));
}

// The instructions of tk_del_mbx() on a mailbox with `queued` messages.
static UW instructions_to_delete(UINT queued) {
  ID mbxid = create_mailbox(TA_TFIFO | TA_MPRI);
  queue_messages(mbxid, queued, queued);

  start_on_a_tick_edge();
  UW before = test_instret();
  ER ercd = tk_del_mbx(mbxid);
  UW used = test_instret() - before;

  TEST_ASSERT_EQUAL(E_OK, ercd);
  return used;
}

// The instructions of one tk_snd_mbx() of `priority` to a TA_MPRI mailbox that
// holds `queued` messages, `high` of them of QUEUED_PRIORITY and the others of
// BEHIND_PRIORITY. The message must end up `expected_index`th in the queue.
static UW instructions_to_send(UINT queued, UINT high, PRI priority,
                               UINT expected_index) {
  ID mbxid = create_mailbox(TA_TFIFO | TA_MPRI);
  queue_messages(mbxid, queued, high);
  T_MSG_PRI *probe = &s_messages[QUEUED];
  probe->msgpri = priority;

  start_on_a_tick_edge();
  UW before = test_instret();
  ER ercd = tk_snd_mbx(mbxid, &probe->msgque);
  UW used = test_instret() - before;

  TEST_ASSERT_EQUAL(E_OK, ercd);
  assert_probe_was_queued_at(mbxid, &probe->msgque, queued + 1, expected_index);
  TEST_ASSERT_EQUAL(E_OK, tk_del_mbx(mbxid));
  return used;
}

static void report(const char *label, UINT queued, UW used) {
  UnityPrint("mailbox-cost: ");
  UnityPrint(label);
  UnityPrint(": with ");
  UnityPrintNumberUnsigned(queued);
  UnityPrint(" queued messages the send took ");
  UnityPrintNumberUnsigned(used);
  UnityPrint(" of ");
  UnityPrintNumberUnsigned(BUDGET_INSTRUCTIONS);
  UnityPrint(" budgeted instructions");
  UNITY_PRINT_EOL();
}

// Prints how much more than an empty mailbox a call took with `queued`
// messages, which is the margin the check against SLACK leaves.
static void report_extra(const char *label, UINT queued, UW empty, UW used) {
  UnityPrint("mailbox-cost: ");
  UnityPrint(label);
  UnityPrint(": with ");
  UnityPrintNumberUnsigned(queued);
  UnityPrint(" queued messages the call took ");
  UnityPrintNumberUnsigned(used > empty ? used - empty : 0);
  UnityPrint(" more instructions than on an empty mailbox, of ");
  UnityPrintNumberUnsigned(SLACK);
  UnityPrint(" allowed");
  UNITY_PRINT_EOL();
}

static void
deleting_a_mailbox_costs_the_same_with_any_number_of_messages(void) {
  UW empty = instructions_to_delete(0);
  UW full = instructions_to_delete(QUEUED);
  report_extra("tk_del_mbx", QUEUED, empty, full);
  TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(0, empty, "minstret does not count");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      empty + SLACK, full, "tk_del_mbx() depends on the messages queued");
}

static void
a_send_behind_the_last_message_costs_the_same_as_into_an_empty_queue(void) {
  UW empty = instructions_to_send(0, 0, QUEUED_PRIORITY, 0);
  UW behind_one = instructions_to_send(1, 1, QUEUED_PRIORITY, 1);
  UW behind_equal =
      instructions_to_send(QUEUED, QUEUED, QUEUED_PRIORITY, QUEUED);
  UW behind_lower =
      instructions_to_send(QUEUED, QUEUED, BETWEEN_PRIORITY, QUEUED);
  report_extra("send behind the last", 1, empty, behind_one);
  report_extra("send behind the last", QUEUED, empty, behind_equal);
  report_extra("send behind the last, of a lower priority", QUEUED, empty,
               behind_lower);
  TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(0, empty, "minstret does not count");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      empty + SLACK, behind_one,
      "a send behind the only queued message depends on the messages queued");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      empty + SLACK, behind_equal,
      "a send of the queue's last priority depends on the messages queued");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      empty + SLACK, behind_lower,
      "a send behind every queued message depends on the messages queued");
}

static void
a_send_ahead_of_every_message_costs_the_same_as_into_an_empty_queue(void) {
  UW empty = instructions_to_send(0, 0, AHEAD_PRIORITY, 0);
  UW ahead_one = instructions_to_send(1, 1, AHEAD_PRIORITY, 0);
  UW ahead = instructions_to_send(QUEUED, QUEUED, AHEAD_PRIORITY, 0);
  report_extra("send ahead of every message", 1, empty, ahead_one);
  report_extra("send ahead of every message", QUEUED, empty, ahead);
  TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(0, empty, "minstret does not count");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      empty + SLACK, ahead_one,
      "a send ahead of the only queued message depends on the messages queued");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      empty + SLACK, ahead,
      "a send ahead of every queued message depends on the messages queued");
}

// The send that scans the queue: half of the messages are ahead of it.
static void
a_send_into_the_middle_of_a_full_queue_stays_within_the_budget(void) {
  UW used =
      instructions_to_send(QUEUED, QUEUED / 2, BETWEEN_PRIORITY, QUEUED / 2);
  report("send into the middle", QUEUED, used);
  TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(0, used, "minstret does not count");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      BUDGET_INSTRUCTIONS, used,
      "a send into the middle of a full queue is over the budget");
}

// The worst send: it goes before the last message and behind all the others,
// so the scan visits every queued message. One queued message and QUEUED give
// the cost of one more message, which the report line shows.
static void
a_send_before_the_last_of_a_full_queue_stays_within_the_budget(void) {
  UW one = instructions_to_send(1, 0, BETWEEN_PRIORITY, 0);
  UW used =
      instructions_to_send(QUEUED, QUEUED - 1, BETWEEN_PRIORITY, QUEUED - 1);
  report("send before the last message", 1, one);
  report("send before the last message", QUEUED, used);
  TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(0, one, "minstret does not count");
  TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
      one, used, "the scan did not grow with the messages queued");
  UnityPrint("mailbox-cost: each queued message the scan visits took about ");
  UnityPrintNumberUnsigned((used - one) / (QUEUED - 1));
  UnityPrint(" instructions");
  UNITY_PRINT_EOL();
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      BUDGET_INSTRUCTIONS, used,
      "a send before the last message of a full queue is over the budget");
}

static const test_case_t s_cases[] = {
    {"deleting_a_mailbox_costs_the_same_with_any_number_of_messages",
     deleting_a_mailbox_costs_the_same_with_any_number_of_messages},
    {"a_send_behind_the_last_message_costs_the_same_as_into_an_empty_queue",
     a_send_behind_the_last_message_costs_the_same_as_into_an_empty_queue},
    {"a_send_ahead_of_every_message_costs_the_same_as_into_an_empty_queue",
     a_send_ahead_of_every_message_costs_the_same_as_into_an_empty_queue},
    {"a_send_into_the_middle_of_a_full_queue_stays_within_the_budget",
     a_send_into_the_middle_of_a_full_queue_stays_within_the_budget},
    {"a_send_before_the_last_of_a_full_queue_stays_within_the_budget",
     a_send_before_the_last_of_a_full_queue_stays_within_the_budget},
};

TEST_SUITE_MAIN("mailbox-cost", s_cases, 20000)
