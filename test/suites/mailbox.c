/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Tests of mailboxes (tk_cre_mbx, tk_snd_mbx, tk_rcv_mbx, tk_del_mbx).
//
// Every case creates and deletes its own mailbox. A case that stops at a failed
// assert leaves its mailbox behind, and the kernel has 8.
//
// The cases that need a waiting receiver run it as a child at priority 2 or 3,
// above the case (4): the receiver is already blocked in tk_rcv_mbx() when
// test_spawn() returns, so the case can send to it or delete the mailbox at
// once. Each receiver reaches test_checkpoint() only after its last assert.

#include <tk/tkernel.h>
#include <unity.h>

#include "../runner.h"

enum { HI_PRIORITY = 2, LO_PRIORITY = 3, RECEIVE_WAIT_MS = 1000 };
_Static_assert(
    LO_PRIORITY < TEST_CASE_PRIORITY,
    "the receivers must preempt the case, to be blocked before it sends");

// The kernel links messages through T_MSG's list. That member must have the
// kernel's own link type, or the kernel would access it through another type.
_Static_assert(_Generic(((T_MSG *)0)->list, tkmc_list_head: 1, default: 0),
               "T_MSG's link must be a tkmc_list_head");

typedef struct {
  T_MSG hdr;
  INT value;
} message_t;

typedef struct {
  T_MSG_PRI hdr;
  INT value;
} priority_message_t;

static ID create_mailbox(ATR attributes) {
  ID mbxid = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = attributes});
  TEST_ASSERT_GREATER_THAN(E_OK, mbxid);
  return mbxid;
}

// Messages leave a FIFO mailbox in the order they were sent.
static void fifo_queue(void) {
  ID mbxid = create_mailbox(TA_TFIFO | TA_MFIFO);
  message_t first = {.value = 1};
  message_t second = {.value = 2};
  T_MSG *received = 0;

  TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, &first.hdr));
  TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, &second.hdr));

  TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(mbxid, &received, TMO_POL));
  TEST_ASSERT_EQUAL_PTR(&first.hdr, received);
  TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(mbxid, &received, TMO_POL));
  TEST_ASSERT_EQUAL_PTR(&second.hdr, received);
  TEST_ASSERT_EQUAL(E_TMOUT, tk_rcv_mbx(mbxid, &received, TMO_POL));

  TEST_ASSERT_EQUAL(E_OK, tk_del_mbx(mbxid));
}

// Messages leave a priority mailbox by message priority (1 is the highest),
// and messages of the same priority leave in the order they were sent. A
// message priority of 0 is refused.
static void priority_queue(void) {
  ID mbxid = create_mailbox(TA_TFIFO | TA_MPRI);
  priority_message_t low = {.hdr.msgpri = 9, .value = 9};
  priority_message_t high1 = {.hdr.msgpri = 1, .value = 1};
  priority_message_t mid = {.hdr.msgpri = 5, .value = 5};
  priority_message_t high2 = {.hdr.msgpri = 1, .value = 11};
  T_MSG *received = 0;

  TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, (T_MSG *)&low.hdr));
  TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, (T_MSG *)&high1.hdr));
  TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, (T_MSG *)&mid.hdr));
  TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, (T_MSG *)&high2.hdr));

  TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(mbxid, &received, TMO_POL));
  TEST_ASSERT_EQUAL_PTR((T_MSG *)&high1.hdr, received);
  TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(mbxid, &received, TMO_POL));
  TEST_ASSERT_EQUAL_PTR((T_MSG *)&high2.hdr, received);
  TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(mbxid, &received, TMO_POL));
  TEST_ASSERT_EQUAL_PTR((T_MSG *)&mid.hdr, received);
  TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(mbxid, &received, TMO_POL));
  TEST_ASSERT_EQUAL_PTR((T_MSG *)&low.hdr, received);

  priority_message_t invalid = {.hdr.msgpri = 0};
  TEST_ASSERT_EQUAL(E_PAR, tk_snd_mbx(mbxid, (T_MSG *)&invalid.hdr));

  TEST_ASSERT_EQUAL(E_OK, tk_del_mbx(mbxid));
}

// What a receiver child needs to know, and the order it finished in.
typedef struct {
  ID mbxid;
  T_MSG *expected;
  TMO timeout_ms;
} receiver_t;

static void receive_expected(void *arg) {
  receiver_t *receiver = arg;
  T_MSG *received = 0;
  TEST_ASSERT_EQUAL(
      E_OK, tk_rcv_mbx(receiver->mbxid, &received, receiver->timeout_ms));
  TEST_ASSERT_EQUAL_PTR(receiver->expected, received);
  test_checkpoint();
}

// A message sent to a mailbox with a waiting receiver goes straight to it.
static message_t g_direct_message = {.value = 100};
static receiver_t g_direct_receiver;

static void direct_delivery(void) {
  ID mbxid = create_mailbox(TA_TFIFO | TA_MFIFO);
  g_direct_receiver =
      (receiver_t){mbxid, &g_direct_message.hdr, RECEIVE_WAIT_MS};

  test_spawn(receive_expected, HI_PRIORITY, &g_direct_receiver);
  TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, &g_direct_message.hdr));
  test_join_expect(1);

  TEST_ASSERT_EQUAL(E_OK, tk_del_mbx(mbxid));
}

// Two receivers wait together: the "lo" one (priority 3) first, then the "hi"
// one (priority 2). With TA_TFIFO the first to wait is served first; with
// TA_TPRI the higher priority task is. Each receiver notes when it finished.
typedef struct {
  ID mbxid;
  T_MSG *expected;
  INT who;
  volatile INT *order;
  volatile UINT *order_count;
} ordered_receiver_t;

static void receive_and_note_order(void *arg) {
  ordered_receiver_t *receiver = arg;
  T_MSG *received = 0;
  TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(receiver->mbxid, &received, TMO_FEVR));
  TEST_ASSERT_EQUAL_PTR(receiver->expected, received);
  receiver
      ->order[__atomic_fetch_add(receiver->order_count, 1, __ATOMIC_SEQ_CST)] =
      receiver->who;
  test_checkpoint();
}

enum { LO = 1, HI = 2 };

static message_t g_order_message[2] = {{.value = 1}, {.value = 2}};
static volatile INT g_order[2];
static volatile UINT g_order_count;
static ordered_receiver_t g_lo_receiver;
static ordered_receiver_t g_hi_receiver;

static void run_wait_order(ATR attributes, T_MSG *lo_expected,
                           T_MSG *hi_expected, INT first, INT second) {
  ID mbxid = create_mailbox(attributes);
  g_order[0] = g_order[1] = 0;
  g_order_count = 0;
  g_lo_receiver =
      (ordered_receiver_t){mbxid, lo_expected, LO, g_order, &g_order_count};
  g_hi_receiver =
      (ordered_receiver_t){mbxid, hi_expected, HI, g_order, &g_order_count};

  test_spawn(receive_and_note_order, LO_PRIORITY, &g_lo_receiver);
  test_spawn(receive_and_note_order, HI_PRIORITY, &g_hi_receiver);

  TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, &g_order_message[0].hdr));
  TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, &g_order_message[1].hdr));
  test_join_expect(2);

  TEST_ASSERT_EQUAL(2, g_order_count);
  TEST_ASSERT_EQUAL(first, g_order[0]);
  TEST_ASSERT_EQUAL(second, g_order[1]);

  TEST_ASSERT_EQUAL(E_OK, tk_del_mbx(mbxid));
}

static void wait_queue_fifo(void) {
  run_wait_order(TA_TFIFO | TA_MFIFO, &g_order_message[0].hdr,
                 &g_order_message[1].hdr, LO, HI);
}

static void wait_queue_priority(void) {
  run_wait_order(TA_TPRI | TA_MFIFO, &g_order_message[1].hdr,
                 &g_order_message[0].hdr, HI, LO);
}

// Deleting a mailbox releases a waiting receiver with E_DLT.
static void receive_deleted(void *arg) {
  T_MSG *received = 0;
  TEST_ASSERT_EQUAL(E_DLT, tk_rcv_mbx((ID)(INT)arg, &received, TMO_FEVR));
  test_checkpoint();
}

static void delete_releases_waiter(void) {
  ID mbxid = create_mailbox(TA_TFIFO | TA_MFIFO);

  test_spawn(receive_deleted, HI_PRIORITY, (void *)(INT)mbxid);
  TEST_ASSERT_EQUAL(E_OK, tk_del_mbx(mbxid));
  test_join_expect(1);
}

// A receiver that times out leaves nothing behind: a message sent afterwards
// is not lost to it, and the mailbox still works.
static void receive_times_out(void *arg) {
  // A receive that fails must leave the pointer alone.
  T_MSG untouched;
  T_MSG *received = &untouched;
  TEST_ASSERT_EQUAL(E_TMOUT, tk_rcv_mbx((ID)(INT)arg, &received, 50));
  TEST_ASSERT_EQUAL_PTR(&untouched, received);
  test_checkpoint();
}

static void timeout_leaves_no_waiter(void) {
  ID mbxid = create_mailbox(TA_TFIFO | TA_MFIFO);
  message_t message = {.value = 77};
  T_MSG *received = 0;

  test_spawn(receive_times_out, HI_PRIORITY, (void *)(INT)mbxid);
  test_join_expect(1);

  TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, &message.hdr));
  TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(mbxid, &received, TMO_POL));
  TEST_ASSERT_EQUAL_PTR(&message.hdr, received);

  TEST_ASSERT_EQUAL(E_OK, tk_del_mbx(mbxid));
}

static const test_case_t s_cases[] = {
    {"fifo_queue", fifo_queue},
    {"priority_queue", priority_queue},
    {"direct_delivery", direct_delivery},
    {"wait_queue_fifo", wait_queue_fifo},
    {"wait_queue_priority", wait_queue_priority},
    {"delete_releases_waiter", delete_releases_waiter},
    {"timeout_leaves_no_waiter", timeout_leaves_no_waiter},
};

TEST_SUITE_MAIN("mailbox", s_cases, 5000)
