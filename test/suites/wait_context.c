/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Wait calls made with interrupts off. A task is switched out when interrupts
// come back on, so a wait call made with them off could only return at once,
// with the task still in the waiting state. The kernel refuses it with E_CTX
// and changes nothing.
//
// These cases cover task-side DI(). The task-context suites separately enter
// real timer/dispatch traps and check the MIE-independent ISR state.
//
// Each case looks at the state of the running task before it enables
// interrupts again, because the stranded wait ends by itself as soon as they
// are on. The waits have a time limit, so a kernel that does not refuse them
// fails an assert instead of hanging the suite.

#include <tk/tkernel.h>
#include <unity.h>

#include "runner.h"
#include "task.h"

enum { WAIT_MS = 50, LATE_CHILD_PRIORITY = 5 };
_Static_assert(LATE_CHILD_PRIORITY > TEST_CASE_PRIORITY,
               "the child must not run until the case blocks");

static ID s_sem;
static ID s_flg;
static ID s_mbx;
static T_MSG s_message;
static T_MSG s_sentinel;

typedef struct {
  ER ercd;
  UINT tskstat;
  UINT tskwait;
  UINT delay_ticks;
  BOOL interrupts_off;
} outcome_t;

// Records what a call left behind. Call it with interrupts off, right after
// the call under test.
static outcome_t observe(ER ercd) {
  volatile TCB *self = current;
  outcome_t o = {.ercd = ercd,
                 .tskstat = self->tskstat,
                 .tskwait = self->tskwait,
                 .delay_ticks = self->delay_ticks};
  // DI() returns the state it found: interrupts were off if the bit is clear.
  UINT probe;
  DI(probe);
  o.interrupts_off = isDI(probe);
  return o;
}

// The call was refused and the task is still the plain running task. Interrupts
// are on again, so a failed assert can end the task.
static void assert_refused(const outcome_t *o) {
  TEST_ASSERT_EQUAL(E_CTX, o->ercd);
  TEST_ASSERT_TRUE(o->interrupts_off);
  TEST_ASSERT_EQUAL_UINT(TTS_RUN, o->tskstat);
  TEST_ASSERT_EQUAL_UINT(0, o->tskwait);
  TEST_ASSERT_EQUAL_UINT(0, o->delay_ticks);
}

// Runs with the interrupts the caller had on, whatever a failed call did.
static void interrupts_on(void) { EI(0); }

// Makes the call with interrupts off and checks that it was refused and left
// the plain running task.
static void check_refused_with_interrupts_off(ER (*call)(void)) {
  UINT intsts;
  DI(intsts);
  outcome_t o = observe(call());
  EI(intsts);
  interrupts_on();
  assert_refused(&o);
}

static ER call_dly_tsk(void) { return tk_dly_tsk(WAIT_MS); }
static ER call_slp_tsk(void) { return tk_slp_tsk(WAIT_MS); }
static ER wait_semaphore(void) { return tk_wai_sem(s_sem, 1, WAIT_MS); }
static ER wait_flag(void) {
  UINT pattern = 0;
  return tk_wai_flg(s_flg, 0x1, TWF_ORW, &pattern, WAIT_MS);
}
static ER receive_message(void) {
  T_MSG *message = NULL;
  return tk_rcv_mbx(s_mbx, &message, WAIT_MS);
}

static void tk_dly_tsk_with_interrupts_off_is_refused(void) {
  check_refused_with_interrupts_off(call_dly_tsk);
}

static void tk_slp_tsk_with_interrupts_off_is_refused(void) {
  check_refused_with_interrupts_off(call_slp_tsk);
}

static void tk_wai_sem_with_interrupts_off_is_refused(void) {
  s_sem = tk_cre_sem(&(T_CSEM){.sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
  TEST_ASSERT_GREATER_THAN(0, s_sem);
  check_refused_with_interrupts_off(wait_semaphore);
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(s_sem));
}

static void tk_wai_flg_with_interrupts_off_is_refused(void) {
  s_flg = tk_cre_flg(&(T_CFLG){.flgatr = TA_WSGL});
  TEST_ASSERT_GREATER_THAN(0, s_flg);
  check_refused_with_interrupts_off(wait_flag);
}

static void tk_rcv_mbx_with_interrupts_off_is_refused(void) {
  s_mbx = tk_cre_mbx(&(T_CMBX){.mbxatr = TA_TFIFO});
  TEST_ASSERT_GREATER_THAN(0, s_mbx);
  check_refused_with_interrupts_off(receive_message);
  TEST_ASSERT_EQUAL(E_OK, tk_del_mbx(s_mbx));
}

// A refusal uses up nothing, even when the call could succeed at once: what it
// asked for is still there for the next call. The next call is a poll with
// interrupts on, which returns E_OK once and E_TMOUT after that.

// The task has a wake-up request queued before it sleeps. It runs after the
// case, which queues the request first.
static void sleeping_child(void *arg) {
  (void)arg;
  UINT intsts;
  DI(intsts);
  ER refused = tk_slp_tsk(TMO_POL);
  EI(intsts);
  interrupts_on();
  TEST_ASSERT_EQUAL(E_CTX, refused);
  TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(TMO_POL));
  TEST_ASSERT_EQUAL(E_TMOUT, tk_slp_tsk(TMO_POL));
  test_checkpoint();
}

static void tk_slp_tsk_refusal_keeps_wakeup_request(void) {
  ID child = test_spawn(sleeping_child, LATE_CHILD_PRIORITY, 0);
  TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(child));
  test_join_expect(1);
}

static void tk_wai_sem_refusal_keeps_unit(void) {
  s_sem = tk_cre_sem(&(T_CSEM){.sematr = TA_TFIFO, .isemcnt = 1, .maxsem = 1});
  TEST_ASSERT_GREATER_THAN(0, s_sem);
  UINT intsts;
  DI(intsts);
  ER refused = tk_wai_sem(s_sem, 1, TMO_POL);
  EI(intsts);
  interrupts_on();
  TEST_ASSERT_EQUAL(E_CTX, refused);
  TEST_ASSERT_EQUAL(E_OK, tk_wai_sem(s_sem, 1, TMO_POL));
  TEST_ASSERT_EQUAL(E_TMOUT, tk_wai_sem(s_sem, 1, TMO_POL));
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(s_sem));
}

static void tk_wai_flg_refusal_keeps_pattern(void) {
  s_flg = tk_cre_flg(&(T_CFLG){.flgatr = TA_WSGL});
  TEST_ASSERT_GREATER_THAN(0, s_flg);
  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(s_flg, 0x1));
  UINT pattern = 0;
  UINT intsts;
  DI(intsts);
  ER refused = tk_wai_flg(s_flg, 0x1, TWF_ORW | TWF_CLR, &pattern, TMO_POL);
  EI(intsts);
  interrupts_on();
  TEST_ASSERT_EQUAL(E_CTX, refused);
  TEST_ASSERT_EQUAL_UINT(0, pattern);
  TEST_ASSERT_EQUAL(
      E_OK, tk_wai_flg(s_flg, 0x1, TWF_ORW | TWF_CLR, &pattern, TMO_POL));
  TEST_ASSERT_EQUAL_UINT(0x1, pattern);
  TEST_ASSERT_EQUAL(
      E_TMOUT, tk_wai_flg(s_flg, 0x1, TWF_ORW | TWF_CLR, &pattern, TMO_POL));
}

static void tk_rcv_mbx_refusal_keeps_message(void) {
  s_mbx = tk_cre_mbx(&(T_CMBX){.mbxatr = TA_TFIFO});
  TEST_ASSERT_GREATER_THAN(0, s_mbx);
  TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(s_mbx, &s_message));
  T_MSG *message = &s_sentinel;
  UINT intsts;
  DI(intsts);
  ER refused = tk_rcv_mbx(s_mbx, &message, TMO_POL);
  EI(intsts);
  interrupts_on();
  TEST_ASSERT_EQUAL(E_CTX, refused);
  TEST_ASSERT_EQUAL_PTR(&s_sentinel, message);
  TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(s_mbx, &message, TMO_POL));
  TEST_ASSERT_EQUAL_PTR(&s_message, message);
  TEST_ASSERT_EQUAL(E_TMOUT, tk_rcv_mbx(s_mbx, &message, TMO_POL));
  TEST_ASSERT_EQUAL(E_OK, tk_del_mbx(s_mbx));
}

// tk_dly_tsk(0) is the wait that does not block. With interrupts on it returns
// E_OK at once, and with them still on.
static void tk_dly_tsk_zero_with_interrupts_on_returns_with_them_on(void) {
  ER ercd = tk_dly_tsk(0);
  UINT probe;
  DI(probe);
  BOOL was_on = !isDI(probe);
  interrupts_on();
  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_TRUE(was_on);
}

static const test_case_t s_cases[] = {
    CASE(tk_dly_tsk_with_interrupts_off_is_refused),
    CASE(tk_slp_tsk_with_interrupts_off_is_refused),
    CASE(tk_wai_sem_with_interrupts_off_is_refused),
    CASE(tk_wai_flg_with_interrupts_off_is_refused),
    CASE(tk_rcv_mbx_with_interrupts_off_is_refused),
    CASE(tk_slp_tsk_refusal_keeps_wakeup_request),
    CASE(tk_wai_sem_refusal_keeps_unit),
    CASE(tk_wai_flg_refusal_keeps_pattern),
    CASE(tk_rcv_mbx_refusal_keeps_message),
    CASE(tk_dly_tsk_zero_with_interrupts_on_returns_with_them_on),
};

TEST_SUITE_MAIN("wait-context", s_cases, 5000)
