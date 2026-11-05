/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// The idle task must stop the CPU while every other task waits. The guest's
// retired-instruction counter shows it: a case that sleeps for SLEEP_MS runs
// only the tick handlers and the polling of the runner, about 20000
// instructions. An idle task that spins would add a million or more, because
// the guest then runs for as long as the sleep lasts.

#include <tk/tkernel.h>
#include <unity.h>

#include "runner.h"

enum {
  SLEEP_MS = 100,
  TICK_MS = 10,
  // The tick handlers alone run more than this in SLEEP_MS.
  MIN_INSTRUCTIONS = 1000,
  // Far above what the handlers and the runner's polling need during
  // SLEEP_MS, far below the instructions a spinning idle task runs in that
  // time: about 3 million at -icount shift=5, 430000 at shift 8. The suite is
  // registered only for shifts up to 8, where QEMU's minstret counts
  // instructions and the spinning count stays well above this limit.
  INSTRUCTION_LIMIT = 100000
};

static void idle_stops_the_cpu_while_all_tasks_wait(void) {
  // Start on a tick edge, so that the sleep below spans whole ticks.
  TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(1));
  UW mtime_before = test_mtime();
  UW before = test_instret();
  TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(SLEEP_MS));
  UW used = test_instret() - before;
  UW elapsed = test_mtime() - mtime_before;
  // A bound from above alone would also pass if the wait returned at once or
  // the counter did not count.
  TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE((SLEEP_MS - TICK_MS) *
                                                TEST_MTIME_PER_MS,
                                            elapsed, "the sleep ended early");
  TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(MIN_INSTRUCTIONS, used,
                                        "minstret does not count");
  TEST_ASSERT_LESS_THAN_UINT_MESSAGE(INSTRUCTION_LIMIT, used,
                                     "the guest kept running while idle");
}

static void lowest_priority_child(void *arg) {
  (void)arg;
  UW saved_mie;
  // Leave only software interrupts enabled: a yield must not need a tick or
  // an external interrupt to get past idle. A regression hits CTest's timeout.
  __asm__ volatile("csrrw %0, mie, %1"
                   : "=r"(saved_mie)
                   : "r"(1u << 3)
                   : "memory");
  ER result = E_OK;
  for (UINT i = 0; i < 8; ++i) {
    ER ercd = tk_dly_tsk(0);
    if (ercd != E_OK) {
      result = ercd;
      break;
    }
  }
  __asm__ volatile("csrw mie, %0" ::"r"(saved_mie) : "memory");
  TEST_ASSERT_EQUAL(E_OK, result);
  test_checkpoint();
}

static void lowest_priority_yield_resumes_without_a_tick(void) {
  test_spawn(lowest_priority_child, CFN_MAX_PRI, NULL);
  test_join_expect(1);
}

static const test_case_t s_cases[] = {
    {"lowest_priority_yield_resumes_without_a_tick",
     lowest_priority_yield_resumes_without_a_tick},
    {"idle_stops_the_cpu_while_all_tasks_wait",
     idle_stops_the_cpu_while_all_tasks_wait},
};

TEST_SUITE_MAIN("idle-halt", s_cases, 5000)
