/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Tests of semaphores (tk_cre_sem, tk_wai_sem, tk_sig_sem, tk_del_sem).
//
// The first two cases run two children that compete for a semaphore. A child
// holds the semaphore for HOLD_MS, and the other must wait for it. The children
// count how many of them are inside at once, so a semaphore that lets both in
// fails the assert in the child that arrives second. The others test a
// time-out, a signal after a time-out, a delete with waiters, the limit, the
// counts a semaphore accepts, the order in which a new request may take units
// ahead of the tasks that wait, and what the units of a head that leaves
// unserved go to.

#include <tk/tkernel.h>
#include <unity.h>

#include "../runner.h"

enum {
  ROUNDS = 5,
  HOLD_MS = 50,
  PAUSE_MS = 50,
  WAIT_MS = 100,
  TIMEOUT_MS = 50,
  DELETE_WAIT_MS = 1000,
  FIRST_PRIORITY = 2,
  SECOND_PRIORITY = 3
};
_Static_assert(
    SECOND_PRIORITY < TEST_CASE_PRIORITY,
    "the children must preempt the case, to start contending at once");

static ID create_semaphore_with(ATR attr, INT initial, INT maximum) {
  ID semid = tk_cre_sem(&(T_CSEM){
      .exinf = 0, .sematr = attr, .isemcnt = initial, .maxsem = maximum});
  TEST_ASSERT_GREATER_THAN(E_OK, semid);
  return semid;
}

static ID create_semaphore(INT initial, INT maximum) {
  return create_semaphore_with(TA_TFIFO, initial, maximum);
}

typedef struct {
  ID semid;
  INT units;           // How many units each round takes, and gives back.
  TMO wait_ms;         // How long a round may wait for the semaphore.
  volatile INT inside; // Children currently holding the semaphore.
} contest_t;

static void contender(void *arg) {
  contest_t *contest = arg;
  for (int round = 0; round < ROUNDS; ++round) {
    TEST_ASSERT_EQUAL(
        E_OK, tk_wai_sem(contest->semid, contest->units, contest->wait_ms));
    contest->inside++;
    TEST_ASSERT_EQUAL(1, contest->inside);
    tk_dly_tsk(HOLD_MS);
    contest->inside--;
    for (INT unit = 0; unit < contest->units; ++unit) {
      TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(contest->semid, 1));
    }
    tk_dly_tsk(PAUSE_MS);
  }
  test_checkpoint();
}

// One unit, two children. The high priority child gets the unit first; the
// other waits for it, and its wait of WAIT_MS outlasts the hold of HOLD_MS.
static contest_t g_binary;

static void binary_semaphore_is_exclusive(void) {
  g_binary.semid = create_semaphore(1, 1);
  g_binary.units = 1;
  g_binary.wait_ms = WAIT_MS;

  test_spawn(contender, FIRST_PRIORITY, &g_binary);
  test_spawn(contender, SECOND_PRIORITY, &g_binary);
  test_join_expect(2);

  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(g_binary.semid));
}

// Three units, and each child needs all three each round, so they take turns.
// Both children wait for as long as it takes.
static contest_t g_counting;

static void counting_semaphore_grants_whole_requests(void) {
  g_counting.semid = create_semaphore(3, 3);
  g_counting.units = 3;
  g_counting.wait_ms = TMO_FEVR;

  test_spawn(contender, FIRST_PRIORITY, &g_counting);
  test_spawn(contender, FIRST_PRIORITY, &g_counting);
  test_join_expect(2);

  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(g_counting.semid));
}

// A wait with a time limit that nobody satisfies returns E_TMOUT, and not
// before the limit. Only a lower bound is checked: a stalled host makes the
// wait longer, never shorter.
static void wait_times_out(void) {
  ID semid = create_semaphore(0, 1);

  UW start = test_mtime_mid_tick();
  TEST_ASSERT_EQUAL(E_TMOUT, tk_wai_sem(semid, 1, TIMEOUT_MS));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(TIMEOUT_MS * TEST_MTIME_PER_MS,
                                      (UW)(test_mtime() - start));

  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
}

// A wait that times out while a task of higher priority is running: the waiter
// is ready at once, but it cannot run, and a signal that comes in the meantime
// must not treat it as a waiter any more. The unit stays, and the waiter still
// returns E_TMOUT when it runs.
enum { LATE_PRIORITY = 5, SPIN_MS = 100 };
_Static_assert(LATE_PRIORITY > TEST_CASE_PRIORITY,
               "the waiter must not run while the case spins");

typedef struct {
  ID semid;
  volatile ER result;
} timed_wait_t;

static timed_wait_t g_timed;

static void timed_waiter(void *arg) {
  timed_wait_t *timed = arg;
  timed->result = tk_wai_sem(timed->semid, 1, TIMEOUT_MS);
  test_checkpoint();
}

static void signal_after_timeout_keeps_the_unit(void) {
  g_timed.semid = create_semaphore(0, 1);
  g_timed.result = E_SYS;

  test_spawn(timed_waiter, LATE_PRIORITY, &g_timed);
  tk_dly_tsk(10); // The waiter is below the case: it runs now, and blocks.

  // Spin without blocking, past the waiter's time limit. The waiter is ready
  // by then and is starved by the case.
  UW start = test_mtime();
  while ((UW)(test_mtime() - start) < SPIN_MS * TEST_MTIME_PER_MS) {
  }

  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(g_timed.semid, 1));
  test_join_expect(1);
  TEST_ASSERT_EQUAL(E_TMOUT, g_timed.result);
  TEST_ASSERT_EQUAL(E_OK, tk_wai_sem(g_timed.semid, 1, TMO_POL));

  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(g_timed.semid));
}

// Deleting a semaphore releases the tasks that wait for it with E_DLT, with or
// without a time limit, and the semaphore can be created and used again.
typedef struct {
  ID semid;
  TMO wait_ms;
} delete_wait_t;

static delete_wait_t g_delete_forever = {0, TMO_FEVR};
static delete_wait_t g_delete_timed = {0, DELETE_WAIT_MS};

static void wait_for_deleted(void *arg) {
  delete_wait_t *wait = arg;
  TEST_ASSERT_EQUAL(E_DLT, tk_wai_sem(wait->semid, 1, wait->wait_ms));
  test_checkpoint();
}

static void delete_releases_waiters(void) {
  ID semid = create_semaphore(0, 1);
  g_delete_forever.semid = semid;
  g_delete_timed.semid = semid;

  test_spawn(wait_for_deleted, FIRST_PRIORITY, &g_delete_forever);
  test_spawn(wait_for_deleted, SECOND_PRIORITY, &g_delete_timed);
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  test_join_expect(2);

  // The control block is on the free list again, and works.
  ID again = create_semaphore(1, 1);
  TEST_ASSERT_EQUAL(E_OK, tk_wai_sem(again, 1, TMO_POL));
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(again, 1));
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(again));
}

// A semaphore refuses to go past its maximum, and keeps its count.
static void signal_over_limit_is_refused(void) {
  ID semid = create_semaphore(1, 1);

  TEST_ASSERT_EQUAL(E_QOVR, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL(E_OK, tk_wai_sem(semid, 1, TMO_POL));
  TEST_ASSERT_EQUAL(E_TMOUT, tk_wai_sem(semid, 1, TMO_POL));

  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
}

// A maximum of 1 to 32767 and an initial count of 0 to the maximum are
// accepted, and nothing else.
static void counts_out_of_range_are_refused(void) {
  static const INT bad[][2] = {{-1, 1},       {0, 0},           {0, -1},
                               {0, 32768},    {0, __INT_MAX__}, {2, 1},
                               {32768, 32768}};
  for (UINT i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
    TEST_ASSERT_EQUAL(E_PAR, tk_cre_sem(&(T_CSEM){.exinf = 0,
                                                  .sematr = TA_TFIFO,
                                                  .isemcnt = bad[i][0],
                                                  .maxsem = bad[i][1]}));
  }

  // Each accepted semaphore fills up to its maximum and refuses one more.
  static const INT good[][2] = {{0, 1}, {1, 1}, {0, 32767}, {32767, 32767}};
  for (UINT i = 0; i < sizeof(good) / sizeof(good[0]); ++i) {
    ID semid = create_semaphore(good[i][0], good[i][1]);
    if (good[i][1] > good[i][0]) {
      TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, good[i][1] - good[i][0]));
    }
    TEST_ASSERT_EQUAL(E_QOVR, tk_sig_sem(semid, 1));
    TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  }
}

// A signal larger than the maximum, or one whose sum overflows, is refused,
// and the semaphore keeps the count it had.
static void signal_past_the_maximum_keeps_the_count(void) {
  ID empty = create_semaphore(0, 1);
  TEST_ASSERT_EQUAL(E_QOVR, tk_sig_sem(empty, 2));
  TEST_ASSERT_EQUAL(E_TMOUT, tk_wai_sem(empty, 1, TMO_POL));
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(empty));

  ID full = create_semaphore(1, 1);
  TEST_ASSERT_EQUAL(E_QOVR, tk_sig_sem(full, __INT_MAX__));
  TEST_ASSERT_EQUAL(E_OK, tk_wai_sem(full, 1, TMO_POL));
  TEST_ASSERT_EQUAL(E_TMOUT, tk_wai_sem(full, 1, TMO_POL));
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(full));
}

// The queue-order cases. A waits for two units and is the head of the queue;
// one unit is signalled, which is not enough for A; then B asks for one unit.
// Whether B may take the unit that A cannot use is what the semaphore
// attributes decide. The children only record what they got: a case that
// asserted before it deleted the semaphore would leave A waiting for good, and
// the time-out would skip every later case. Deleting the semaphore ends A with
// E_DLT, whatever the kernel did.
enum { UNSET = 1, QUEUE_TIMEOUT_MS = 1000 };
_Static_assert(UNSET > E_OK, "UNSET must not be a result of tk_wai_sem");

typedef struct {
  ID semid;
  INT units;
  TMO wait_ms;
  volatile ER result;
} request_t;

static void requester(void *arg) {
  request_t *request = arg;
  request->result =
      tk_wai_sem(request->semid, request->units, request->wait_ms);
  test_checkpoint();
}

// Both priorities are above the case, so the request is made, or waits, before
// this returns.
static void start_request(request_t *request, ID semid, INT units, TMO wait_ms,
                          PRI prio) {
  request->semid = semid;
  request->units = units;
  request->wait_ms = wait_ms;
  request->result = UNSET;
  test_spawn(requester, prio, request);
}

// A semaphore that holds one unit, with A waiting at its head for two. The
// signal that leaves the unit goes to *signalled, to be checked with the rest:
// a semaphore that held no unit would refuse a late request on any kernel.
static ID semaphore_with_head_waiting(ATR attr, request_t *head, PRI prio,
                                      ER *signalled) {
  ID semid = create_semaphore_with(attr, 0, 2);
  start_request(head, semid, 2, TMO_FEVR, prio);
  *signalled = tk_sig_sem(semid, 1);
  return semid;
}

// With TA_FIRST the head of the queue is served first, so a later request must
// not take the unit the head cannot use, even from a task of higher priority.
// Without this a large request at the head can starve.
static request_t g_fifo_head;
static request_t g_fifo_late;

static void fifo_first_keeps_the_head_ahead(void) {
  ER signalled;
  ID semid = semaphore_with_head_waiting(TA_TFIFO | TA_FIRST, &g_fifo_head,
                                         SECOND_PRIORITY, &signalled);
  start_request(&g_fifo_late, semid, 1, TMO_POL, FIRST_PRIORITY);

  ER deleted = tk_del_sem(semid);
  test_join_expect(2);

  TEST_ASSERT_EQUAL(E_OK, signalled);
  TEST_ASSERT_EQUAL(E_TMOUT, g_fifo_late.result);
  TEST_ASSERT_EQUAL(E_DLT, g_fifo_head.result);
  TEST_ASSERT_EQUAL(E_OK, deleted);
}

// With TA_TPRI | TA_FIRST a later request passes the head only when its task
// has a strictly higher priority; one of the same priority queues behind it.
typedef struct {
  const char *name;
  PRI head_prio;
  PRI late_prio;
  ER late_result;
} priority_row_t;

enum { PRIORITY_ROWS = 3 };
static const priority_row_t s_priority_rows[PRIORITY_ROWS] = {
    {"late is higher", 3, 2, E_OK},
    {"same priority", 2, 2, E_TMOUT},
    {"late is lower", 2, 3, E_TMOUT},
};
static request_t g_priority_head[PRIORITY_ROWS];
static request_t g_priority_late[PRIORITY_ROWS];

static void tpri_first_lets_only_a_higher_priority_pass(void) {
  ER signalled[PRIORITY_ROWS];
  ER deleted[PRIORITY_ROWS];
  for (int row = 0; row < PRIORITY_ROWS; ++row) {
    ID semid = semaphore_with_head_waiting(
        TA_TPRI | TA_FIRST, &g_priority_head[row],
        s_priority_rows[row].head_prio, &signalled[row]);
    start_request(&g_priority_late[row], semid, 1, TMO_POL,
                  s_priority_rows[row].late_prio);
    deleted[row] = tk_del_sem(semid);
  }
  test_join_expect(2 * PRIORITY_ROWS);

  for (int row = 0; row < PRIORITY_ROWS; ++row) {
    TEST_ASSERT_EQUAL_MESSAGE(E_OK, signalled[row], s_priority_rows[row].name);
    TEST_ASSERT_EQUAL_MESSAGE(s_priority_rows[row].late_result,
                              g_priority_late[row].result,
                              s_priority_rows[row].name);
    TEST_ASSERT_EQUAL_MESSAGE(E_DLT, g_priority_head[row].result,
                              s_priority_rows[row].name);
    TEST_ASSERT_EQUAL_MESSAGE(E_OK, deleted[row], s_priority_rows[row].name);
  }
}

// TA_CNT serves any request the units cover, not only the head's, so a small
// request takes the unit the head cannot use, in either queue order. This
// guards the other attribute against being made as strict as TA_FIRST.
static request_t g_cnt_head[2];

static void cnt_lets_a_small_request_pass(void) {
  static const ATR attrs[2] = {TA_TFIFO | TA_CNT, TA_TPRI | TA_CNT};
  ER signalled[2];
  ER polled[2];
  ER deleted[2];
  for (int i = 0; i < 2; ++i) {
    ID semid = semaphore_with_head_waiting(attrs[i], &g_cnt_head[i],
                                           SECOND_PRIORITY, &signalled[i]);
    polled[i] = tk_wai_sem(semid, 1, TMO_POL);
    deleted[i] = tk_del_sem(semid);
  }
  test_join_expect(2);

  for (int i = 0; i < 2; ++i) {
    TEST_ASSERT_EQUAL_MESSAGE(E_OK, signalled[i], i ? "TA_TPRI" : "TA_TFIFO");
    TEST_ASSERT_EQUAL_MESSAGE(E_OK, polled[i], i ? "TA_TPRI" : "TA_TFIFO");
    TEST_ASSERT_EQUAL_MESSAGE(E_DLT, g_cnt_head[i].result,
                              i ? "TA_TPRI" : "TA_TFIFO");
    TEST_ASSERT_EQUAL_MESSAGE(E_OK, deleted[i], i ? "TA_TPRI" : "TA_TFIFO");
  }
}

// A request that was refused waits in the queue behind the head, and is served
// when the head has been. The checks in between matter: a kernel that serves B
// at once passes a check on the final results alone.
static request_t g_ordered_head;
static request_t g_ordered_late;

static void refused_request_is_served_after_the_head(void) {
  ER signalled;
  ID semid = semaphore_with_head_waiting(TA_TFIFO | TA_FIRST, &g_ordered_head,
                                         SECOND_PRIORITY, &signalled);
  start_request(&g_ordered_late, semid, 1, QUEUE_TIMEOUT_MS, FIRST_PRIORITY);
  ER late_while_head_waits = g_ordered_late.result;

  // The count reaches two: the head takes both units and B keeps waiting.
  ER second_signal = tk_sig_sem(semid, 1);
  ER head_after_second = g_ordered_head.result;
  ER late_after_second = g_ordered_late.result;

  ER third_signal = tk_sig_sem(semid, 1);
  ER late_after_third = g_ordered_late.result;

  ER deleted = tk_del_sem(semid);
  test_join_expect(2);

  TEST_ASSERT_EQUAL(E_OK, signalled);
  TEST_ASSERT_EQUAL(UNSET, late_while_head_waits);
  TEST_ASSERT_EQUAL(E_OK, second_signal);
  TEST_ASSERT_EQUAL(E_OK, head_after_second);
  TEST_ASSERT_EQUAL(UNSET, late_after_second);
  TEST_ASSERT_EQUAL(E_OK, third_signal);
  TEST_ASSERT_EQUAL(E_OK, late_after_third);
  TEST_ASSERT_EQUAL(E_OK, deleted);
}

// On a TA_FIRST semaphore the head of the queue holds the waiters behind it
// back. When the head leaves without its units (here its time limit runs out),
// the units it could not use go to the next waiter at once. The kernel used to
// leave that waiter waiting until the next signal, with the units lying there.
static request_t g_leaving_head;
static request_t g_served_next;

// The head's limit is long enough that a stalled host cannot run it out
// before the signal, which would serve the next waiter by the signal instead.
enum { HEAD_LIMIT_MS = 200 };

static void a_leaving_head_hands_its_units_on(void) {
  ID semid = create_semaphore_with(TA_TFIFO | TA_FIRST, 0, 2);
  start_request(&g_leaving_head, semid, 2, HEAD_LIMIT_MS, SECOND_PRIORITY);
  start_request(&g_served_next, semid, 1, QUEUE_TIMEOUT_MS, FIRST_PRIORITY);
  ER signalled = tk_sig_sem(semid, 1);
  ER next_while_head_waits = g_served_next.result;

  tk_dly_tsk(HEAD_LIMIT_MS + 100); // Past the head's time limit.
  ER head_result = g_leaving_head.result;
  ER next_result = g_served_next.result;

  ER deleted = tk_del_sem(semid);
  test_join_expect(2);

  TEST_ASSERT_EQUAL(E_OK, signalled);
  TEST_ASSERT_EQUAL(UNSET, next_while_head_waits);
  TEST_ASSERT_EQUAL(E_TMOUT, head_result);
  TEST_ASSERT_EQUAL(E_OK, next_result);
  TEST_ASSERT_EQUAL(E_OK, deleted);
}

static const test_case_t s_cases[] = {
    {"binary_semaphore_is_exclusive", binary_semaphore_is_exclusive},
    {"counting_semaphore_grants_whole_requests",
     counting_semaphore_grants_whole_requests},
    {"wait_times_out", wait_times_out},
    {"delete_releases_waiters", delete_releases_waiters},
    {"signal_after_timeout_keeps_the_unit",
     signal_after_timeout_keeps_the_unit},
    {"signal_over_limit_is_refused", signal_over_limit_is_refused},
    {"counts_out_of_range_are_refused", counts_out_of_range_are_refused},
    {"signal_past_the_maximum_keeps_the_count",
     signal_past_the_maximum_keeps_the_count},
    {"fifo_first_keeps_the_head_ahead", fifo_first_keeps_the_head_ahead},
    {"tpri_first_lets_only_a_higher_priority_pass",
     tpri_first_lets_only_a_higher_priority_pass},
    {"cnt_lets_a_small_request_pass", cnt_lets_a_small_request_pass},
    {"refused_request_is_served_after_the_head",
     refused_request_is_served_after_the_head},
    {"a_leaving_head_hands_its_units_on", a_leaving_head_hands_its_units_on},
};

TEST_SUITE_MAIN("semaphore", s_cases, 10000)
