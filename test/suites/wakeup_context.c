/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// tk_wup_tsk() called from a real timer interrupt: the ISR that interrupted a
// task may queue a wake-up request for that task, while the task itself may not
// (see test/suites/task_timer.c, tk_wup_tsk_refuses_task_waking_itself). The
// kernel tells the two apart by the ISR state of the RV32 trap entries, not by
// MIE.
//
// The timer trap is the real one. Its C handler is wrapped at link time
// (--wrap=tkmc_timer_handler), so the call under test runs on the trap path
// with tkmc_isr_depth set, in the task that the trap interrupted. Calling the
// handler from a task would prove nothing about that. The wrapper never
// asserts and ends no task: it stores what it saw, and the case checks it after
// the trap has returned, as a task.
//
// Wake-up requests are counted in two parts, so that a value put there by the
// test is not mistaken for one the kernel made: the case seeds the count of its
// own task with interrupts off, and expects the seed plus at most the one
// request of the ISR, and nothing else, to be used up by later polls.

#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "context.h"
#include "runner.h"
#include "task.h"

extern tkmc_list_head tkmc_timer_queue;
extern void __real_tkmc_timer_handler(void);

enum {
  // The kernel's limit on queued wake-up requests (TK_WAKEUP_MAXCNT),
  // written out on purpose: a test that used the constant would pass
  // whatever its value.
  WAKEUP_LIMIT = 4095,
  CHILD_PRIORITY = 2,
  SLEEP_MS = 1000, // The longest a child may sleep if nothing wakes it.
  SPIN_MS = 2000   // The longest a case waits for the timer trap.
};
_Static_assert(CHILD_PRIORITY < TEST_CASE_PRIORITY,
               "the child must preempt the case, to sleep before it goes on");
_Static_assert(SLEEP_MS < SPIN_MS, "a child must give up before the case does");

typedef struct {
  ER ercd;
  UINT count_before;
  UINT count_after;
  BOOL rest_unchanged;
  BOOL in_isr_before;
  BOOL in_isr_after;
  BOOL irq_before;
  BOOL irq_after;
  UINT target_state_after;
  BOOL child_done;
} observation_t;

// What the wrapper does once, in the first timer trap that interrupts the task
// of the case. Written by the case with interrupts off, before arming.
static volatile BOOL s_armed;
static volatile BOOL s_observed;
static TCB *volatile s_interrupted;
static volatile ID s_target;
static volatile BOOL s_enable_mie;
static volatile observation_t s_obs;

// Written by the sleeping child.
static volatile ER s_child_result;
static volatile BOOL s_child_after_isr;
static volatile BOOL s_child_in_isr;
static volatile BOOL s_child_done;

// The kernel before the call, to compare with it after: every task, every
// ready queue, the timer queue, and the current and next task.
static struct {
  TCB tasks[CFN_MAX_TSKID];
  tkmc_list_head ready[CFN_MAX_PRI];
  tkmc_list_head timer;
  TCB *running;
  TCB *scheduled;
} s_before;

static void capture(void) {
  test_copy_bytes(s_before.tasks, tkmc_tcbs, sizeof(s_before.tasks));
  test_copy_bytes(s_before.ready, tkmc_ready_queue, sizeof(s_before.ready));
  test_copy_bytes(&s_before.timer, &tkmc_timer_queue, sizeof(s_before.timer));
  s_before.running = current;
  s_before.scheduled = next;
}

// Everything captured is as it was, but for the wake-up count of the target.
static BOOL unchanged_but_for_the_count_of(ID target) {
  enum { SKIP = __builtin_offsetof(TCB, wupcnt) }; // No <stddef.h> here.
  for (UINT i = 0; i < CFN_MAX_TSKID; ++i) {
    const unsigned char *was = (const unsigned char *)&s_before.tasks[i];
    const unsigned char *now = (const unsigned char *)&tkmc_tcbs[i];
    if (i + 1 != (UINT)target) {
      if (!test_equal_bytes(was, now, sizeof(TCB))) {
        return FALSE;
      }
    } else if (!test_equal_bytes(was, now, SKIP) ||
               !test_equal_bytes(was + SKIP + sizeof(UINT),
                                 now + SKIP + sizeof(UINT),
                                 sizeof(TCB) - SKIP - sizeof(UINT))) {
      return FALSE;
    }
  }
  return test_equal_bytes(s_before.ready, tkmc_ready_queue,
                          sizeof(s_before.ready)) &&
         test_equal_bytes(&s_before.timer, &tkmc_timer_queue,
                          sizeof(s_before.timer)) &&
         s_before.running == current && s_before.scheduled == next;
}

void __wrap_tkmc_timer_handler(void) {
  // No check of the ISR state here: it is recorded below as in_isr_before and
  // checked by the case, since nothing in an ISR may end the test.
  __real_tkmc_timer_handler();
  if (!s_armed || current != s_interrupted) {
    return;
  }
  s_armed = FALSE;

  // A defensive probe of the timer path, not a test of nesting: MIE is on for
  // the call, but every interrupt source is masked, so no trap can nest.
  // Nested external interrupts are tested in the interrupt suite.
  UINT saved_sources = 0;
  if (s_enable_mie) {
    __asm__ volatile("csrrw %0, mie, zero" : "=r"(saved_sources)::"memory");
    EI(0);
  }

  ID target = s_target;
  capture();
  s_obs.in_isr_before = tkmc_in_isr();
  s_obs.irq_before = test_irq_enabled();
  s_obs.count_before = tkmc_tcbs[target - 1].wupcnt;
  s_obs.ercd = tk_wup_tsk(target);
  s_obs.irq_after = test_irq_enabled();
  s_obs.in_isr_after = tkmc_in_isr();
  s_obs.count_after = tkmc_tcbs[target - 1].wupcnt;
  s_obs.target_state_after = tkmc_tcbs[target - 1].tskstat;
  s_obs.rest_unchanged = unchanged_but_for_the_count_of(target);
  s_obs.child_done = s_child_done;

  if (s_enable_mie) {
    UINT ignored;
    DI(ignored);
    __asm__ volatile("csrw mie, %0" ::"r"(saved_sources) : "memory");
  }
  s_observed = TRUE;
}

// Arms the wrapper for the next timer trap that interrupts the running task.
// What the wrapper reads is published with interrupts off, so that no trap can
// see it half written. seed is the wake-up count to start from, or -1 to leave
// the count alone.
static void arm(ID target, BOOL enable_mie, INT seed) {
  UINT intsts;
  DI(intsts);
  s_interrupted = current;
  s_target = target;
  s_enable_mie = enable_mie;
  s_observed = FALSE;
  if (seed >= 0) {
    tkmc_tcbs[target - 1].wupcnt = (UINT)seed;
  }
  s_armed = TRUE;
  EI(intsts);
}

// Spins for the trap. No assert here: a failed one would end the case task
// before its children were collected.
static void wait_for_the_trap(void) {
  UW started = test_mtime();
  while (!s_observed &&
         (UW)(test_mtime() - started) < SPIN_MS * TEST_MTIME_PER_MS) {
  }
}

static ID own_id(void) {
  UINT intsts;
  DI(intsts);
  ID id = current->tskid;
  EI(intsts);
  return id;
}

// An ISR interrupts the running task and queues one request for it, unless the
// count is full. The request is real: polls use up exactly the seed and that
// one request, and then time out.
static void isr_wakes_the_interrupted_task(UINT seed, BOOL enable_mie) {
  ID self = own_id();
  arm(self, enable_mie, (INT)seed);
  wait_for_the_trap();

  UINT intsts;
  DI(intsts);
  BOOL in_isr_now = tkmc_in_isr();
  UINT count = tkmc_tcbs[self - 1].wupcnt;
  EI(intsts);

  UINT generated = seed < WAKEUP_LIMIT ? 1 : 0;
  TEST_ASSERT_TRUE_MESSAGE(s_observed, "no timer trap reached the case");
  TEST_ASSERT_TRUE(s_obs.in_isr_before);
  TEST_ASSERT_TRUE(s_obs.in_isr_after);
  TEST_ASSERT_FALSE(in_isr_now);
  TEST_ASSERT_EQUAL(enable_mie, s_obs.irq_before);
  TEST_ASSERT_EQUAL(enable_mie, s_obs.irq_after);
  TEST_ASSERT_EQUAL(generated ? E_OK : E_QOVR, s_obs.ercd);
  TEST_ASSERT_EQUAL_UINT(seed, s_obs.count_before);
  TEST_ASSERT_EQUAL_UINT(seed + generated, s_obs.count_after);
  TEST_ASSERT_TRUE_MESSAGE(s_obs.rest_unchanged,
                           "the call changed more than the wake-up count");
  TEST_ASSERT_EQUAL_UINT(seed + generated, count);

  for (UINT i = 0; i < seed + generated; ++i) {
    TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(TMO_POL));
  }
  TEST_ASSERT_EQUAL(E_TMOUT, tk_slp_tsk(TMO_POL));
}

static void tk_wup_tsk_from_isr_queues_wakeup_for_interrupted_task(void) {
  isr_wakes_the_interrupted_task(0, FALSE);
}

static void tk_wup_tsk_from_isr_queues_last_wakeup_slot(void) {
  isr_wakes_the_interrupted_task(WAKEUP_LIMIT - 1, FALSE);
}

static void tk_wup_tsk_from_isr_refuses_wakeup_past_limit(void) {
  isr_wakes_the_interrupted_task(WAKEUP_LIMIT, FALSE);
}

// MIE is on while the ISR calls, as a handler that enabled interrupts would
// have it. The kernel still sees an ISR: it does not decide by MIE.
static void tk_wup_tsk_from_isr_with_mie_on_is_still_isr(void) {
  isr_wakes_the_interrupted_task(0, TRUE);
}

// A task that sleeps is woken by the ISR, not given a request. The child has
// the higher priority, so it runs as soon as the trap has returned and not
// before.
static void sleeping_child(void *arg) {
  (void)arg;
  s_child_result = tk_slp_tsk(SLEEP_MS);
  s_child_after_isr = s_observed;
  s_child_in_isr = tkmc_in_isr();
  s_child_done = TRUE;
  test_checkpoint();
}

static void tk_wup_tsk_from_isr_wakes_sleeping_task(void) {
  ID child = test_spawn(sleeping_child, CHILD_PRIORITY, NULL);
  arm(child, FALSE, -1);
  wait_for_the_trap();
  // The child's sleep is bounded, so this returns even if nothing woke it.
  test_join_expect(1);

  TEST_ASSERT_TRUE_MESSAGE(s_observed, "no timer trap reached the case");
  TEST_ASSERT_TRUE(s_obs.in_isr_before);
  TEST_ASSERT_TRUE(s_obs.in_isr_after);
  TEST_ASSERT_EQUAL(E_OK, s_obs.ercd);
  TEST_ASSERT_EQUAL_UINT(0, s_obs.count_before);
  TEST_ASSERT_EQUAL_UINT(0, s_obs.count_after);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, s_obs.target_state_after);
  TEST_ASSERT_FALSE_MESSAGE(s_obs.child_done, "the child ran inside the ISR");
  TEST_ASSERT_TRUE(s_child_done);
  TEST_ASSERT_TRUE_MESSAGE(s_child_after_isr, "the child ran before the ISR");
  TEST_ASSERT_FALSE(s_child_in_isr);
  TEST_ASSERT_EQUAL(E_OK, s_child_result);
}

static const test_case_t s_cases[] = {
    CASE(tk_wup_tsk_from_isr_queues_wakeup_for_interrupted_task),
    CASE(tk_wup_tsk_from_isr_queues_last_wakeup_slot),
    CASE(tk_wup_tsk_from_isr_refuses_wakeup_past_limit),
    CASE(tk_wup_tsk_from_isr_with_mie_on_is_still_isr),
    CASE(tk_wup_tsk_from_isr_wakes_sleeping_task),
};

// Five cases, one stack each, and one child in the last: far below the runner's
// 48 stacks, and one child is within the slots of a case's ledger.
TEST_SUITE_MAIN("wakeup-context", s_cases, 5000)
