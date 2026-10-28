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
// An interrupt handler runs with interrupts off (a trap clears MIE), so DI()
// stands in for one here: the suites have no way to run code in a handler. A
// handler that enables interrupts again is not covered; the kernel does not
// see it either.
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

static void delay_with_interrupts_off(void) {
  UINT intsts;
  DI(intsts);
  outcome_t o = observe(tk_dly_tsk(WAIT_MS));
  EI(intsts);
  interrupts_on();
  assert_refused(&o);
}

static void sleep_with_interrupts_off(void) {
  UINT intsts;
  DI(intsts);
  outcome_t o = observe(tk_slp_tsk(WAIT_MS));
  EI(intsts);
  interrupts_on();
  assert_refused(&o);
}

static void semaphore_wait_with_interrupts_off(void) {
  s_sem = tk_cre_sem(&(T_CSEM){.sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
  TEST_ASSERT_GREATER_THAN(0, s_sem);
  UINT intsts;
  DI(intsts);
  outcome_t o = observe(tk_wai_sem(s_sem, 1, WAIT_MS));
  EI(intsts);
  interrupts_on();
  assert_refused(&o);
}

static void flag_wait_with_interrupts_off(void) {
  s_flg = tk_cre_flg(&(T_CFLG){.flgatr = TA_WSGL});
  TEST_ASSERT_GREATER_THAN(0, s_flg);
  UINT pattern = 0;
  UINT intsts;
  DI(intsts);
  outcome_t o = observe(tk_wai_flg(s_flg, 0x1, TWF_ORW, &pattern, WAIT_MS));
  EI(intsts);
  interrupts_on();
  assert_refused(&o);
}

static void mailbox_receive_with_interrupts_off(void) {
  s_mbx = tk_cre_mbx(&(T_CMBX){.mbxatr = TA_TFIFO});
  TEST_ASSERT_GREATER_THAN(0, s_mbx);
  T_MSG *message = NULL;
  UINT intsts;
  DI(intsts);
  outcome_t o = observe(tk_rcv_mbx(s_mbx, &message, WAIT_MS));
  EI(intsts);
  interrupts_on();
  assert_refused(&o);
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

static void sleep_refusal_keeps_the_wakeup_request(void) {
  ID child = test_spawn(sleeping_child, LATE_CHILD_PRIORITY, 0);
  TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(child));
  test_join_expect(1);
}

static void semaphore_refusal_keeps_the_unit(void) {
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
}

static void flag_refusal_keeps_the_pattern(void) {
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

static void mailbox_refusal_keeps_the_message(void) {
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
}

// tk_dly_tsk(0) is the wait that does not block. With interrupts on it still
// works, and returns with them on.
static void zero_delay_with_interrupts_on_returns_with_them_on(void) {
  ER ercd = tk_dly_tsk(0);
  UINT probe;
  DI(probe);
  BOOL was_on = !isDI(probe);
  interrupts_on();
  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_TRUE(was_on);
}

static const test_case_t s_cases[] = {
    {"delay_with_interrupts_off", delay_with_interrupts_off},
    {"sleep_with_interrupts_off", sleep_with_interrupts_off},
    {"semaphore_wait_with_interrupts_off", semaphore_wait_with_interrupts_off},
    {"flag_wait_with_interrupts_off", flag_wait_with_interrupts_off},
    {"mailbox_receive_with_interrupts_off",
     mailbox_receive_with_interrupts_off},
    {"sleep_refusal_keeps_the_wakeup_request",
     sleep_refusal_keeps_the_wakeup_request},
    {"semaphore_refusal_keeps_the_unit", semaphore_refusal_keeps_the_unit},
    {"flag_refusal_keeps_the_pattern", flag_refusal_keeps_the_pattern},
    {"mailbox_refusal_keeps_the_message", mailbox_refusal_keeps_the_message},
    {"zero_delay_with_interrupts_on_returns_with_them_on",
     zero_delay_with_interrupts_on_returns_with_them_on},
};

TEST_SUITE_MAIN("wait-context", s_cases, 5000)
