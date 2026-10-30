/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// The counts of a semaphore: tk_cre_sem() takes a maximum of 1 to 32767 and an
// initial count of 0 to the maximum, and tk_sig_sem() refuses to take the count
// past the maximum. The kernel used to store any counts it was given, and to
// skip the limit check for a signal larger than the maximum, so a semaphore of
// one unit could hold two and a signal of INT_MAX overflowed the count.
//
// A refusal is checked to change nothing the kernel keeps (host_snapshot_t),
// and the count a semaphore holds is read the way a task would: by taking units
// without waiting until none is left. The tests of the order of the waiters,
// below, read it from the control block instead.
//
// The limits are written as numbers, not as TK_SEMAPHORE_MAXCNT, so that a
// wrong value in the profile fails here.

#include <limits.h>
#include <stdio.h>

#include <tk/tkernel.h>
#include <unity.h>

#include "fakes.h"
#include "semaphore.h"
#include "task.h"
#include "timer.h"

enum { HIGH_PRIORITY = 4, LOW_PRIORITY = 5, LIMIT = 32767 };

static T_CSEM packet(INT isemcnt, INT maxsem) {
  return (T_CSEM){
      .exinf = 0, .sematr = TA_TFIFO, .isemcnt = isemcnt, .maxsem = maxsem};
}

static ID create_semaphore(INT isemcnt, INT maxsem) {
  T_CSEM pk = packet(isemcnt, maxsem);
  ID semid = tk_cre_sem(&pk);
  TEST_ASSERT_GREATER_THAN(0, semid);
  return semid;
}

// Fails unless the semaphore holds exactly count units.
static void check_count(ID semid, INT count) {
  for (INT i = 0; i < count; ++i) {
    TEST_ASSERT_EQUAL(E_OK, tk_wai_sem(semid, 1, TMO_POL));
  }
  TEST_ASSERT_EQUAL(E_TMOUT, tk_wai_sem(semid, 1, TMO_POL));
}

// Calls tk_cre_sem() with the packet and checks the refusal: the error code,
// interrupts back on, and nothing the kernel keeps changed (the free list of
// semaphores runs through the control blocks, so taking one would show).
static void check_create_refused(const T_CSEM *pk, ER expected) {
  host_snapshot_t before;
  host_take_snapshot(&before);

  TEST_ASSERT_EQUAL(expected, tk_cre_sem(pk));
  TEST_ASSERT_TRUE(host_interrupts_enabled());
  host_assert_unchanged(&before, "a refused tk_cre_sem()");
  host_check_kernel_queues();
}

// Calls tk_sig_sem() and checks the refusal in the same way.
static void check_signal_refused(ID semid, INT cnt, ER expected) {
  host_snapshot_t before;
  host_take_snapshot(&before);

  TEST_ASSERT_EQUAL(expected, tk_sig_sem(semid, cnt));
  TEST_ASSERT_TRUE(host_interrupts_enabled());
  host_assert_unchanged(&before, "a refused tk_sig_sem()");
  host_check_kernel_queues();
}

typedef struct {
  INT isemcnt;
  INT maxsem;
} counts_t;

static const counts_t bad_counts[] = {
    {-1, 1},        // a negative initial count
    {0, 0},         // a maximum of zero
    {0, -1},        // a negative maximum
    {-1, -2},       // both negative
    {0, 32768},     // a maximum above the limit
    {0, INT_MAX},   // the largest maximum
    {2, 1},         // an initial count above the maximum
    {32768, 32768}, // both above the limit
};

enum { BAD_COUNTS = sizeof(bad_counts) / sizeof(bad_counts[0]) };

void test_a_semaphore_count_outside_its_range_is_refused(void) {
  for (UINT i = 0; i < BAD_COUNTS; ++i) {
    T_CSEM pk = packet(bad_counts[i].isemcnt, bad_counts[i].maxsem);
    check_create_refused(&pk, E_PAR);
  }
}

void test_an_attribute_error_comes_before_a_count_error(void) {
  for (UINT i = 0; i < BAD_COUNTS; ++i) {
    T_CSEM pk = packet(bad_counts[i].isemcnt, bad_counts[i].maxsem);
    pk.sematr = TA_TPRI | TA_DSNAME; // a bit the kernel does not support
    check_create_refused(&pk, E_RSATR);
  }
}

void test_a_count_error_comes_before_running_out_of_semaphores(void) {
  for (UINT i = 0; i < CFN_MAX_SEMID; ++i) {
    create_semaphore(0, 1);
  }
  for (UINT i = 0; i < BAD_COUNTS; ++i) {
    T_CSEM pk = packet(bad_counts[i].isemcnt, bad_counts[i].maxsem);
    check_create_refused(&pk, E_PAR);
  }
  T_CSEM good = packet(0, 1);
  check_create_refused(&good, E_LIMIT);
}

void test_refused_creations_use_up_no_semaphore(void) {
  for (UINT i = 0; i < BAD_COUNTS; ++i) {
    T_CSEM pk = packet(bad_counts[i].isemcnt, bad_counts[i].maxsem);
    TEST_ASSERT_EQUAL(E_PAR, tk_cre_sem(&pk));
  }
  // The first semaphore after a boot is still ID 1, and all of them are left.
  TEST_ASSERT_EQUAL(1, create_semaphore(0, 1));
  for (UINT i = 1; i < CFN_MAX_SEMID; ++i) {
    create_semaphore(0, 1);
  }
  T_CSEM good = packet(0, 1);
  TEST_ASSERT_EQUAL(E_LIMIT, tk_cre_sem(&good));
}

// Each accepted pair starts with its initial count, takes signals up to its
// maximum and refuses one more.
void test_the_counts_at_both_ends_of_the_range_are_accepted(void) {
  static const counts_t good[] = {
      {0, 1}, {1, 1}, {0, LIMIT}, {LIMIT, LIMIT}, {1, 3}};
  for (UINT i = 0; i < sizeof(good) / sizeof(good[0]); ++i) {
    ID semid = create_semaphore(good[i].isemcnt, good[i].maxsem);
    check_count(semid, good[i].isemcnt);

    TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, good[i].maxsem));
    check_signal_refused(semid, 1, E_QOVR);
    check_count(semid, good[i].maxsem);
    TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  }
}

void test_a_signal_past_the_maximum_is_refused_and_keeps_the_count(void) {
  static const struct {
    INT isemcnt;
    INT maxsem;
    INT cnt;
    ER expected;
  } rows[] = {
      {1, 1, 1, E_QOVR},           // one over a full semaphore
      {0, 1, 2, E_QOVR},           // more than the maximum at once
      {0, 3, 4, E_QOVR},           // more than the maximum, above one
      {2, 3, 2, E_QOVR},           // one over, from part full
      {1, 1, INT_MAX, E_QOVR},     // a sum that overflows
      {0, LIMIT, INT_MAX, E_QOVR}, // the largest count, from empty
      {0, 1, 0, E_PAR},            // no unit
      {0, 1, -1, E_PAR},           // a negative count
      {0, 1, INT_MIN, E_PAR},      // the most negative count
  };
  for (UINT i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
    ID semid = create_semaphore(rows[i].isemcnt, rows[i].maxsem);
    check_signal_refused(semid, rows[i].cnt, rows[i].expected);
    check_count(semid, rows[i].isemcnt);
    TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  }
}

// A task that waits for units of the semaphore, and another one to run.
static ID make_waiter(ID semid, INT cnt, ID *other) {
  ID waiter = host_make_task(HIGH_PRIORITY);
  *other = host_make_task(LOW_PRIORITY);
  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, cnt, TMO_FEVR));
  host_set_current(*other);
  return waiter;
}

void test_a_signal_past_the_maximum_wakes_no_waiter(void) {
  ID semid = create_semaphore(0, 1);
  ID other;
  ID waiter = make_waiter(semid, 1, &other);

  check_signal_refused(semid, 2, E_QOVR);
  check_signal_refused(semid, INT_MAX, E_QOVR);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tkmc_tcbs[waiter - 1].tskstat);
}

// The maximum bounds the count before any of it is given to the waiters, even
// units a waiter would take at once.
void test_the_maximum_counts_units_before_they_go_to_waiters(void) {
  ID semid = tk_cre_sem(&(T_CSEM){
      .exinf = 0, .sematr = TA_TFIFO | TA_FIRST, .isemcnt = 2, .maxsem = 3});
  TEST_ASSERT_GREATER_THAN(0, semid);
  ID other;
  ID waiter = make_waiter(semid, 3, &other);

  check_signal_refused(semid, 2, E_QOVR);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tkmc_tcbs[waiter - 1].tskstat);

  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tkmc_tcbs[waiter - 1].tskstat);
  host_check_kernel_queues();
  check_count(semid, 0);
}

void test_a_signal_refused_with_interrupts_off_leaves_them_off(void) {
  ID semid = create_semaphore(1, 1);
  host_snapshot_t before;
  host_take_snapshot(&before);

  UINT saved;
  DI(saved);
  ER ercd = tk_sig_sem(semid, 1);
  BOOL still_off = !host_interrupts_enabled();
  EI(saved);

  TEST_ASSERT_EQUAL(E_QOVR, ercd);
  TEST_ASSERT_TRUE(still_off);
  host_assert_unchanged(&before, "a refused tk_sig_sem() with interrupts off");
  check_count(semid, 1);
}

// The order a new request takes units in. A request used to take units
// whenever enough were left, ahead of the tasks that already waited for them,
// so on a TA_FIRST semaphore a small late request could pass the large one at
// the head of the queue again and again. Now a request that finds waiters on a
// TA_FIRST semaphore takes units at once only under TA_TPRI and only when it
// outranks the head, as it would be queued ahead of the head; under TA_CNT any
// request that finds enough units takes them, as before.
//
// The count is read from the control block: once a task waits on a TA_FIRST
// semaphore, a poll is refused whatever the count, so check_count() can no
// longer read it.

enum { WAIT_MS = 50 };

typedef enum { TAKES, REFUSED } order_t;

typedef struct {
  ATR sematr;
  PRI priority; // of the new request; the head waits at HIGH_PRIORITY
  INT left;     // units left while the head waits for two
  order_t expected;
} order_row_t;

static const order_row_t order_rows[] = {
    {TA_TFIFO | TA_FIRST, HIGH_PRIORITY - 1, 1, REFUSED},
    {TA_TFIFO | TA_FIRST, HIGH_PRIORITY, 1, REFUSED},
    {TA_TFIFO | TA_FIRST, LOW_PRIORITY, 1, REFUSED},
    {TA_TPRI | TA_FIRST, HIGH_PRIORITY - 1, 1, TAKES},
    {TA_TPRI | TA_FIRST, HIGH_PRIORITY, 1, REFUSED},
    {TA_TPRI | TA_FIRST, LOW_PRIORITY, 1, REFUSED},
    {TA_TFIFO | TA_CNT, HIGH_PRIORITY - 1, 1, TAKES},
    {TA_TFIFO | TA_CNT, HIGH_PRIORITY, 1, TAKES},
    {TA_TFIFO | TA_CNT, LOW_PRIORITY, 1, TAKES},
    {TA_TPRI | TA_CNT, HIGH_PRIORITY - 1, 1, TAKES},
    {TA_TPRI | TA_CNT, HIGH_PRIORITY, 1, TAKES},
    {TA_TPRI | TA_CNT, LOW_PRIORITY, 1, TAKES},
    // With no unit left, not even a request that may go first takes one.
    {TA_TPRI | TA_FIRST, HIGH_PRIORITY - 1, 0, REFUSED},
    {TA_TFIFO | TA_CNT, HIGH_PRIORITY - 1, 0, REFUSED},
    {TA_TPRI | TA_CNT, HIGH_PRIORITY - 1, 0, REFUSED},
};

static UINT count_in_queue(const tkmc_list_head *head) {
  UINT n = 0;
  for (const tkmc_list_head *pos = head->next; pos != head; pos = pos->next) {
    ++n;
  }
  return n;
}

static BOOL in_queue(const tkmc_list_head *head, const tkmc_list_head *node) {
  for (const tkmc_list_head *pos = head->next; pos != head; pos = pos->next) {
    if (pos == node) {
      return TRUE;
    }
  }
  return FALSE;
}

static TCB *first_waiter(SEMCB *semcb) {
  return tkmc_list_first_entry(&semcb->wait_queue, TCB, winfo.wait_queue);
}

static TCB *last_waiter(SEMCB *semcb) {
  return tkmc_list_entry(semcb->wait_queue.prev, TCB, winfo.wait_queue);
}

static const char *attribute_name(ATR sematr) {
  switch (sematr) {
  case TA_TFIFO | TA_FIRST:
    return "TA_TFIFO | TA_FIRST";
  case TA_TPRI | TA_FIRST:
    return "TA_TPRI | TA_FIRST";
  case TA_TFIFO | TA_CNT:
    return "TA_TFIFO | TA_CNT";
  default:
    return "TA_TPRI | TA_CNT";
  }
}

// A semaphore of two units with the row's attribute, on which a task waits for
// both while row->left of them are there, and a new task at the row's priority,
// running, to make the request. The waiting task goes to *head and the new one
// to *late.
static ID head_waits_for_two(const order_row_t *row, ID *head, ID *late) {
  ID semid = tk_cre_sem(
      &(T_CSEM){.exinf = 0, .sematr = row->sematr, .isemcnt = 0, .maxsem = 2});
  TEST_ASSERT_GREATER_THAN(0, semid);
  ID other;
  *head = make_waiter(semid, 2, &other);
  if (row->left > 0) {
    TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, row->left));
  }
  TEST_ASSERT_EQUAL(row->left, tkmc_semcbs[semid - 1].semcnt);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tkmc_tcbs[*head - 1].tskstat);
  *late = host_make_task(row->priority);
  host_set_current(*late);
  return semid;
}

// After the late request took its unit at once: the count is down by one (the
// return code alone would not show a request that waited, as the PC does not
// switch it out without HOST_EXPECT_WAIT), and the head still waits, alone.
static void check_took_one(ID semid, ID head, const char *label) {
  SEMCB *semcb = &tkmc_semcbs[semid - 1];
  TEST_ASSERT_EQUAL_MESSAGE(0, semcb->semcnt, label);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, count_in_queue(&semcb->wait_queue), label);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(&tkmc_tcbs[head - 1], first_waiter(semcb),
                                label);
  TEST_ASSERT_TRUE_MESSAGE(host_interrupts_enabled(), label);
  host_check_kernel_queues();
}

static void check_poll(const order_row_t *row, const char *label) {
  ID head, late;
  ID semid = head_waits_for_two(row, &head, &late);
  host_snapshot_t before;
  host_take_snapshot(&before);

  ER ercd = tk_wai_sem(semid, 1, TMO_POL);
  if (row->expected == REFUSED) {
    TEST_ASSERT_EQUAL_MESSAGE(E_TMOUT, ercd, label);
    TEST_ASSERT_TRUE_MESSAGE(host_interrupts_enabled(), label);
    host_assert_unchanged(&before, label);
    return;
  }
  TEST_ASSERT_EQUAL_MESSAGE(E_OK, ercd, label);
  check_took_one(semid, head, label);
  // The unit is all the request changed. Giving it back asks for no switch
  // only while the late task is the highest priority ready one: the task that
  // make_waiter() left ready runs at LOW_PRIORITY and was queued first.
  if (row->priority < LOW_PRIORITY) {
    TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
    host_assert_unchanged(&before, label);
  }
}

static void check_timed_wait(const order_row_t *row, const char *label) {
  ID head, late;
  ID semid = head_waits_for_two(row, &head, &late);
  SEMCB *semcb = &tkmc_semcbs[semid - 1];

  if (row->expected == TAKES) {
    TEST_ASSERT_EQUAL_MESSAGE(E_OK, tk_wai_sem(semid, 1, WAIT_MS), label);
    check_took_one(semid, head, label);
    return;
  }
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, WAIT_MS));
  // The late task waits once, in its place in the queue (ahead of the head
  // only under TA_TPRI with the higher priority), with its time limit, and
  // leaves the units it found.
  TCB *tcb = &tkmc_tcbs[late - 1];
  BOOL goes_first =
      (row->sematr & TA_TPRI) != 0 && row->priority < HIGH_PRIORITY;
  TEST_ASSERT_EQUAL_MESSAGE(row->left, semcb->semcnt, label);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(2, count_in_queue(&semcb->wait_queue), label);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(goes_first ? tcb : &tkmc_tcbs[head - 1],
                                first_waiter(semcb), label);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(goes_first ? &tkmc_tcbs[head - 1] : tcb,
                                last_waiter(semcb), label);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(TTW_SEM, tcb->tskwait, label);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(tkmc_ms_to_ticks(WAIT_MS), tcb->delay_ticks,
                                 label);
  TEST_ASSERT_TRUE_MESSAGE(in_queue(&tkmc_timer_queue, &tcb->head), label);
  host_check_kernel_queues();
}

// A poll the order refuses is E_TMOUT and changes nothing; a request with a
// time limit the order refuses waits in the queue. A request the order allows
// takes its unit either way.
void test_a_new_request_takes_units_only_in_the_order_of_the_semaphore(void) {
  for (UINT i = 0; i < sizeof(order_rows) / sizeof(order_rows[0]); ++i) {
    const order_row_t *row = &order_rows[i];
    char label[96];
    snprintf(label, sizeof(label), "%s, priority %d against %d, %d left",
             attribute_name(row->sematr), (int)row->priority,
             (int)HIGH_PRIORITY, (int)row->left);

    host_reset();
    check_poll(row, label);
    host_reset();
    check_timed_wait(row, label);
  }
}

// A request that waits for ever is queued in the same way, with no time limit.
void test_a_request_refused_by_the_order_waits_for_ever_without_a_timer(void) {
  const order_row_t row = {TA_TFIFO | TA_FIRST, HIGH_PRIORITY - 1, 1, REFUSED};
  ID head, late;
  ID semid = head_waits_for_two(&row, &head, &late);
  SEMCB *semcb = &tkmc_semcbs[semid - 1];
  TCB *tcb = &tkmc_tcbs[late - 1];

  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
  TEST_ASSERT_EQUAL(1, semcb->semcnt);
  TEST_ASSERT_EQUAL_UINT(2, count_in_queue(&semcb->wait_queue));
  TEST_ASSERT_EQUAL_PTR(&tkmc_tcbs[head - 1], first_waiter(semcb));
  TEST_ASSERT_EQUAL_PTR(tcb, last_waiter(semcb));
  TEST_ASSERT_EQUAL_UINT(TTW_SEM, tcb->tskwait);
  TEST_ASSERT_EQUAL_UINT(0, tcb->delay_ticks);
  TEST_ASSERT_TRUE(tkmc_list_empty(&tcb->head)); // On no ready or timer queue.
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  host_check_kernel_queues();
}

// Under TA_TPRI the request is weighed against the head of the queue, not
// against any waiter: one that outranks a waiter further back but not the head
// would be queued behind the head, so it takes nothing.
void test_a_request_that_outranks_only_a_later_waiter_takes_nothing(void) {
  ID semid = tk_cre_sem(&(T_CSEM){
      .exinf = 0, .sematr = TA_TPRI | TA_FIRST, .isemcnt = 0, .maxsem = 2});
  TEST_ASSERT_GREATER_THAN(0, semid);
  ID head = host_make_task(HIGH_PRIORITY - 1);
  ID later = host_make_task(LOW_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY + 1);
  host_set_current(head);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 2, TMO_FEVR));
  host_set_current(later);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL(1, tkmc_semcbs[semid - 1].semcnt);
  TEST_ASSERT_EQUAL_UINT(2, count_in_queue(&tkmc_semcbs[semid - 1].wait_queue));

  host_set_current(host_make_task(HIGH_PRIORITY));
  host_snapshot_t before;
  host_take_snapshot(&before);
  TEST_ASSERT_EQUAL(E_TMOUT, tk_wai_sem(semid, 1, TMO_POL));
  TEST_ASSERT_TRUE(host_interrupts_enabled());
  host_assert_unchanged(&before, "a poll that outranks only a later waiter");
}

// With no task waiting, every attribute lets a request take its units at once,
// and the count goes down by the request, once: 3 - 2 is 1, where taking one
// unit or taking two twice would leave 2 or less than 0.
void test_a_request_that_finds_no_waiter_takes_its_units_at_once(void) {
  static const ATR attributes[] = {TA_TFIFO | TA_FIRST, TA_TPRI | TA_FIRST,
                                   TA_TFIFO | TA_CNT, TA_TPRI | TA_CNT};
  static const TMO limits[] = {TMO_POL, WAIT_MS, TMO_FEVR};
  for (UINT i = 0; i < sizeof(attributes) / sizeof(attributes[0]); ++i) {
    for (UINT j = 0; j < sizeof(limits) / sizeof(limits[0]); ++j) {
      char label[64];
      snprintf(label, sizeof(label), "%s, limit %d",
               attribute_name(attributes[i]), (int)limits[j]);
      host_reset();
      ID semid = tk_cre_sem(&(T_CSEM){
          .exinf = 0, .sematr = attributes[i], .isemcnt = 3, .maxsem = 3});
      TEST_ASSERT_GREATER_THAN(0, semid);
      host_set_current(host_make_task(HIGH_PRIORITY));
      host_snapshot_t before;
      host_take_snapshot(&before);

      TEST_ASSERT_EQUAL_MESSAGE(E_OK, tk_wai_sem(semid, 2, limits[j]), label);
      TEST_ASSERT_EQUAL_MESSAGE(1, tkmc_semcbs[semid - 1].semcnt, label);
      TEST_ASSERT_TRUE_MESSAGE(host_interrupts_enabled(), label);
      // The units are all the request changed.
      TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 2));
      host_assert_unchanged(&before, label);
    }
  }
}

// A request for more units than the semaphore can ever hold is a parameter
// error, whatever the time limit: it could only wait for ever. The kernel used
// to queue it.
void test_a_request_above_the_maximum_is_refused(void) {
  ID semid = create_semaphore(1, 1);
  host_set_current(host_make_task(HIGH_PRIORITY));
  static const TMO limits[] = {TMO_POL, WAIT_MS, TMO_FEVR};
  for (UINT i = 0; i < sizeof(limits) / sizeof(limits[0]); ++i) {
    host_snapshot_t before;
    host_take_snapshot(&before);
    TEST_ASSERT_EQUAL(E_PAR, tk_wai_sem(semid, 2, limits[i]));
    TEST_ASSERT_TRUE(host_interrupts_enabled());
    host_assert_unchanged(&before, "a request above the maximum");
  }
  // A semaphore that does not exist is reported first.
  TEST_ASSERT_EQUAL(E_NOEXS, tk_wai_sem(semid + 1, 2, TMO_POL));
}

// A signal serves the waiters from the head of the queue as far as its units
// go: under TA_FIRST it stops at the first request it cannot cover, under
// TA_CNT it skips that request and goes on. The head waits for two units and
// the task behind it for one.
void test_a_signal_serves_the_waiters_in_queue_order_as_far_as_it_goes(void) {
  static const struct {
    ATR sematr;
    INT cnt;
    UINT head_state;
    UINT second_state;
  } rows[] = {
      {TA_TFIFO | TA_FIRST, 3, TTS_RDY, TTS_RDY},
      {TA_TFIFO | TA_FIRST, 1, TTS_WAI, TTS_WAI},
      {TA_TFIFO | TA_CNT, 1, TTS_WAI, TTS_RDY},
      {TA_TFIFO | TA_CNT, 2, TTS_RDY, TTS_WAI},
  };
  for (UINT i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
    char label[64];
    snprintf(label, sizeof(label), "%s, signal %d",
             attribute_name(rows[i].sematr), (int)rows[i].cnt);
    host_reset();
    ID semid = tk_cre_sem(&(T_CSEM){
        .exinf = 0, .sematr = rows[i].sematr, .isemcnt = 0, .maxsem = 3});
    TEST_ASSERT_GREATER_THAN(0, semid);
    ID other;
    ID head = make_waiter(semid, 2, &other);
    ID second = host_make_task(HIGH_PRIORITY);
    host_set_current(second);
    HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
    host_set_current(other);

    TEST_ASSERT_EQUAL_MESSAGE(E_OK, tk_sig_sem(semid, rows[i].cnt), label);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(rows[i].head_state,
                                   tkmc_tcbs[head - 1].tskstat, label);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(rows[i].second_state,
                                   tkmc_tcbs[second - 1].tskstat, label);
    INT served = (rows[i].head_state == TTS_RDY ? 2 : 0) +
                 (rows[i].second_state == TTS_RDY ? 1 : 0);
    TEST_ASSERT_EQUAL_MESSAGE(rows[i].cnt - served,
                              tkmc_semcbs[semid - 1].semcnt, label);
    host_check_kernel_queues();
  }
}
