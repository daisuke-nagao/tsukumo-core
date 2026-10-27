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
// time-out, a signal after a time-out, a delete with waiters, and the limit.

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

static ID create_semaphore(INT initial, INT maximum) {
  ID semid = tk_cre_sem(&(T_CSEM){
      .exinf = 0, .sematr = TA_TFIFO, .isemcnt = initial, .maxsem = maximum});
  TEST_ASSERT_GREATER_THAN(E_OK, semid);
  return semid;
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

static const test_case_t s_cases[] = {
    {"binary_semaphore_is_exclusive", binary_semaphore_is_exclusive},
    {"counting_semaphore_grants_whole_requests",
     counting_semaphore_grants_whole_requests},
    {"wait_times_out", wait_times_out},
    {"delete_releases_waiters", delete_releases_waiters},
    {"signal_after_timeout_keeps_the_unit",
     signal_after_timeout_keeps_the_unit},
    {"signal_over_limit_is_refused", signal_over_limit_is_refused},
};

TEST_SUITE_MAIN("semaphore", s_cases, 10000)
