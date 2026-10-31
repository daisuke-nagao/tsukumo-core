/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>
#include <unity.h>

#include "runner.h"

enum {
  UNSET = 1,
  HEAD_LIMIT_MS = 1000,
  OBSERVE_LIMIT_MS = 2000,
  HEAD_PRIORITY = 2,
  NEXT_PRIORITY = 3
};
_Static_assert(HEAD_PRIORITY < NEXT_PRIORITY &&
                   NEXT_PRIORITY < TEST_CASE_PRIORITY,
               "both waiters must run before the case resumes");
_Static_assert(UNSET > E_OK, "UNSET cannot be a wait result");

typedef struct {
  ID semid;
  INT units;
  TMO timeout;
  volatile ER result;
} request_t;

static request_t head_request, next_request;

static void request_units(void *arg) {
  request_t *request = arg;
  request->result =
      tk_wai_sem(request->semid, request->units, request->timeout);
  test_checkpoint();
}

static ID start_waiters(ATR attr, TMO timeout, ID *head) {
  ID semid = tk_cre_sem(&(T_CSEM){.sematr = attr, .maxsem = 2});
  TEST_ASSERT_GREATER_THAN(0, semid);
  head_request = (request_t){semid, 2, timeout, UNSET};
  next_request = (request_t){semid, 1, TMO_FEVR, UNSET};
  *head = test_spawn(request_units, HEAD_PRIORITY, &head_request);
  test_spawn(request_units, NEXT_PRIORITY, &next_request);
  return semid;
}

static void check_departure(ATR attr, BOOL timeout) {
  ID head;
  ID semid = start_waiters(attr, timeout ? HEAD_LIMIT_MS : TMO_FEVR, &head);
  ER head_before = head_request.result;
  ER next_before = next_request.result;
  ER signalled = tk_sig_sem(semid, 1);
  ER head_after_signal = head_request.result;
  ER next_after_signal = next_request.result;
  ER released = E_OK;
  if (timeout) {
    UW started = test_mtime();
    while (head_request.result == UNSET &&
           (UW)(test_mtime() - started) <
               OBSERVE_LIMIT_MS * TEST_MTIME_PER_MS) {
      tk_dly_tsk(10);
    }
  } else {
    released = tk_rel_wai(head);
  }
  ER head_after = head_request.result;
  ER next_after = next_request.result;
  ER remaining = tk_wai_sem(semid, 1, TMO_POL);

  // Clean up before asserting, so a broken handoff cannot strand a child.
  ER deleted = tk_del_sem(semid);
  test_join_expect(2);
  TEST_ASSERT_EQUAL(UNSET, head_before);
  TEST_ASSERT_EQUAL(UNSET, next_before);
  TEST_ASSERT_EQUAL(E_OK, signalled);
  TEST_ASSERT_EQUAL(UNSET, head_after_signal);
  TEST_ASSERT_EQUAL((attr & TA_CNT) ? E_OK : UNSET, next_after_signal);
  TEST_ASSERT_EQUAL(E_OK, released);
  TEST_ASSERT_EQUAL(timeout ? E_TMOUT : E_RLWAI, head_after);
  TEST_ASSERT_EQUAL(E_OK, next_after);
  TEST_ASSERT_EQUAL(E_TMOUT, remaining);
  TEST_ASSERT_EQUAL(E_OK, deleted);
}

static void check_delete(ATR attr) {
  ER head_before[2], next_before[2], signalled[2], head_after_signal[2];
  ER next_after_signal[2], deleted[2], head_after[2], next_after[2];
  for (UINT signal = 0; signal < 2; ++signal) {
    ID head;
    ID semid = start_waiters(attr, TMO_FEVR, &head);
    head_before[signal] = head_request.result;
    next_before[signal] = next_request.result;
    signalled[signal] = signal ? tk_sig_sem(semid, 1) : E_OK;
    head_after_signal[signal] = head_request.result;
    next_after_signal[signal] = next_request.result;
    deleted[signal] = tk_del_sem(semid);
    head_after[signal] = head_request.result;
    next_after[signal] = next_request.result;
    test_join_expect(2 * (signal + 1));
  }
  for (UINT signal = 0; signal < 2; ++signal) {
    BOOL served = signal && (attr & TA_CNT);
    TEST_ASSERT_EQUAL(UNSET, head_before[signal]);
    TEST_ASSERT_EQUAL(UNSET, next_before[signal]);
    TEST_ASSERT_EQUAL(E_OK, signalled[signal]);
    TEST_ASSERT_EQUAL(UNSET, head_after_signal[signal]);
    TEST_ASSERT_EQUAL(served ? E_OK : UNSET, next_after_signal[signal]);
    TEST_ASSERT_EQUAL(E_OK, deleted[signal]);
    TEST_ASSERT_EQUAL(E_DLT, head_after[signal]);
    TEST_ASSERT_EQUAL(served ? E_OK : E_DLT, next_after[signal]);
  }
}

static void fifo_first_timeout(void) {
  check_departure(TA_TFIFO | TA_FIRST, TRUE);
}
static void tpri_first_timeout(void) {
  check_departure(TA_TPRI | TA_FIRST, TRUE);
}
static void fifo_cnt_timeout(void) { check_departure(TA_TFIFO | TA_CNT, TRUE); }
static void tpri_cnt_timeout(void) { check_departure(TA_TPRI | TA_CNT, TRUE); }
static void fifo_first_release(void) {
  check_departure(TA_TFIFO | TA_FIRST, FALSE);
}
static void tpri_first_release(void) {
  check_departure(TA_TPRI | TA_FIRST, FALSE);
}
static void fifo_cnt_release(void) {
  check_departure(TA_TFIFO | TA_CNT, FALSE);
}
static void tpri_cnt_release(void) { check_departure(TA_TPRI | TA_CNT, FALSE); }
static void fifo_first_delete(void) { check_delete(TA_TFIFO | TA_FIRST); }
static void tpri_first_delete(void) { check_delete(TA_TPRI | TA_FIRST); }
static void fifo_cnt_delete(void) { check_delete(TA_TFIFO | TA_CNT); }
static void tpri_cnt_delete(void) { check_delete(TA_TPRI | TA_CNT); }

// Twelve cases plus 32 children use 44 of the runner's 48 stack slots.
static const test_case_t s_cases[] = {
    {"fifo_first_timeout", fifo_first_timeout},
    {"tpri_first_timeout", tpri_first_timeout},
    {"fifo_cnt_timeout", fifo_cnt_timeout},
    {"tpri_cnt_timeout", tpri_cnt_timeout},
    {"fifo_first_release", fifo_first_release},
    {"tpri_first_release", tpri_first_release},
    {"fifo_cnt_release", fifo_cnt_release},
    {"tpri_cnt_release", tpri_cnt_release},
    {"fifo_first_delete", fifo_first_delete},
    {"tpri_first_delete", tpri_first_delete},
    {"fifo_cnt_delete", fifo_cnt_delete},
    {"tpri_cnt_delete", tpri_cnt_delete},
};

TEST_SUITE_MAIN("semaphore-wait-release", s_cases, 5000)
