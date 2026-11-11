/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// The cost of the timer interrupt handler when it ends as many waits in one
// call as the kernel allows: every task but the idle task, CFN_MAX_TSKID - 1.
// The timer-cost suite stops at 29, because its runner and case task take two
// of the tasks; this suite has no runner and measures the worst cases of that
// suite again at the kernel's limit. See
// docs/developer/notes/timer-delay-budget.md.
//
// usermain makes CFN_MAX_TSKID - 2 children and waits itself, so that all 31
// tasks wait with limits one tick apart, the longest first, and all end in one
// call that comes late. The order matters: the handler scans the timer queue
// for the earliest deadline, and the scan costs most when the queue lists the
// waits latest first. The children sleep between the cases and usermain wakes
// them by number, so they begin their waits in that order every time; woken by
// a semaphore, they would queue in the order their last waits ended, the
// cheapest one. Nobody can make that call late while every task waits:
// the wrapper of the timer handler does, inside the trap, by spinning with
// interrupts off until the passed edges reach every limit, outside the window
// it counts.
//
// Two cases: tk_dly_tsk() waits, and time-outs on a TA_CNT semaphore that
// nothing signals, which may cost a little more than the delays but no walk of
// the wait queue each (timer_cost.c).
//
// Without the runner there is no Unity: a failed TEST_ASSERT would only return.
// The suite checks by hand, prints `:FAIL:` and exits with 1 on a failure, and
// prints the marker the test registration waits for only when everything held.

#include <tk/tkernel.h>

#include "../finish.h"
#include "../putstring.h"
#include "asm/rv32/address.h"
#include "runner.h"
#include "task.h"

extern tkmc_list_head tkmc_timer_queue;
extern void __real_tkmc_timer_handler(void);

enum {
  WAITS = CFN_MAX_TSKID - 1, // Every task but the idle task.
  CHILDREN = WAITS - 1,      // The waits besides usermain's.
  CHILD_PRIORITY = 2,        // Below usermain, so they wait after it.
  CHILD_STACK_WORDS = 128,
  TICK_MS = 10,
  TICK_COUNTS = TICK_MS * TEST_MTIME_PER_MS,
  // usermain waits WAITS * TICK_MS ms, child i (1 to CHILDREN) i ticks less:
  // the kernel counts a limit in ticks rounded up, plus one, so usermain's
  // wait is LONGEST_TICKS ticks and child i's LONGEST_TICKS - i.
  LONGEST_TICKS = WAITS + 1,
  // The edges a call must find passed to end the longest wait: one more than
  // this many ticks late. The least lateness that ends all of them.
  LATE_TICKS = LONGEST_TICKS - 1,
  // The budget of timer_cost.c: a tenth of a tick at -icount shift=5.
  BUDGET_INSTRUCTIONS = 31250,
  SEMAPHORE_EXTRA_INSTRUCTIONS = BUDGET_INSTRUCTIONS / 10,
};
_Static_assert(CHILD_PRIORITY > 1, "usermain (priority 1) must wait first");

enum { CASE_DELAY, CASE_SEMAPHORE, CASES };
static const char *const s_labels[CASES] = {
    "the most waits, one tick apart, as late as the longest",
    "semaphore waits one tick apart, as late as the longest"};

typedef struct {
  UW worst;       // Instructions of the late call.
  UINT edges;     // Tick edges that call found passed.
  UINT ended;     // Waits that call ended.
  BOOL one_tick;  // The waits began in one tick, the longest first.
  BOOL triggered; // The wrapper saw every task waiting.
} record_t;

static record_t s_records[CASES];
static volatile UINT s_case;
static volatile BOOL s_armed;
static volatile UINT s_child_errors;
static ID s_main;
static ID s_children[CHILDREN];
static ID s_done;
static ID s_timed;
static _Alignas(16) UW s_stacks[CHILDREN][CHILD_STACK_WORDS];

static UINT queue_length(void) {
  UINT length = 0;
  for (tkmc_list_head *link = tkmc_timer_queue.next; link != &tkmc_timer_queue;
       link = link->next) {
    ++length;
  }
  return length;
}

static UINT wait_of_the_case(void) {
  return s_case == CASE_SEMAPHORE ? TTW_SEM : TTW_DLY;
}

static BOOL waits(ID id) {
  const volatile TCB *tcb = &tkmc_tcbs[id - 1];
  return tcb->tskstat == TTS_WAI && tcb->tskwait == wait_of_the_case();
}

static UINT waiting_tasks(void) {
  UINT waiting = waits(s_main) ? 1 : 0;
  for (UINT i = 0; i < CHILDREN; ++i) {
    waiting += waits(s_children[i]) ? 1 : 0;
  }
  return waiting;
}

// The queue lists usermain's wait first, then child i's at place i, each one
// tick shorter: the waits began in one tick (one that began after a tick edge
// would have a tick more left), and in the order that costs the scan most.
static BOOL queued_longest_first_in_one_tick(void) {
  UINT place = 0;
  for (tkmc_list_head *link = tkmc_timer_queue.next; link != &tkmc_timer_queue;
       link = link->next, ++place) {
    const TCB *tcb = tkmc_list_entry(link, TCB, head);
    if (tcb->delay_ticks != LONGEST_TICKS - place) {
      return FALSE;
    }
  }
  return place == WAITS &&
         tkmc_list_entry(tkmc_timer_queue.next, TCB, head)->tskid == s_main;
}

void __wrap_tkmc_timer_handler(void) {
  if (!s_armed || queue_length() != WAITS) {
    __real_tkmc_timer_handler();
    return;
  }
  s_armed = FALSE;
  record_t *r = &s_records[s_case];
  r->triggered = TRUE;
  r->one_tick = queued_longest_first_in_one_tick();
  // Late: the pending interrupt is for the edge at the compare value, and the
  // spin goes on to an eighth of a tick past LATE_TICKS edges after it. Under
  // -icount the guest clock follows the instructions, so it is repeatable.
  const UW compare_before = in_w(CLINT_BASE_ADDRESS + CLINT_MTIMECMP_OFFSET);
  const UW until = compare_before + LATE_TICKS * TICK_COUNTS + TICK_COUNTS / 8;
  while ((W)(test_mtime() - until) < 0) {
  }
  const UINT waiting_before = waiting_tasks();
  const UW before = test_instret();
  __real_tkmc_timer_handler();
  r->worst = test_instret() - before;
  r->edges =
      (in_w(CLINT_BASE_ADDRESS + CLINT_MTIMECMP_OFFSET) - compare_before) /
      TICK_COUNTS;
  r->ended = waiting_before - waiting_tasks();
}

static ER timed_wait(UINT ms) {
  return s_case == CASE_SEMAPHORE ? tk_wai_sem(s_timed, 1, (TMO)ms)
                                  : tk_dly_tsk(ms);
}

static ER expected_timed_result(void) {
  return s_case == CASE_SEMAPHORE ? E_TMOUT : E_OK;
}

// Child i (1 to CHILDREN) waits (WAITS - i) * TICK_MS ms in every case.
static void child(INT index, void *exinf) {
  (void)exinf;
  for (UINT c = 0; c < CASES; ++c) {
    if (tk_slp_tsk(TMO_FEVR) != E_OK) {
      s_child_errors += 1;
    }
    if (timed_wait((WAITS - (UINT)index) * TICK_MS) !=
        expected_timed_result()) {
      s_child_errors += 1;
    }
    tk_sig_sem(s_done, 1);
  }
  tk_exd_tsk();
}

static __attribute__((__noreturn__)) void fail(const char *what) {
  putstring(":FAIL: timer-cost-max: ");
  putstring(what);
  putstring("\n");
  test_exit(1);
}

static void putdec(UW value) {
  char digits[11];
  UINT n = 0;
  do {
    digits[n++] = (char)('0' + value % 10);
    value /= 10;
  } while (value != 0);
  while (n > 0) {
    tkmc_putchar(digits[--n]);
  }
}

static void report(UINT c) {
  const record_t *r = &s_records[c];
  putstring("timer-cost-max: ");
  putstring(s_labels[c]);
  putstring(": ");
  putdec(r->ended);
  putstring(" waits ended in one call, which found ");
  putdec(r->edges);
  putstring(" tick edges passed and took ");
  putdec(r->worst);
  putstring(" of ");
  putdec(BUDGET_INSTRUCTIONS);
  putstring(" budgeted instructions\n");
}

static void measure(UINT c) {
  s_case = c;
  s_records[c] = (record_t){0};
  // Start on a tick edge, so that the burst of waits fits in the tick that
  // follows. The children go to sleep meanwhile.
  if (tk_dly_tsk(1) != E_OK) {
    fail("the delay to the tick edge failed");
  }
  s_armed = TRUE;
  // By number: the children become ready, and begin their waits once usermain
  // blocks, in this order.
  for (UINT i = 0; i < CHILDREN; ++i) {
    if (tk_wup_tsk(s_children[i]) != E_OK) {
      fail("a child could not be woken");
    }
  }
  // usermain begins first and waits longest; the children follow as soon as
  // it blocks.
  if (timed_wait(WAITS * TICK_MS) != expected_timed_result()) {
    fail("usermain's own wait ended with the wrong result");
  }
  s_armed = FALSE;
  if (tk_wai_sem(s_done, CHILDREN, TMO_FEVR) != E_OK) {
    fail("the children did not all report back");
  }
}

void usermain(int a0) {
  (void)a0;
  s_main = current->tskid;
  s_done = tk_cre_sem(&(T_CSEM){
      .exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = CHILDREN});
  // Nothing signals it: its waits end by time-out.
  s_timed = tk_cre_sem(&(T_CSEM){
      .exinf = 0, .sematr = TA_TFIFO | TA_CNT, .isemcnt = 0, .maxsem = 1});
  if (s_done <= 0 || s_timed <= 0) {
    fail("a semaphore could not be created");
  }
  T_CTSK packet = {.exinf = NULL,
                   .tskatr = TA_HLNG | TA_USERBUF,
                   .task = (FP)child,
                   .itskpri = CHILD_PRIORITY,
                   .stksz = sizeof(s_stacks[0])};
  for (UINT i = 0; i < CHILDREN; ++i) {
    packet.bufptr = s_stacks[i];
    s_children[i] = tk_cre_tsk(&packet);
    if (s_children[i] <= 0 || tk_sta_tsk(s_children[i], (INT)(i + 1)) != E_OK) {
      fail("a child could not be made");
    }
  }
  // The kernel is at its limit: no task is left to make. The packet is a valid
  // one (a refusal for the packet would not be E_LIMIT), and a refused call
  // does not touch the stack it names.
  packet.bufptr = s_stacks[0];
  if (tk_cre_tsk(&packet) != E_LIMIT) {
    fail("a task was left to make: the waits are not the kernel's most");
  }

  // Each report as soon as its case has run, and every report before any
  // judgement, so that a failure leaves the counts there are.
  for (UINT c = 0; c < CASES; ++c) {
    measure(c);
    report(c);
  }
  if (s_child_errors != 0) {
    fail("a child's wait ended with the wrong result");
  }
  for (UINT c = 0; c < CASES; ++c) {
    const record_t *r = &s_records[c];
    if (!r->triggered) {
      fail("no timer call found every task waiting");
    }
    if (!r->one_tick) {
      fail("the waits did not begin in one tick, the longest first");
    }
    if (r->worst == 0) {
      fail("minstret does not count");
    }
    if (r->edges != LATE_TICKS + 1) {
      fail("the call was not as late as planned");
    }
    if (r->ended != WAITS) {
      fail("the late call did not end every wait");
    }
    if (r->worst > BUDGET_INSTRUCTIONS) {
      fail("the timer handler took more instructions than its budget");
    }
  }
  if (s_records[CASE_SEMAPHORE].worst >
      s_records[CASE_DELAY].worst + SEMAPHORE_EXTRA_INSTRUCTIONS) {
    fail("time-outs on a TA_CNT semaphore cost more than the same delays");
  }
  putstring("TIMER COST MAX OK\n");
  test_exit(0);
}
