/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// How long the timer interrupt handler runs, and whether it stays inside its
// budget. The handler scans the timer queue twice for every wait it ends
// (kernel/timer.c), so the work grows with the square of the waits that end in
// one call, and it runs with interrupts disabled. The budget is a tenth of a
// kernel tick, counted in instructions (see docs/developer/notes/
// timer-delay-budget.md); every case fails when the most expensive handler call
// of the case is over it. The budget is a count of instructions, the same at
// every -icount shift: the shift only sets how many a tick holds, and the
// handler spends the same either way.
//
// The timer trap is the real one. Its C handler is wrapped at link time
// (--wrap=tkmc_timer_handler), so the wrapper reads the guest's
// retired-instruction counter around the handler on the trap path. Reading it
// in the case would add the trap entry and the task switch. The wrapper never
// asserts: it stores what it saw, and the case checks it as a task.
//
// The wrapper watches the children of the case. It keeps the cost of the most
// expensive call, and the number of waits ended by the call that ended the most
// of them. Other waits can end in the same call (the runner polls with its own
// timed sleep), and the report says how many. The case itself blocks without a
// time limit, so its own sleep is never one of them.
//
// A case starts n children that wait for a signal and begin a timed wait right
// after it; they outrank the case, so the whole burst runs inside the call that
// sends the signal, in one tick. The check that the waits began in one tick
// compares the ticks they have left, so it assumes the queue holds a count of
// ticks for each entry, as it does now; a queue that stores differences would
// need another check. The waits are of three kinds:
//
// * all with one limit, so that they end in one call;
// * with limits one tick apart, so that each ends at its own edge, or, when the
//   case holds interrupts off for a while (a late interrupt), in one call that
//   ends every wait the lateness reaches, earliest deadline first;
// * the same, but on a TA_CNT semaphore that nothing signals: the former
// timeout
//   path walked the whole semaphore wait queue; the current path skips that
//   walk.
//
// A child's wait begins in the order of the children, the longest limit first,
// so the queue does not list the waits in the order of their deadlines, and the
// order in which the children leave their waits shows the order in which the
// handler ended them.
//
// A last pair of cases has no children, and holds interrupts off for a number
// of ticks: the cost of the handler then comes from counting the edges it
// missed, one by one, and the difference of the two cases, divided by the
// edges between them, is the cost of one edge. The wrapper also counts the
// edges the handler found, from the compare value it moved, so that a case that
// means to be late fails when it was not.

#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "runner.h"
#include "task.h"

extern tkmc_list_head tkmc_timer_queue;
extern void __real_tkmc_timer_handler(void);

enum {
  CHILD_PRIORITY = 2,
  SLEEP_MS = 50,
  TICK_MS = 10,
  // The budget: a tenth of the 312500 instructions that a tick holds at the
  // default -icount shift 5.
  BUDGET_INSTRUCTIONS = 31250,
  // What 29 time-outs on a TA_CNT semaphore may cost beyond 29 tk_dly_tsk()
  // time-outs that end in the same call: a tenth of the budget. With the walk
  // of the wait queue that the kernel skips under TA_CNT, the 29 time-outs cost
  // about 4200 instructions more than the delays in the minsizerel preset and
  // 12400 more in debug; without it, about 260 and 1700 more. The check is
  // relative, so that it also holds in a preset where the budget alone would
  // not tell the two apart.
  SEMAPHORE_EXTRA_INSTRUCTIONS = BUDGET_INSTRUCTIONS / 10,
  // The kernel has CFN_MAX_TSKID tasks; the idle task, the runner and the case
  // take three of them.
  MOST_CHILDREN = CFN_MAX_TSKID - 3,
  // The machine timer counts of one tick. The kernel keeps its own copy.
  TICK_COUNTS = TICK_MS * TEST_MTIME_PER_MS,
  // Lateness of the interrupt, in ticks, for the cases without children. The
  // runner stops a case after its limit, so the longer one stays well below.
  SHORT_LATE_TICKS = 1,
  LONG_LATE_TICKS = 200
};
_Static_assert(
    CHILD_PRIORITY < TEST_CASE_PRIORITY,
    "a child must preempt the case, to wait before the case goes on");

typedef struct {
  const char *label;
  UINT n;
  // 0: every wait has the limit SLEEP_MS. Else child i waits (n - i) * this
  // many ms, so that the waits end one tick apart, the last child's first.
  UINT limit_step_ms;
  BOOL on_semaphore; // A timed wait on a semaphore, not tk_dly_tsk().
  // Ticks that the case holds interrupts off, once the waits have begun.
  UINT late_ticks;
  // Waits that the call that ended the most of them has to end at least.
  UINT ended_at_least;
} scenario_t;

static ID s_signal;
static ID s_done;
static ID s_timed;
static ID s_children[MOST_CHILDREN];
static BOOL s_on_semaphore;
static UINT s_children_count;
static UINT s_limit_step_ms;
// The place in which each child left its wait: 0 for the first.
static UINT s_rank[MOST_CHILDREN];
static UINT s_next_rank;
// The worst call of the last case that held a late interrupt with waits that
// end by tk_dly_tsk(), for the semaphore case to compare with. 0: not run.
static UW s_delay_late_worst;

// What the wrapper saw. The counts are taken outside the instruction count.
static volatile BOOL s_active;  // Whether the wrapper records.
static volatile UINT s_watched; // The children the wrapper looks at.
static volatile UINT s_ended;   // Waits of the children that a call ended.
static volatile UINT s_removed; // Entries that call took off the timer queue.
static volatile UW s_worst;     // Instructions of the most expensive call.
static volatile UINT s_edges;   // Most tick edges one call found passed.

static UINT queue_length(void) {
  UINT length = 0;
  for (tkmc_list_head *link = tkmc_timer_queue.next; link != &tkmc_timer_queue;
       link = link->next) {
    ++length;
  }
  return length;
}

static UINT waiting_children(UINT watched) {
  const UINT wait = s_on_semaphore ? TTW_SEM : TTW_DLY;
  UINT waiting = 0;
  for (UINT i = 0; i < watched; ++i) {
    const volatile TCB *tcb = &tkmc_tcbs[s_children[i] - 1];
    if (tcb->tskstat == TTS_WAI && tcb->tskwait == wait) {
      ++waiting;
    }
  }
  return waiting;
}

void __wrap_tkmc_timer_handler(void) {
  UINT watched = s_watched;
  UINT waiting_before = waiting_children(watched);
  UINT length_before = queue_length();
  // The handler moves the compare value on by one tick for each edge it finds
  // passed.
  const UW compare_before = in_w(CLINT_BASE_ADDRESS + CLINT_MTIMECMP_OFFSET);
  UW before = test_instret();
  __real_tkmc_timer_handler();
  UW used = test_instret() - before;
  const UINT edges =
      (in_w(CLINT_BASE_ADDRESS + CLINT_MTIMECMP_OFFSET) - compare_before) /
      TICK_COUNTS;
  UINT ended = waiting_before - waiting_children(watched);
  if (!s_active) {
    return;
  }
  if (used > s_worst) {
    s_worst = used;
  }
  if (edges > s_edges) {
    s_edges = edges;
  }
  if (ended > s_ended) {
    s_ended = ended;
    s_removed = length_before - queue_length();
  }
}

static void child(void *arg) {
  const UINT index = (UINT)(UW)arg;
  const UINT ms = s_limit_step_ms != 0
                      ? (s_children_count - index) * s_limit_step_ms
                      : (UINT)SLEEP_MS;
  TEST_ASSERT_EQUAL(E_OK, tk_wai_sem(s_signal, 1, TMO_FEVR));
  if (s_on_semaphore) {
    TEST_ASSERT_EQUAL(E_TMOUT, tk_wai_sem(s_timed, 1, ms));
  } else {
    TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(ms));
  }
  s_rank[index] = __atomic_fetch_add(&s_next_rank, 1, __ATOMIC_SEQ_CST);
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(s_done, 1));
  test_checkpoint();
}

// Every child waits for its own number of ticks: child i has the first one's,
// less one for each step. One that started in an earlier tick has fewer left,
// so a burst that crossed a tick edge is caught here and not taken for a
// measurement.
static void assert_all_wait_as_planned(const scenario_t *s) {
  const UINT wait = s->on_semaphore ? TTW_SEM : TTW_DLY;
  const UINT first = tkmc_tcbs[s_children[0] - 1].delay_ticks;
  for (UINT i = 0; i < s->n; ++i) {
    const volatile TCB *tcb = &tkmc_tcbs[s_children[i] - 1];
    TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_WAI, tcb->tskstat,
                                   "a child is not waiting");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(wait, tcb->tskwait,
                                   "a child is not in the wait of the case");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        first - (s->limit_step_ms != 0 ? i : 0), tcb->delay_ticks,
        "the waits did not start in one tick, so they will not end as planned");
  }
}

// The children leave their waits in the order of their deadlines, which is the
// reverse of the order in which they began to wait.
static void assert_waits_ended_in_deadline_order(const scenario_t *s) {
  for (UINT i = 0; s->limit_step_ms != 0 && i < s->n; ++i) {
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        s->n - 1 - i, s_rank[i],
        "the waits did not end in the order of their deadlines");
  }
}

// Holds interrupts off until the machine time is late_ticks ticks and an eighth
// of a tick past the compare value, which is the edge that the pending
// interrupt is for. The handler then finds late_ticks + 1 edges passed, if it
// ends its count before the next edge: the time it counts in is ahead of the
// edge by seven eighths of a tick, which a slower clock (a larger -icount shift
// runs fewer instructions in it) would take up sooner. Under -icount the guest
// clock follows the instructions, so the spin itself is repeatable.
static void hold_interrupts_off(UINT late_ticks) {
  UINT intsts;
  DI(intsts);
  const UW compare = in_w(CLINT_BASE_ADDRESS + CLINT_MTIMECMP_OFFSET);
  const UW until = compare + late_ticks * TICK_COUNTS + TICK_COUNTS / 8;
  while ((W)(test_mtime() - until) < 0) {
  }
  EI(intsts);
}

static void report(const char *label) {
  UnityPrint("timer-cost: ");
  UnityPrint(label);
  UnityPrint(": ");
  UnityPrintNumberUnsigned(s_ended);
  UnityPrint(" waits and ");
  UnityPrintNumberUnsigned(s_removed > s_ended ? s_removed - s_ended : 0);
  UnityPrint(" others ended in one call: the worst call took ");
  UnityPrintNumberUnsigned(s_worst);
  UnityPrint(" of ");
  UnityPrintNumberUnsigned(BUDGET_INSTRUCTIONS);
  UnityPrint(
      " budgeted instructions in the timer handler, which found at most ");
  UnityPrintNumberUnsigned(s_edges);
  UnityPrint(" tick edges passed");
  UNITY_PRINT_EOL();
}

// A case that holds interrupts off for late_ticks ticks means the handler to
// find late_ticks + 1 edges passed in one call. Without this a late interrupt
// that never came would pass as a cheap on-time one.
static void assert_found_the_planned_edges(UINT late_ticks) {
  if (late_ticks != 0) {
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        late_ticks + 1, s_edges,
        "the interrupt was not as late as the case plans");
  }
}

static void assert_within_budget(void) {
  TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(0, s_worst, "minstret does not count");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      BUDGET_INSTRUCTIONS, s_worst,
      "the timer handler took more instructions than its budget");
}

static void clear_records(void) {
  s_ended = 0;
  s_removed = 0;
  s_worst = 0;
  s_edges = 0;
}

// n timed waits, and the instructions the handler took while they ended.
static void measure(const scenario_t *s) {
  s_active = FALSE;
  s_watched = 0;
  s_on_semaphore = s->on_semaphore;
  s_signal = tk_cre_sem(&(T_CSEM){
      .exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = (INT)s->n});
  TEST_ASSERT_GREATER_THAN(E_OK, s_signal);
  s_done = tk_cre_sem(&(T_CSEM){
      .exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = (INT)s->n});
  TEST_ASSERT_GREATER_THAN(E_OK, s_done);
  if (s->on_semaphore) {
    // Nothing signals it: the waits end by time-out. The former TA_CNT timeout
    // path walked this queue; the current path skips that walk.
    s_timed = tk_cre_sem(&(T_CSEM){
        .exinf = 0, .sematr = TA_TFIFO | TA_CNT, .isemcnt = 0, .maxsem = 1});
    TEST_ASSERT_GREATER_THAN(E_OK, s_timed);
  }
  s_children_count = s->n;
  s_limit_step_ms = s->limit_step_ms;
  s_next_rank = 0;
  for (UINT i = 0; i < s->n; ++i) {
    s_children[i] = test_spawn(child, CHILD_PRIORITY, (void *)(UW)i);
  }

  clear_records();
  s_watched = s->n;
  s_active = TRUE;
  // The children start their waits right after the signal. The edge makes the
  // whole burst fit in the tick that follows.
  TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(1));
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(s_signal, (INT)s->n));
  assert_all_wait_as_planned(s);
  if (s->late_ticks != 0) {
    hold_interrupts_off(s->late_ticks);
  }

  // No time limit, so that the case adds no entry to the timer queue.
  TEST_ASSERT_EQUAL(E_OK, tk_wai_sem(s_done, (INT)s->n, TMO_FEVR));
  s_active = FALSE;
  s_watched = 0;
  test_join_expect(s->n);
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(s_signal));
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(s_done));
  if (s->on_semaphore) {
    TEST_ASSERT_EQUAL(E_OK, tk_del_sem(s_timed));
  }
  report(s->label);
  assert_found_the_planned_edges(s->late_ticks);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(
      s->ended_at_least, s_ended,
      "no timer interrupt ended as many waits as the case plans");
  assert_waits_ended_in_deadline_order(s);
  assert_within_budget();
}

// No waits of the children, an interrupt that is late_ticks late.
static void measure_late_interrupt(const char *label, UINT late_ticks) {
  s_watched = 0;
  clear_records();
  s_active = TRUE;
  // Start just after an edge, so that the hold begins a whole tick before the
  // next one.
  TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(1));
  clear_records();
  hold_interrupts_off(late_ticks);
  s_active = FALSE;
  report(label);
  assert_found_the_planned_edges(late_ticks);
  assert_within_budget();
}

static const scenario_t s_one = {"one wait", 1, 0, FALSE, 0, 1};
static const scenario_t s_eight = {"eight waits", 8, 0, FALSE, 0, 8};
static const scenario_t s_most = {"the most waits", MOST_CHILDREN, 0, FALSE, 0,
                                  MOST_CHILDREN};
static const scenario_t s_apart = {"the most waits, limits one tick apart",
                                   MOST_CHILDREN,
                                   TICK_MS,
                                   FALSE,
                                   0,
                                   1};
static const scenario_t s_apart_late_1 = {
    "limits one tick apart, 1 tick late", MOST_CHILDREN, TICK_MS, FALSE, 1, 1};
static const scenario_t s_apart_late_most = {
    "limits one tick apart, as late as the most waits",
    MOST_CHILDREN,
    TICK_MS,
    FALSE,
    MOST_CHILDREN,
    MOST_CHILDREN};
static const scenario_t s_semaphore_late_most = {
    "semaphore waits one tick apart, as late as the most waits",
    MOST_CHILDREN,
    TICK_MS,
    TRUE,
    MOST_CHILDREN,
    MOST_CHILDREN};

static void one_wait_ends_in_a_tick(void) { measure(&s_one); }
static void eight_waits_end_in_one_tick(void) { measure(&s_eight); }
static void the_most_waits_end_in_one_tick(void) { measure(&s_most); }
static void the_most_waits_end_one_by_one(void) { measure(&s_apart); }
static void waits_one_tick_apart_end_after_a_late_tick(void) {
  measure(&s_apart_late_1);
}
static void waits_one_tick_apart_all_end_after_a_late_interrupt(void) {
  measure(&s_apart_late_most);
  s_delay_late_worst = s_worst;
}
// The same waits as the case before, on a TA_CNT semaphore: its time-outs may
// cost a little more, for the semaphore, but not a walk of the wait queue each.
static void semaphore_waits_all_time_out_after_a_late_interrupt(void) {
  measure(&s_semaphore_late_most);
  TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
      0, s_delay_late_worst, "the case to compare with did not run before");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      s_delay_late_worst + SEMAPHORE_EXTRA_INSTRUCTIONS, s_worst,
      "time-outs on a TA_CNT semaphore cost more than the same delays");
}
static void a_short_late_interrupt_counts_the_missed_edges(void) {
  measure_late_interrupt("no waits, 1 tick late", SHORT_LATE_TICKS);
}
static void a_long_late_interrupt_counts_the_missed_edges(void) {
  measure_late_interrupt("no waits, 200 ticks late", LONG_LATE_TICKS);
}

static const test_case_t s_cases[] = {
    {"one_wait_ends_in_a_tick", one_wait_ends_in_a_tick},
    {"eight_waits_end_in_one_tick", eight_waits_end_in_one_tick},
    {"the_most_waits_end_in_one_tick", the_most_waits_end_in_one_tick},
    {"the_most_waits_end_one_by_one", the_most_waits_end_one_by_one},
    {"waits_one_tick_apart_end_after_a_late_tick",
     waits_one_tick_apart_end_after_a_late_tick},
    {"waits_one_tick_apart_all_end_after_a_late_interrupt",
     waits_one_tick_apart_all_end_after_a_late_interrupt},
    {"semaphore_waits_all_time_out_after_a_late_interrupt",
     semaphore_waits_all_time_out_after_a_late_interrupt},
    {"a_short_late_interrupt_counts_the_missed_edges",
     a_short_late_interrupt_counts_the_missed_edges},
    {"a_long_late_interrupt_counts_the_missed_edges",
     a_long_late_interrupt_counts_the_missed_edges},
};

TEST_SUITE_MAIN("timer-cost", s_cases, 5000)
