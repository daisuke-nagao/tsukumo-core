/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "runner.h"

#include <unity.h>

#include "asm/rv32/address.h"
#include "finish.h"
#include "putstring.h"
#include "task.h"

void (*outputChar)(int) = tkmc_putchar;

// Unity calls these around every case. They run in the runner task, where a
// failed assert would end the runner, so they must not assert.
void setUp(void) {}
void tearDown(void) {}

// The runner has the highest priority so that it keeps polling while a case or
// a child spins at a lower one. A case runs below its children by default (see
// TEST_CASE_PRIORITY in runner.h).
// Only a spin at priority 1, a loop with interrupts disabled, or a broken
// kernel timer can stop the runner; ctest's TIMEOUT is then the only detector
// and no FAIL line is printed.

// Every task gets its own stack and none is handed out twice in one boot:
// there is no way to end a hung task, so reusing its stack would corrupt the
// next case. The suite is a fresh boot, so a fixed pool is enough.
enum { STACK_WORDS = 512, POOL_STACKS = 32, LEDGER_SIZE = 8 };

static UW s_runner_stack[STACK_WORDS] __attribute__((aligned(16)));
static UW s_pool[POOL_STACKS][STACK_WORDS] __attribute__((aligned(16)));
static UINT s_pool_used;

// ID of every child of the running case. 0 means "reserved, not created yet"
// and blocks the case until the registrant fills it in; -1 means "creation or
// start failed" and is ignored. A slot is set to 0 when it is reserved, so a
// value left over from an earlier case never counts.
static ID s_ledger[LEDGER_SIZE];
static UINT s_ledger_count;
// Checkpoints reached by the children of the running case.
static volatile UINT s_checkpoints;
typedef struct {
  void (*fn)(void *);
  void *arg;
} child_t;
static child_t s_child[LEDGER_SIZE];

static const char *s_suite;
static const test_case_t *s_cases;
static UINT s_case_count;
static UW s_limit_counts;
static UINT s_index;
static int s_aborted;

enum { MAX_LIMIT_MS = 400000 };

// mtime of the QEMU virt machine ticks at 10 MHz (TEST_MTIME_PER_MS per ms).
UW test_mtime(void) { return in_w(CLINT_BASE_ADDRESS + CLINT_MTIME_OFFSET); }

UW test_mtime_mid_tick(void) {
  // tk_dly_tsk() returns right after a tick edge. Then spin for half a tick.
  tk_dly_tsk(1);
  UW edge = test_mtime();
  while ((UW)(test_mtime() - edge) < 5 * TEST_MTIME_PER_MS) {
  }
  return test_mtime();
}

// Reads the task table the kernel owns. The tests depend on its layout.
static int task_gone(ID id) {
  return ((volatile TCB *)&tkmc_tcbs[id - 1])->tskstat == TTS_NOEXS;
}

static UW *take_stack(void) {
  UINT intsts;
  UINT slot;
  DI(intsts);
  slot = s_pool_used < POOL_STACKS ? s_pool_used++ : POOL_STACKS;
  EI(intsts);
  return slot < POOL_STACKS ? s_pool[slot] : 0;
}

static int children_gone(void) {
  UINT intsts;
  UINT count;
  DI(intsts);
  count = s_ledger_count;
  EI(intsts);
  for (UINT i = 0; i < count; ++i) {
    ID id = s_ledger[i];
    if (id == 0 || (id > 0 && !task_gone(id))) {
      return 0;
    }
  }
  return 1;
}

// A failed assert ends only the task it runs in. The runner reaches this
// through the hook that unity_config.h calls from every failed assert.
static void end_failed_task(void) { tk_exd_tsk(); }

// Tasks end with tk_exd_tsk(): tk_ext_tsk() would leave the TCB allocated.
static void case_entry(INT stacd, void *exinf) {
  s_cases[s_index].fn();
  tk_exd_tsk();
}

static void child_entry(INT stacd, void *exinf) {
  const child_t *child = exinf;
  child->fn(child->arg);
  tk_exd_tsk();
}

ID test_spawn(void (*fn)(void *), PRI prio, void *arg) {
  // tk_cre_tsk() does not check the priority, and tk_sta_tsk() would index the
  // ready queues with it.
  if (prio < 1 || prio > CFN_MAX_PRI) {
    TEST_FAIL_MESSAGE("test_spawn: priority out of range");
    return -1;
  }
  // The stack comes first: a slot reserved for a child that never gets created
  // would be left looking "not registered yet" and block the case forever.
  UW *stack = take_stack();
  UINT intsts;
  UINT slot;
  DI(intsts);
  slot = s_ledger_count < LEDGER_SIZE ? s_ledger_count++ : LEDGER_SIZE;
  if (slot < LEDGER_SIZE) {
    s_ledger[slot] = 0;
  }
  EI(intsts);
  if (slot >= LEDGER_SIZE || !stack) {
    if (slot < LEDGER_SIZE) {
      s_ledger[slot] = -1;
    }
    TEST_FAIL_MESSAGE("test_spawn: out of ledger slots or stacks");
    return -1;
  }
  s_child[slot].fn = fn;
  s_child[slot].arg = arg;
  ID id = tk_cre_tsk(&(T_CTSK){.exinf = &s_child[slot],
                               .tskatr = TA_HLNG | TA_USERBUF,
                               .task = (FP)child_entry,
                               .itskpri = prio,
                               .stksz = STACK_WORDS * sizeof(UW),
                               .bufptr = stack});
  if (id < 0) {
    s_ledger[slot] = -1;
    TEST_FAIL_MESSAGE("test_spawn: tk_cre_tsk failed");
    return -1;
  }
  // Registered before the child runs: it may be the higher priority and finish
  // before tk_sta_tsk() returns.
  s_ledger[slot] = id;
  if (tk_sta_tsk(id, 0) != E_OK) {
    // The child stays dormant and never ends; do not wait for it.
    s_ledger[slot] = -1;
    TEST_FAIL_MESSAGE("test_spawn: tk_sta_tsk failed");
  }
  return id;
}

void test_join(void) {
  while (!children_gone()) {
    tk_dly_tsk(1);
  }
}

void test_checkpoint(void) {
  __atomic_fetch_add(&s_checkpoints, 1, __ATOMIC_SEQ_CST);
}

void test_join_expect(UINT n) {
  test_join();
  TEST_ASSERT_EQUAL_UINT(n, s_checkpoints);
}

// Fails the running case from the runner itself (a timeout, or a case task that
// could not start). UnityFail() is not used: it prints nothing when an earlier
// failure is already recorded, and its abort hook would end the runner. Unity
// concludes the case once when this returns; concluding it here as well would
// print a second, passing result for the same case.
static void report_failure(const char *message) {
  if (Unity.CurrentTestFailed) {
    UNITY_PRINT_EOL();
  }
  UnityPrint(Unity.TestFile);
  UNITY_OUTPUT_CHAR(':');
  UnityPrintNumber((UNITY_INT)Unity.CurrentTestLineNumber);
  UNITY_OUTPUT_CHAR(':');
  UnityPrint(Unity.CurrentTestName);
  UnityPrint(message);
  Unity.CurrentTestFailed = 1;
}

// Runs one case in its own task and waits for it and its children. The wait
// sleeps with tk_dly_tsk() but measures the deadline on mtime.
static void run_case(void) {
  UnityPrint("RUN ");
  UnityPrint(s_cases[s_index].name);
  UNITY_PRINT_EOL();

  s_ledger_count = 0;
  s_checkpoints = 0;
  UW *stack = take_stack();
  ID id = stack ? tk_cre_tsk(&(T_CTSK){.exinf = 0,
                                       .tskatr = TA_HLNG | TA_USERBUF,
                                       .task = (FP)case_entry,
                                       .itskpri = TEST_CASE_PRIORITY,
                                       .stksz = STACK_WORDS * sizeof(UW),
                                       .bufptr = stack})
                : -1;
  if (id < 0 || tk_sta_tsk(id, 0) != E_OK) {
    report_failure(":FAIL: cannot start the case task");
    s_aborted = 1;
    return;
  }
  UW start = test_mtime();
  while (!(task_gone(id) && children_gone())) {
    if ((UW)(test_mtime() - start) >= s_limit_counts) {
      report_failure(":FAIL: timeout");
      s_aborted = 1;
      return;
    }
    tk_dly_tsk(1);
  }
}

static void runner_entry(INT stacd, void *exinf) {
  test_abort_hook = end_failed_task;
  UnityBegin(s_suite);
  for (s_index = 0; s_index < s_case_count && !s_aborted; ++s_index) {
    UnityDefaultTestRun(run_case, s_cases[s_index].name, __LINE__);
  }
  test_exit(UnityEnd());
}

void test_start_runner(const char *suite, const test_case_t *cases, UINT count,
                       UINT limit_ms) {
  s_suite = suite;
  s_cases = cases;
  s_case_count = count;
  // The limit is measured in 32 bit mtime counts, which wrap after about 429 s;
  // a longer limit would silently become a shorter one.
  if (limit_ms > MAX_LIMIT_MS) {
    limit_ms = MAX_LIMIT_MS;
  }
  s_limit_counts = limit_ms * TEST_MTIME_PER_MS;
  ID id = tk_cre_tsk(&(T_CTSK){.exinf = 0,
                               .tskatr = TA_HLNG | TA_USERBUF,
                               .task = (FP)runner_entry,
                               .itskpri = TEST_RUNNER_PRIORITY,
                               .stksz = sizeof(s_runner_stack),
                               .bufptr = s_runner_stack});
  if (id > 0) {
    tk_sta_tsk(id, 0);
  }
}
