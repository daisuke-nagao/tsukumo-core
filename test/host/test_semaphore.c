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

#include <sys/interrupt.h>
#include <tk/tkernel.h>
#include <unity.h>

#include "fakes.h"
#include "semaphore.h"
#include "task.h"
#include "timer.h"

enum { LIMIT = 32767 };

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
// interrupts preserved, and nothing the kernel keeps changed (the free list of
// semaphores runs through the control blocks, so taking one would show).
static void check_create_refused(const T_CSEM *pk, ER expected) {
  const BOOL interrupts = host_interrupts_enabled();
  host_snapshot_t before;
  host_take_snapshot(&before);

  TEST_ASSERT_EQUAL(expected, tk_cre_sem(pk));
  TEST_ASSERT_EQUAL_UINT(interrupts, host_interrupts_enabled());
  host_assert_unchanged(&before, "a refused tk_cre_sem()");
  host_check_kernel_queues();
}

static void test_tk_cre_sem_refuses_null_packet_and_keeps_free_slots(void) {
  for (UINT disabled = 0; disabled < 2; ++disabled) {
    host_reset();
    UINT intsts;
    if (disabled) {
      DI(intsts);
    }
    for (UINT i = 0; i < CFN_MAX_SEMID; ++i) {
      check_create_refused(NULL, E_PAR);
    }
    for (UINT i = 0; i < CFN_MAX_SEMID; ++i) {
      TEST_ASSERT_EQUAL(i + 1, create_semaphore(0, 1));
    }
    check_create_refused(NULL, E_PAR); // Even with no free control block.
    T_CSEM good = packet(0, 1);
    check_create_refused(&good, E_LIMIT);
    if (disabled) {
      EI(intsts);
    }
  }
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

enum { BAD_COUNTS = HOST_COUNT(bad_counts) };

// Offers every pair of bad counts with the attributes, and checks each refusal.
static void check_bad_counts_refused(ATR sematr, ER expected) {
  for (UINT i = 0; i < BAD_COUNTS; ++i) {
    T_CSEM pk = packet(bad_counts[i].isemcnt, bad_counts[i].maxsem);
    pk.sematr = sematr;
    check_create_refused(&pk, expected);
  }
}

static void test_tk_cre_sem_refuses_count_outside_range(void) {
  check_bad_counts_refused(TA_TFIFO, E_PAR);
  TEST_ASSERT_EQUAL(1, create_semaphore(0, 1));
  TEST_ASSERT_EQUAL(CFN_MAX_SEMID - 1, host_count_creatable_semaphores());
}

static void test_tk_cre_sem_reports_attribute_error_before_count_error(void) {
  // TA_DSNAME is a bit the kernel does not support.
  check_bad_counts_refused(TA_TPRI | TA_DSNAME, E_RSATR);
}

static void test_tk_cre_sem_reports_count_error_before_exhausted_slots(void) {
  for (UINT i = 0; i < CFN_MAX_SEMID; ++i) {
    create_semaphore(0, 1);
  }
  check_bad_counts_refused(TA_TFIFO, E_PAR);
  T_CSEM good = packet(0, 1);
  check_create_refused(&good, E_LIMIT);
}

// Each accepted pair starts with its initial count, takes signals up to its
// maximum and refuses one more.
static void test_tk_cre_sem_accepts_counts_at_both_ends_of_range(void) {
  host_set_current(host_make_task(HOST_HIGH_PRIORITY));
  static const counts_t good[] = {
      {0, 1}, {1, 1}, {0, LIMIT}, {LIMIT, LIMIT}, {1, 3}};
  for (UINT i = 0; i < HOST_COUNT(good); ++i) {
    ID semid = create_semaphore(good[i].isemcnt, good[i].maxsem);
    check_count(semid, good[i].isemcnt);

    TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, good[i].maxsem));
    check_signal_refused(semid, 1, E_QOVR);
    check_count(semid, good[i].maxsem);
    TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  }
}

static void test_tk_sig_sem_refuses_signal_past_maximum_and_keeps_count(void) {
  host_set_current(host_make_task(HOST_HIGH_PRIORITY));
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
  for (UINT i = 0; i < HOST_COUNT(rows); ++i) {
    ID semid = create_semaphore(rows[i].isemcnt, rows[i].maxsem);
    check_signal_refused(semid, rows[i].cnt, rows[i].expected);
    check_count(semid, rows[i].isemcnt);
    TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  }
}

// A task that waits for units of the semaphore, and another one to run.
static ID make_waiter(ID semid, INT cnt, ID *other) {
  ID waiter = host_make_task(HOST_HIGH_PRIORITY);
  *other = host_make_task(HOST_LOW_PRIORITY);
  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, cnt, TMO_FEVR));
  host_set_current(*other);
  return waiter;
}

static void test_tk_sig_sem_past_maximum_wakes_no_waiter(void) {
  ID semid = create_semaphore(0, 1);
  ID other;
  ID waiter = make_waiter(semid, 1, &other);

  check_signal_refused(semid, 2, E_QOVR);
  check_signal_refused(semid, INT_MAX, E_QOVR);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tkmc_tcbs[waiter - 1].tskstat);
}

// The maximum bounds the count before any of it is given to the waiters, even
// units a waiter would take at once.
static void test_tk_sig_sem_counts_maximum_before_serving_waiters(void) {
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

static void test_tk_sig_sem_refused_with_interrupts_off_leaves_them_off(void) {
  host_set_current(host_make_task(HOST_HIGH_PRIORITY));
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
  PRI priority; // of the new request; the head waits at HOST_HIGH_PRIORITY
  INT units;    // the head waits for all of them, and they are the maximum
  INT left;     // units left while the head waits
  INT request;  // units the new request asks for
  order_t expected;
} order_row_t;

// A request for one unit, behind a head that waits for two.
#define ONE_OF_TWO(sematr, priority, left, expected)                           \
  {sematr, priority, 2, left, 1, expected}

static const order_row_t order_rows[] = {
    ONE_OF_TWO(TA_TFIFO | TA_FIRST, HOST_HIGH_PRIORITY - 1, 1, REFUSED),
    ONE_OF_TWO(TA_TFIFO | TA_FIRST, HOST_HIGH_PRIORITY, 1, REFUSED),
    ONE_OF_TWO(TA_TFIFO | TA_FIRST, HOST_LOW_PRIORITY, 1, REFUSED),
    ONE_OF_TWO(TA_TPRI | TA_FIRST, HOST_HIGH_PRIORITY - 1, 1, TAKES),
    ONE_OF_TWO(TA_TPRI | TA_FIRST, HOST_HIGH_PRIORITY, 1, REFUSED),
    ONE_OF_TWO(TA_TPRI | TA_FIRST, HOST_LOW_PRIORITY, 1, REFUSED),
    ONE_OF_TWO(TA_TFIFO | TA_CNT, HOST_HIGH_PRIORITY - 1, 1, TAKES),
    ONE_OF_TWO(TA_TFIFO | TA_CNT, HOST_HIGH_PRIORITY, 1, TAKES),
    ONE_OF_TWO(TA_TFIFO | TA_CNT, HOST_LOW_PRIORITY, 1, TAKES),
    ONE_OF_TWO(TA_TPRI | TA_CNT, HOST_HIGH_PRIORITY - 1, 1, TAKES),
    ONE_OF_TWO(TA_TPRI | TA_CNT, HOST_HIGH_PRIORITY, 1, TAKES),
    ONE_OF_TWO(TA_TPRI | TA_CNT, HOST_LOW_PRIORITY, 1, TAKES),
    // With no unit left, not even a request that may go first takes one.
    ONE_OF_TWO(TA_TPRI | TA_FIRST, HOST_HIGH_PRIORITY - 1, 0, REFUSED),
    ONE_OF_TWO(TA_TFIFO | TA_CNT, HOST_HIGH_PRIORITY - 1, 0, REFUSED),
    ONE_OF_TWO(TA_TPRI | TA_CNT, HOST_HIGH_PRIORITY - 1, 0, REFUSED),
    // A request for more than one unit: the head waits for three, two are left.
    // The units a request asks for decide, as well as the order, whether it
    // takes them: three are not there, however the order would let it pass.
    {TA_TFIFO | TA_FIRST, HOST_HIGH_PRIORITY - 1, 3, 2, 2, REFUSED},
    {TA_TPRI | TA_FIRST, HOST_HIGH_PRIORITY - 1, 3, 2, 2, TAKES},
    {TA_TPRI | TA_FIRST, HOST_HIGH_PRIORITY, 3, 2, 2, REFUSED},
    {TA_TFIFO | TA_CNT, HOST_HIGH_PRIORITY, 3, 2, 2, TAKES},
    {TA_TFIFO | TA_FIRST, HOST_HIGH_PRIORITY - 1, 3, 2, 3, REFUSED},
    {TA_TPRI | TA_FIRST, HOST_HIGH_PRIORITY - 1, 3, 2, 3, REFUSED},
    {TA_TFIFO | TA_CNT, HOST_HIGH_PRIORITY, 3, 2, 3, REFUSED},
    {TA_TPRI | TA_CNT, HOST_HIGH_PRIORITY - 1, 3, 2, 3, REFUSED},
};

static UINT count_in_queue(const tkmc_list_head *head) {
  UINT n = 0;
  for (const tkmc_list_head *pos = head->next; pos != head; pos = pos->next) {
    ++n;
  }
  return n;
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

// A semaphore of row->units units (all of them its maximum) with the row's
// attribute, on which a task waits for all of them while row->left of them are
// there, and a new task at the row's priority, running, to make the request.
// The waiting task goes to *head and the new one to *late.
static ID head_waits(const order_row_t *row, ID *head, ID *late) {
  ID semid = tk_cre_sem(&(T_CSEM){
      .exinf = 0, .sematr = row->sematr, .isemcnt = 0, .maxsem = row->units});
  TEST_ASSERT_GREATER_THAN(0, semid);
  ID other;
  *head = make_waiter(semid, row->units, &other);
  if (row->left > 0) {
    TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, row->left));
  }
  TEST_ASSERT_EQUAL(row->left, tkmc_semcbs[semid - 1].semcnt);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tkmc_tcbs[*head - 1].tskstat);
  *late = host_make_task(row->priority);
  host_set_current(*late);
  return semid;
}

// After the late request took its units at once: the count is down by them
// (the return code alone would not show a request that waited, as the PC does
// not switch it out without HOST_EXPECT_WAIT), and the head still waits, alone.
static void check_took(ID semid, ID head, INT remaining, const char *label) {
  SEMCB *semcb = &tkmc_semcbs[semid - 1];
  TEST_ASSERT_EQUAL_MESSAGE(remaining, semcb->semcnt, label);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, count_in_queue(&semcb->wait_queue), label);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(&tkmc_tcbs[head - 1], first_waiter(semcb),
                                label);
  TEST_ASSERT_TRUE_MESSAGE(host_interrupts_enabled(), label);
  host_check_kernel_queues();
}

static void check_poll(const order_row_t *row, const char *label) {
  ID head, late;
  ID semid = head_waits(row, &head, &late);
  host_snapshot_t before;
  host_take_snapshot(&before);

  ER ercd = tk_wai_sem(semid, row->request, TMO_POL);
  if (row->expected == REFUSED) {
    TEST_ASSERT_EQUAL_MESSAGE(E_TMOUT, ercd, label);
    TEST_ASSERT_TRUE_MESSAGE(host_interrupts_enabled(), label);
    host_assert_unchanged(&before, label);
    return;
  }
  TEST_ASSERT_EQUAL_MESSAGE(E_OK, ercd, label);
  check_took(semid, head, row->left - row->request, label);
  // The units are all the request changed. Giving them back asks for no switch
  // only while the late task is the highest priority ready one: the task that
  // make_waiter() left ready runs at HOST_LOW_PRIORITY and was queued first.
  if (row->priority < HOST_LOW_PRIORITY) {
    TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, row->request));
    host_assert_unchanged(&before, label);
  }
}

// A request with a time limit, or for ever, that the order refuses waits in
// the queue, with a timer if it has a limit and with none if it does not.
static void check_wait_with_limit(const order_row_t *row, TMO tmout,
                                  const char *label) {
  ID head, late;
  ID semid = head_waits(row, &head, &late);
  SEMCB *semcb = &tkmc_semcbs[semid - 1];

  if (row->expected == TAKES) {
    TEST_ASSERT_EQUAL_MESSAGE(E_OK, tk_wai_sem(semid, row->request, tmout),
                              label);
    check_took(semid, head, row->left - row->request, label);
    return;
  }
  HOST_EXPECT_WAIT(tk_wai_sem(semid, row->request, tmout));
  // The late task waits once, in its place in the queue (ahead of the head
  // only under TA_TPRI with the higher priority), asking for its units, and
  // leaves the units it found.
  TCB *tcb = &tkmc_tcbs[late - 1];
  BOOL goes_first =
      (row->sematr & TA_TPRI) != 0 && row->priority < HOST_HIGH_PRIORITY;
  TEST_ASSERT_EQUAL_MESSAGE(row->left, semcb->semcnt, label);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(2, count_in_queue(&semcb->wait_queue), label);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(goes_first ? tcb : &tkmc_tcbs[head - 1],
                                first_waiter(semcb), label);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(goes_first ? &tkmc_tcbs[head - 1] : tcb,
                                last_waiter(semcb), label);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(TTW_SEM, tcb->tskwait, label);
  TEST_ASSERT_EQUAL_INT_MESSAGE(row->request, tcb->winfo.semaphore.semcnt,
                                label);
  if (tmout == TMO_FEVR) {
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, tcb->delay_ticks, label);
    // On no ready queue and no timer queue.
    TEST_ASSERT_TRUE_MESSAGE(tkmc_list_empty(&tcb->head), label);
    TEST_ASSERT_TRUE_MESSAGE(tkmc_list_empty(&tkmc_timer_queue), label);
  } else {
    TEST_ASSERT_EQUAL_UINT_MESSAGE(tkmc_ms_to_ticks(tmout), tcb->delay_ticks,
                                   label);
    TEST_ASSERT_TRUE_MESSAGE(host_in_queue(&tkmc_timer_queue, &tcb->head),
                             label);
  }
  host_check_kernel_queues();
}

// A poll the order refuses is E_TMOUT and changes nothing; a request with a
// time limit, or for ever, that the order refuses waits in the queue. A request
// the order allows takes its units either way.
static void
test_tk_wai_sem_new_request_takes_units_only_in_semaphore_order(void) {
  for (UINT i = 0; i < HOST_COUNT(order_rows); ++i) {
    const order_row_t *row = &order_rows[i];
    char label[112];
    snprintf(label, sizeof(label),
             "%s, priority %d against %d, %d left, asks for %d",
             attribute_name(row->sematr), (int)row->priority,
             (int)HOST_HIGH_PRIORITY, (int)row->left, (int)row->request);

    // The label names the run, as Unity reports nothing else about a failure.
    char run[128];
    host_reset();
    snprintf(run, sizeof(run), "%s: poll", label);
    check_poll(row, run);
    host_reset();
    snprintf(run, sizeof(run), "%s: wait %d ms", label, (int)WAIT_MS);
    check_wait_with_limit(row, WAIT_MS, run);
    host_reset();
    snprintf(run, sizeof(run), "%s: wait for ever", label);
    check_wait_with_limit(row, TMO_FEVR, run);
  }
}

// Under TA_TPRI the request is weighed against the head of the queue, not
// against any waiter: one that outranks a waiter further back but not the head
// would be queued behind the head, so it takes nothing. The queue holds the
// head (at HOST_HIGH_PRIORITY - 1, for two units) and a later waiter (at
// HOST_LOW_PRIORITY, for one), and one unit is left.
typedef struct {
  PRI priority; // of the new request, which asks for one unit
  order_t expected;
} later_row_t;

static const later_row_t later_rows[] = {
    {HOST_HIGH_PRIORITY - 2, TAKES},   // outranks both waiters
    {HOST_HIGH_PRIORITY - 1, REFUSED}, // as the head
    {HOST_HIGH_PRIORITY, REFUSED},     // outranks the later waiter only
    {HOST_LOW_PRIORITY, REFUSED},      // as the later waiter
    {HOST_LOW_PRIORITY + 1, REFUSED},
};

static ID two_waiters_and_a_request_at(PRI priority) {
  ID semid = tk_cre_sem(&(T_CSEM){
      .exinf = 0, .sematr = TA_TPRI | TA_FIRST, .isemcnt = 0, .maxsem = 2});
  TEST_ASSERT_GREATER_THAN(0, semid);
  ID head = host_make_task(HOST_HIGH_PRIORITY - 1);
  ID later = host_make_task(HOST_LOW_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY + 1);
  host_set_current(head);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 2, TMO_FEVR));
  host_set_current(later);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL(1, tkmc_semcbs[semid - 1].semcnt);
  TEST_ASSERT_EQUAL_UINT(2, count_in_queue(&tkmc_semcbs[semid - 1].wait_queue));
  host_set_current(host_make_task(priority));
  return semid;
}

static void
test_tk_wai_sem_weighs_request_against_queue_head_not_later_waiter(void) {
  for (UINT i = 0; i < HOST_COUNT(later_rows); ++i) {
    const later_row_t *row = &later_rows[i];
    char label[64];
    snprintf(label, sizeof(label),
             "request at priority %d, waiters at %d and %d", (int)row->priority,
             (int)HOST_HIGH_PRIORITY - 1, (int)HOST_LOW_PRIORITY);

    host_reset();
    ID semid = two_waiters_and_a_request_at(row->priority);
    SEMCB *semcb = &tkmc_semcbs[semid - 1];
    host_snapshot_t before;
    host_take_snapshot(&before);
    ER ercd = tk_wai_sem(semid, 1, TMO_POL);
    if (row->expected == REFUSED) {
      TEST_ASSERT_EQUAL_MESSAGE(E_TMOUT, ercd, label);
      TEST_ASSERT_TRUE_MESSAGE(host_interrupts_enabled(), label);
      host_assert_unchanged(&before, label);
    } else {
      TEST_ASSERT_EQUAL_MESSAGE(E_OK, ercd, label);
      TEST_ASSERT_EQUAL_MESSAGE(0, semcb->semcnt, label);
      TEST_ASSERT_EQUAL_UINT_MESSAGE(2, count_in_queue(&semcb->wait_queue),
                                     label);
    }

    // With a time limit, a refused request waits among the others.
    host_reset();
    semid = two_waiters_and_a_request_at(row->priority);
    semcb = &tkmc_semcbs[semid - 1];
    if (row->expected == REFUSED) {
      HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, WAIT_MS));
      TEST_ASSERT_EQUAL_MESSAGE(1, semcb->semcnt, label);
      TEST_ASSERT_EQUAL_UINT_MESSAGE(3, count_in_queue(&semcb->wait_queue),
                                     label);
      TEST_ASSERT_TRUE_MESSAGE(
          host_in_queue(&semcb->wait_queue, &current->winfo.wait_queue), label);
    } else {
      TEST_ASSERT_EQUAL_MESSAGE(E_OK, tk_wai_sem(semid, 1, WAIT_MS), label);
      TEST_ASSERT_EQUAL_MESSAGE(0, semcb->semcnt, label);
      TEST_ASSERT_EQUAL_UINT_MESSAGE(2, count_in_queue(&semcb->wait_queue),
                                     label);
    }
    host_check_kernel_queues();
  }
}

// With no task waiting, every attribute lets a request take its units at once,
// and the count goes down by the request, once: 3 - 2 is 1, where taking one
// unit or taking two twice would leave 2 or less than 0.
static void test_tk_wai_sem_takes_units_at_once_when_no_waiter(void) {
  static const ATR attributes[] = {TA_TFIFO | TA_FIRST, TA_TPRI | TA_FIRST,
                                   TA_TFIFO | TA_CNT, TA_TPRI | TA_CNT};
  static const TMO limits[] = {TMO_POL, WAIT_MS, TMO_FEVR};
  for (UINT i = 0; i < HOST_COUNT(attributes); ++i) {
    for (UINT j = 0; j < HOST_COUNT(limits); ++j) {
      char label[64];
      snprintf(label, sizeof(label), "%s, limit %d",
               attribute_name(attributes[i]), (int)limits[j]);
      host_reset();
      ID semid = tk_cre_sem(&(T_CSEM){
          .exinf = 0, .sematr = attributes[i], .isemcnt = 3, .maxsem = 3});
      TEST_ASSERT_GREATER_THAN(0, semid);
      host_set_current(host_make_task(HOST_HIGH_PRIORITY));
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
static void test_tk_wai_sem_refuses_request_above_maximum(void) {
  ID semid = create_semaphore(1, 1);
  host_set_current(host_make_task(HOST_HIGH_PRIORITY));
  static const TMO limits[] = {TMO_POL, WAIT_MS, TMO_FEVR};
  for (UINT i = 0; i < HOST_COUNT(limits); ++i) {
    host_snapshot_t before;
    host_take_snapshot(&before);
    TEST_ASSERT_EQUAL(E_PAR, tk_wai_sem(semid, 2, limits[i]));
    TEST_ASSERT_TRUE(host_interrupts_enabled());
    host_assert_unchanged(&before, "a request above the maximum");
  }
  // A semaphore that does not exist is reported first.
  TEST_ASSERT_EQUAL(E_NOEXS, tk_wai_sem(semid + 1, 2, TMO_POL));
}

// Under TA_CNT a signal goes through the whole queue, so no request that the
// units cover stays behind: with 2 units the waiters for 1, 3 and 1 units leave
// the one for 3 waiting, and both of the others are served. A signal that
// stopped after the first request it served would leave the last one waiting
// for units that are there.
static void test_tk_sig_sem_with_ta_cnt_serves_every_request_units_cover(void) {
  static const ATR attributes[] = {TA_TFIFO | TA_CNT, TA_TPRI | TA_CNT};
  for (UINT i = 0; i < HOST_COUNT(attributes); ++i) {
    const char *label = attribute_name(attributes[i]);
    host_reset();
    ID semid = tk_cre_sem(&(T_CSEM){
        .exinf = 0, .sematr = attributes[i], .isemcnt = 0, .maxsem = 3});
    TEST_ASSERT_GREATER_THAN(0, semid);
    ID other;
    ID first = make_waiter(semid, 1, &other);
    ID middle = host_make_task(HOST_HIGH_PRIORITY);
    host_set_current(middle);
    HOST_EXPECT_WAIT(tk_wai_sem(semid, 3, TMO_FEVR));
    ID last = host_make_task(HOST_HIGH_PRIORITY);
    host_set_current(last);
    HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
    host_set_current(other);

    TEST_ASSERT_EQUAL_MESSAGE(E_OK, tk_sig_sem(semid, 2), label);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_RDY, tkmc_tcbs[first - 1].tskstat,
                                   label);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_WAI, tkmc_tcbs[middle - 1].tskstat,
                                   label);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_RDY, tkmc_tcbs[last - 1].tskstat, label);
    TEST_ASSERT_EQUAL_MESSAGE(0, tkmc_semcbs[semid - 1].semcnt, label);
    host_check_kernel_queues();
  }
}

// A signal serves the waiters from the head of the queue as far as its units
// go: under TA_FIRST it stops at the first request it cannot cover, under
// TA_CNT it skips that request and goes on. The head waits for two units and
// the task behind it for one.
static void
test_tk_sig_sem_serves_waiters_in_queue_order_as_far_as_units_go(void) {
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
  for (UINT i = 0; i < HOST_COUNT(rows); ++i) {
    char label[64];
    snprintf(label, sizeof(label), "%s, signal %d",
             attribute_name(rows[i].sematr), (int)rows[i].cnt);
    host_reset();
    ID semid = tk_cre_sem(&(T_CSEM){
        .exinf = 0, .sematr = rows[i].sematr, .isemcnt = 0, .maxsem = 3});
    TEST_ASSERT_GREATER_THAN(0, semid);
    ID other;
    ID head = make_waiter(semid, 2, &other);
    ID second = host_make_task(HOST_HIGH_PRIORITY);
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

// On a TA_FIRST semaphore the head of the queue holds back the waiters behind
// it. When the head leaves without its units (its time limit runs out, or
// tk_rel_wai()), the units it could not use go to the waiters behind it, as far
// as they go, at that moment: a signal that came while the head waited is not
// lost until the next one. The kernel used to leave the next waiter waiting
// with the units lying there, and refused every new request meanwhile.
//
// A semaphore of two units: head waits for two, next waits for one, and one
// unit is signalled, which the head cannot use. The head has the lowest
// priority of the three tasks, so its own departure asks for no task switch:
// a switch request afterwards can only come from serving the next waiter.
static ID head_blocks_the_next_waiter(ID *head, ID *next_waiter, TMO next_ms) {
  ID semid = tk_cre_sem(&(T_CSEM){
      .exinf = 0, .sematr = TA_TFIFO | TA_FIRST, .isemcnt = 0, .maxsem = 2});
  TEST_ASSERT_GREATER_THAN(0, semid);
  *head = host_make_task(HOST_LOW_PRIORITY + 1);
  *next_waiter = host_make_task(HOST_HIGH_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY);
  host_set_current(*head);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 2, 100)); // 11 ticks
  host_set_current(*next_waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, next_ms));
  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, host_tcb(*head)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, host_tcb(*next_waiter)->tskstat);
  TEST_ASSERT_EQUAL(1, tkmc_semcbs[semid - 1].semcnt);
  return semid;
}

// requests_before is the number of switch requests before the head left.
static void check_next_waiter_was_served(ID semid, ID next_waiter,
                                         UINT requests_before) {
  TCB *tcb = host_tcb(next_waiter);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  TEST_ASSERT_EQUAL(E_OK, tcb->wupcause);
  TEST_ASSERT_EQUAL(0, tkmc_semcbs[semid - 1].semcnt);
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_semcbs[semid - 1].wait_queue));
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  TEST_ASSERT_EQUAL_UINT(requests_before + 1, host_write_count(HOST_MSIP));
  TEST_ASSERT_EQUAL_PTR(tcb, next);
  host_check_kernel_state();
}

extern void tkmc_timer_handler(void);

// One delayed interrupt reaches both deadlines. B's earlier deadline must
// release it before A leaves; otherwise A hands the unit to an expired B.
static void test_semaphore_late_interrupt_expires_successor_before_head(void) {
  ID head, next_waiter;
  ID semid = head_blocks_the_next_waiter(&head, &next_waiter, 20);
  UINT requests = host_write_count(HOST_MSIP);

  host_mtime = 11 * HOST_TICK;
  tkmc_timer_handler();

  TEST_ASSERT_EQUAL(E_TMOUT, host_tcb(head)->wupcause);
  TEST_ASSERT_EQUAL(E_TMOUT, host_tcb(next_waiter)->wupcause);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(head)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(next_waiter)->tskstat);
  TEST_ASSERT_EQUAL(1, tkmc_semcbs[semid - 1].semcnt);
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_semcbs[semid - 1].wait_queue));
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  TEST_ASSERT_EQUAL_UINT(requests + 1, host_write_count(HOST_MSIP));
  TEST_ASSERT_EQUAL_PTR(host_tcb(next_waiter), next);
  host_check_kernel_state();
}

// Reversing the deadlines must still hand the unit on when A expires, even
// though B's later deadline has also passed by the time the interrupt runs.
static void
test_semaphore_late_interrupt_serves_successor_before_its_later_deadline(void) {
  ID head, next_waiter;
  ID semid = head_blocks_the_next_waiter(&head, &next_waiter, 200);
  UINT requests = host_write_count(HOST_MSIP);

  host_mtime = 21 * HOST_TICK;
  tkmc_timer_handler();

  TEST_ASSERT_EQUAL(E_TMOUT, host_tcb(head)->wupcause);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(head)->tskstat);
  check_next_waiter_was_served(semid, next_waiter, requests);
}

// The head a leaves by a time-out or tk_rel_wai(). Usually a outranks b and
// the caller, so the departure itself asks for a switch to a. In the last row
// a has the lowest priority of the three: its departure asks for no switch,
// and the one switch request can only come from serving b. Only a TA_TFIFO
// queue keeps a at the head then.
typedef struct {
  ATR attr;
  PRI a_prio;
} departure_row_t;

enum { HEAD_HIGHEST = 3, B_PRIORITY = 4, CALLER_PRIORITY = 5, HEAD_LOWEST = 6 };

static const departure_row_t leave_rows[] = {
    {TA_TFIFO | TA_FIRST, HEAD_HIGHEST}, {TA_TPRI | TA_FIRST, HEAD_HIGHEST},
    {TA_TFIFO | TA_CNT, HEAD_HIGHEST},   {TA_TPRI | TA_CNT, HEAD_HIGHEST},
    {TA_TFIFO | TA_FIRST, HEAD_LOWEST},
};

static void check_departure_waiter(ID id, ID semid, BOOL waiting, BOOL timed,
                                   ER cause) {
  TCB *tcb = host_tcb(id);
  TEST_ASSERT_EQUAL_UINT(waiting ? TTS_WAI : TTS_RDY, tcb->tskstat);
  TEST_ASSERT_EQUAL(waiting && timed,
                    host_in_queue(&tkmc_timer_queue, &tcb->head));
  if (waiting) {
    TEST_ASSERT_EQUAL_UINT(TTW_SEM, tcb->tskwait);
    TEST_ASSERT_TRUE(host_in_queue(&tkmc_semcbs[semid - 1].wait_queue,
                                   &tcb->winfo.wait_queue));
    TEST_ASSERT_EQUAL(timed, tcb->delay_ticks != 0);
  } else {
    TEST_ASSERT_TRUE(tkmc_list_empty(&tcb->winfo.wait_queue));
    TEST_ASSERT_EQUAL(cause, tcb->wupcause);
  }
  // A ready task on the ready queue of its priority, waiting for nothing and
  // with no time limit: host_check_kernel_queues() checks that.
  host_check_kernel_queues();
}

static ID departure_fixture(ATR attr, PRI a_prio, TMO b_timeout, ID *a, ID *b,
                            ID *caller) {
  host_reset();
  ID semid = tk_cre_sem(&(T_CSEM){.sematr = attr, .maxsem = 2});
  TEST_ASSERT_GREATER_THAN(0, semid);
  *a = host_make_task(a_prio);
  *b = host_make_task(B_PRIORITY);
  *caller = host_make_task(CALLER_PRIORITY);
  host_set_current(*a);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 2, 100));
  host_set_current(*b);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, b_timeout));
  host_set_current(*caller);
  TEST_ASSERT_EQUAL_PTR(&host_tcb(*a)->winfo.wait_queue,
                        tkmc_semcbs[semid - 1].wait_queue.next);
  TEST_ASSERT_EQUAL(0, tkmc_semcbs[semid - 1].semcnt);
  check_departure_waiter(*a, semid, TRUE, TRUE, 0);
  check_departure_waiter(*b, semid, TRUE, b_timeout > 0, 0);
  return semid;
}

static void signal_before_departure(ID semid, ATR attr, ID a, ID b,
                                    BOOL b_timed, ID caller) {
  BOOL cnt = (attr & TA_CNT) != 0;
  UINT requests = host_write_count(HOST_MSIP);
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL(cnt ? 0 : 1, tkmc_semcbs[semid - 1].semcnt);
  check_departure_waiter(a, semid, TRUE, TRUE, 0);
  check_departure_waiter(b, semid, !cnt, b_timed, E_OK);
  TEST_ASSERT_EQUAL_UINT(requests + (cnt ? 1 : 0), host_write_count(HOST_MSIP));
  TEST_ASSERT_EQUAL_PTR(host_tcb(caller), current);
  TEST_ASSERT_EQUAL_PTR(host_tcb(cnt ? b : caller), next);
  TEST_ASSERT_TRUE(host_interrupts_enabled());
}

static void
test_semaphore_head_timeout_hands_units_to_next_waiter_per_attribute(void) {
  for (UINT row = 0; row < HOST_COUNT(leave_rows); ++row) {
    for (UINT irq = 0; irq < 2; ++irq) {
      ATR attr = leave_rows[row].attr;
      ID a, b, caller;
      ID semid =
          departure_fixture(attr, leave_rows[row].a_prio, 200, &a, &b, &caller);
      ID expected_next = leave_rows[row].a_prio < B_PRIORITY ? a : b;
      signal_before_departure(semid, attr, a, b, TRUE, caller);
      UINT requests = host_write_count(HOST_MSIP);
      UINT saved = 0;
      if (!irq) {
        DI(saved);
      }
      host_tick(10);
      TEST_ASSERT_EQUAL_UINT(1, host_tcb(a)->delay_ticks);
      check_departure_waiter(a, semid, TRUE, TRUE, 0);
      check_departure_waiter(b, semid, (attr & TA_CNT) == 0, TRUE, E_OK);
      TEST_ASSERT_EQUAL_UINT(requests, host_write_count(HOST_MSIP));
      host_tick(1);
      TEST_ASSERT_EQUAL(irq, host_interrupts_enabled());
      check_departure_waiter(a, semid, FALSE, FALSE, E_TMOUT);
      check_departure_waiter(b, semid, FALSE, FALSE, E_OK);
      TEST_ASSERT_EQUAL(0, tkmc_semcbs[semid - 1].semcnt);
      TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_semcbs[semid - 1].wait_queue));
      TEST_ASSERT_EQUAL_UINT(requests + 1, host_write_count(HOST_MSIP));
      TEST_ASSERT_EQUAL_PTR(host_tcb(expected_next), next);
      TEST_ASSERT_EQUAL_PTR(host_tcb(caller), current);
      if (!irq) {
        EI(saved);
      }
      host_check_kernel_state();
    }
  }
}

static void
test_tk_rel_wai_on_semaphore_head_hands_units_to_next_waiter_per_attribute(
    void) {
  for (UINT row = 0; row < HOST_COUNT(leave_rows); ++row) {
    for (UINT irq = 0; irq < 2; ++irq) {
      ATR attr = leave_rows[row].attr;
      ID a, b, caller;
      ID semid = departure_fixture(attr, leave_rows[row].a_prio, TMO_FEVR, &a,
                                   &b, &caller);
      ID expected_next = leave_rows[row].a_prio < B_PRIORITY ? a : b;
      signal_before_departure(semid, attr, a, b, FALSE, caller);
      UINT requests = host_write_count(HOST_MSIP);
      UINT saved = 0;
      if (!irq) {
        DI(saved);
      }
      TEST_ASSERT_EQUAL(E_OK, tk_rel_wai(a));
      TEST_ASSERT_EQUAL(irq, host_interrupts_enabled());
      check_departure_waiter(a, semid, FALSE, FALSE, E_RLWAI);
      check_departure_waiter(b, semid, FALSE, FALSE, E_OK);
      TEST_ASSERT_EQUAL(0, tkmc_semcbs[semid - 1].semcnt);
      TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_semcbs[semid - 1].wait_queue));
      TEST_ASSERT_EQUAL_UINT(requests + 1, host_write_count(HOST_MSIP));
      TEST_ASSERT_EQUAL_PTR(host_tcb(expected_next), next);
      TEST_ASSERT_EQUAL_PTR(host_tcb(caller), current);
      if (!irq) {
        EI(saved);
      }
      host_check_kernel_state();
    }
  }
}

// Deleting the semaphore releases both with E_DLT: the head's departure hands
// nothing on, as the semaphore is going away with every waiter. The last row
// has a head of the lowest priority and a next waiter without a time limit.
static void
test_tk_del_sem_releases_head_and_next_waiter_with_e_dlt_handing_nothing_on(
    void) {
  static const struct {
    ATR attr;
    PRI a_prio;
    TMO b_timeout;
  } rows[] = {
      {TA_TFIFO | TA_FIRST, HEAD_HIGHEST, 200},
      {TA_TPRI | TA_FIRST, HEAD_HIGHEST, 200},
      {TA_TFIFO | TA_CNT, HEAD_HIGHEST, 200},
      {TA_TPRI | TA_CNT, HEAD_HIGHEST, 200},
      {TA_TFIFO | TA_FIRST, HEAD_LOWEST, TMO_FEVR},
  };
  for (UINT row = 0; row < HOST_COUNT(rows); ++row) {
    for (UINT signalled = 0; signalled < 2; ++signalled) {
      for (UINT irq = 0; irq < 2; ++irq) {
        ATR attr = rows[row].attr;
        ID a, b, caller;
        ID semid = departure_fixture(attr, rows[row].a_prio,
                                     rows[row].b_timeout, &a, &b, &caller);
        ID expected_next = rows[row].a_prio < B_PRIORITY ? a : b;
        if (signalled) {
          signal_before_departure(semid, attr, a, b, rows[row].b_timeout > 0,
                                  caller);
        }
        BOOL b_served = signalled && (attr & TA_CNT);
        UINT requests = host_write_count(HOST_MSIP);
        UINT saved = 0;
        if (!irq) {
          DI(saved);
        }
        TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
        TEST_ASSERT_EQUAL(irq, host_interrupts_enabled());
        check_departure_waiter(a, semid, FALSE, FALSE, E_DLT);
        check_departure_waiter(b, semid, FALSE, FALSE, b_served ? E_OK : E_DLT);
        TEST_ASSERT_EQUAL_UINT(requests + 1, host_write_count(HOST_MSIP));
        TEST_ASSERT_EQUAL_PTR(host_tcb(expected_next), next);
        TEST_ASSERT_EQUAL_PTR(host_tcb(caller), current);
        if (!irq) {
          EI(saved);
        }
        host_snapshot_t snapshot;
        host_take_snapshot(&snapshot);
        DI(saved);
        host_tick(1);
        EI(saved);
        host_assert_unchanged(&snapshot, "deleted waiters must have no timer");
        TEST_ASSERT_EQUAL_UINT(CFN_MAX_SEMID,
                               host_count_creatable_semaphores());
        host_check_kernel_state();
      }
    }
  }
}

void run_semaphore_tests(void) {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_tk_cre_sem_refuses_count_outside_range);
  RUN_TEST(test_tk_cre_sem_reports_attribute_error_before_count_error);
  RUN_TEST(test_tk_cre_sem_reports_count_error_before_exhausted_slots);
  RUN_TEST(test_tk_cre_sem_accepts_counts_at_both_ends_of_range);
  RUN_TEST(test_tk_sig_sem_refuses_signal_past_maximum_and_keeps_count);
  RUN_TEST(test_tk_sig_sem_past_maximum_wakes_no_waiter);
  RUN_TEST(test_tk_sig_sem_counts_maximum_before_serving_waiters);
  RUN_TEST(test_tk_sig_sem_refused_with_interrupts_off_leaves_them_off);
  RUN_TEST(test_tk_wai_sem_new_request_takes_units_only_in_semaphore_order);
  RUN_TEST(test_tk_wai_sem_weighs_request_against_queue_head_not_later_waiter);
  RUN_TEST(test_tk_wai_sem_takes_units_at_once_when_no_waiter);
  RUN_TEST(test_tk_wai_sem_refuses_request_above_maximum);
  RUN_TEST(test_tk_sig_sem_serves_waiters_in_queue_order_as_far_as_units_go);
  RUN_TEST(test_tk_sig_sem_with_ta_cnt_serves_every_request_units_cover);
  RUN_TEST(test_tk_cre_sem_refuses_null_packet_and_keeps_free_slots);
  RUN_TEST(test_semaphore_late_interrupt_expires_successor_before_head);
  RUN_TEST(
      test_semaphore_late_interrupt_serves_successor_before_its_later_deadline);
  RUN_TEST(
      test_semaphore_head_timeout_hands_units_to_next_waiter_per_attribute);
  RUN_TEST(
      test_tk_rel_wai_on_semaphore_head_hands_units_to_next_waiter_per_attribute);
  RUN_TEST(
      test_tk_del_sem_releases_head_and_next_waiter_with_e_dlt_handing_nothing_on);
}
