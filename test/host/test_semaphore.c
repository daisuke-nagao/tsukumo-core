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
// without waiting until none is left.
//
// The limits are written as numbers, not as TK_SEMAPHORE_MAXCNT, so that a
// wrong value in the profile fails here.

#include <limits.h>

#include <tk/tkernel.h>
#include <unity.h>

#include "fakes.h"
#include "semaphore.h"
#include "task.h"

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
