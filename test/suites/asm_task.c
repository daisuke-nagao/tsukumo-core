/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// TA_ASM tasks: the registers a task entered in assembly gets (the start code
// in a0, exinf in a1, sp at the top of its stack), the three ways such a task
// ends (return, tk_ext_tsk(), tk_exd_tsk()), a restart, and a TCB that
// tk_exd_tsk() freed and a new task reuses. No interrupt is involved; the
// completion trace checks that none completes
// (test/check_interrupt_completion.py).

#include <tk/tkernel.h>
#include <unity.h>

#include "runner.h"

enum { WAIT_MS = 400 };
#define WAIT_UNTIL(cond) TEST_WAIT_UNTIL(cond, WAIT_MS)

// TA_ASM task entry points record the startup ABI and then take one of the
// three supported termination paths. The assembly leaves gp/tp untouched.
extern void asm_task_return_entry(void);
extern void asm_task_ext_entry(void);
extern void asm_task_exd_entry(void);

volatile UW asm_task_seen_stacd;
volatile UW asm_task_seen_exinf;
volatile UW asm_task_seen_sp;
volatile UW asm_task_entry_count;

enum {
  ASM_TASK_PRIORITY = 2,
  ASM_TASK_STACK_WORDS = 128,
  ASM_TASK_CLEANUP_STACD = 0x41534d
};

static _Alignas(16) UW s_asm_return_stack[ASM_TASK_STACK_WORDS];
static _Alignas(16) UW s_asm_ext_stack[ASM_TASK_STACK_WORDS];
static _Alignas(16) UW s_asm_exd_stack[ASM_TASK_STACK_WORDS];
static _Alignas(16) UW s_asm_restart_stack[ASM_TASK_STACK_WORDS];
static _Alignas(16) UW s_asm_reused_stack[ASM_TASK_STACK_WORDS];
static _Alignas(16) UW s_asm_filler_stacks[CFN_MAX_TSKID][ASM_TASK_STACK_WORDS];
static ID s_asm_filler_ids[CFN_MAX_TSKID];
static const UW s_asm_exinf_one, s_asm_exinf_two, s_asm_exinf_three;

static ID create_asm_task(void (*entry)(void), void *exinf, UW *stack,
                          SZ stack_size) {
  return tk_cre_tsk(&(T_CTSK){.exinf = exinf,
                              .tskatr = TA_ASM | TA_USERBUF,
                              .task = (FP)entry,
                              .itskpri = ASM_TASK_PRIORITY,
                              .stksz = stack_size,
                              .bufptr = stack});
}

typedef struct {
  INT stacd;
  UW exinf;
  UW sp;
  UINT entries;
  UINT state;
} asm_entry_observation_t;

static asm_entry_observation_t observe_asm_entry(ID id) {
  BOOL ended = WAIT_UNTIL(test_task_state(id) == TTS_DMT ||
                          test_task_state(id) == TTS_NOEXS);
  TEST_ASSERT_TRUE_MESSAGE(ended, "the ASM task did not terminate");
  return (asm_entry_observation_t){
      .stacd = (INT)asm_task_seen_stacd,
      .exinf = asm_task_seen_exinf,
      .sp = asm_task_seen_sp,
      .entries = asm_task_entry_count,
      .state = test_task_state(id),
  };
}

static void assert_asm_entry(asm_entry_observation_t observed, INT stacd,
                             void *exinf, UW *stack, UINT expected_entries,
                             UINT expected_state) {
  TEST_ASSERT_EQUAL_UINT(expected_state, observed.state);
  TEST_ASSERT_EQUAL_UINT(expected_entries, observed.entries);
  TEST_ASSERT_EQUAL_HEX32((UW)stacd, (UW)observed.stacd);
  TEST_ASSERT_EQUAL_HEX32((UW)exinf, observed.exinf);
  UW begin = (UW)stack;
  UW end = begin + ASM_TASK_STACK_WORDS * sizeof(UW);
  TEST_ASSERT_TRUE_MESSAGE(observed.sp >= begin && observed.sp <= end,
                           "the ASM task stack pointer is outside its stack");
  TEST_ASSERT_EQUAL_HEX32(0, observed.sp % 16);
  TEST_ASSERT_EQUAL_HEX32(end, observed.sp);
}

static void cleanup_dormant_asm_task(ID id) {
  if (test_task_state(id) == TTS_NOEXS) {
    return;
  }
  TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(id, ASM_TASK_CLEANUP_STACD));
  BOOL deleted = WAIT_UNTIL(test_task_state(id) == TTS_NOEXS);
  TEST_ASSERT_TRUE_MESSAGE(deleted,
                           "the ASM task cleanup did not free its TCB");
}

static void asm_task_runs_entry_and_ends_by_every_termination_path(void) {
  struct {
    void (*entry)(void);
    void *exinf;
    UW *stack;
    INT stacd;
    UINT state;
  } cases[] = {{asm_task_return_entry, (void *)&s_asm_exinf_one,
                s_asm_return_stack, 37, TTS_DMT},
               {asm_task_ext_entry, (void *)&s_asm_exinf_two, s_asm_ext_stack,
                -41, TTS_DMT},
               {asm_task_exd_entry, (void *)&s_asm_exinf_three, s_asm_exd_stack,
                -53, TTS_NOEXS}};
  ID ids[sizeof(cases) / sizeof(cases[0])];
  asm_entry_observation_t observed[sizeof(cases) / sizeof(cases[0])];
  for (UINT i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    asm_task_entry_count = 0;
    ID id = create_asm_task(cases[i].entry, cases[i].exinf, cases[i].stack,
                            sizeof(s_asm_return_stack));
    TEST_ASSERT_GREATER_THAN(0, id);
    ids[i] = id;
    TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(id, cases[i].stacd));
    observed[i] = observe_asm_entry(id);
  }
  cleanup_dormant_asm_task(ids[0]);
  cleanup_dormant_asm_task(ids[1]);
  for (UINT i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    assert_asm_entry(observed[i], cases[i].stacd, cases[i].exinf,
                     cases[i].stack, 1, cases[i].state);
  }
}

static void asm_task_restart_rebuilds_entry_and_arguments(void) {
  asm_task_entry_count = 0;
  ID id = create_asm_task(asm_task_return_entry, (void *)&s_asm_exinf_one,
                          s_asm_restart_stack, sizeof(s_asm_restart_stack));
  TEST_ASSERT_GREATER_THAN(0, id);
  TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(id, 123));
  asm_entry_observation_t first = observe_asm_entry(id);
  TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(id, -321));
  asm_entry_observation_t second = observe_asm_entry(id);
  cleanup_dormant_asm_task(id);
  assert_asm_entry(first, 123, (void *)&s_asm_exinf_one, s_asm_restart_stack, 1,
                   TTS_DMT);
  assert_asm_entry(second, -321, (void *)&s_asm_exinf_one, s_asm_restart_stack,
                   2, TTS_DMT);
}

static void asm_task_tcb_reuse_starts_with_fresh_arguments(void) {
  asm_task_entry_count = 0;
  ID deleted = create_asm_task(asm_task_exd_entry, (void *)&s_asm_exinf_one,
                               s_asm_exd_stack, sizeof(s_asm_exd_stack));
  TEST_ASSERT_GREATER_THAN(0, deleted);

  UINT fillers = 0;
  while (fillers < CFN_MAX_TSKID) {
    ID id =
        create_asm_task(asm_task_exd_entry, NULL, s_asm_filler_stacks[fillers],
                        sizeof(s_asm_filler_stacks[fillers]));
    if (id == E_LIMIT) {
      break;
    }
    TEST_ASSERT_GREATER_THAN(0, id);
    s_asm_filler_ids[fillers++] = id;
  }
  TEST_ASSERT_LESS_THAN(CFN_MAX_TSKID, fillers);

  TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(deleted, -101));
  asm_entry_observation_t deleted_run = observe_asm_entry(deleted);
  ID reused = create_asm_task(asm_task_exd_entry, (void *)&s_asm_exinf_two,
                              s_asm_reused_stack, sizeof(s_asm_reused_stack));
  BOOL reused_same_id = deleted == reused;
  ER reused_start = reused > 0 ? tk_sta_tsk(reused, 202) : E_ID;
  asm_entry_observation_t reused_run = {0};
  if (reused_start == E_OK) {
    reused_run = observe_asm_entry(reused);
  }

  for (UINT i = 0; i < fillers; ++i) {
    TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(s_asm_filler_ids[i], (INT)i));
    TEST_ASSERT_EQUAL_UINT(TTS_NOEXS, test_task_state(s_asm_filler_ids[i]));
  }
  TEST_ASSERT_TRUE_MESSAGE(reused_same_id, "the freed TCB was not reused");
  TEST_ASSERT_EQUAL(E_OK, reused_start);
  assert_asm_entry(deleted_run, -101, (void *)&s_asm_exinf_one, s_asm_exd_stack,
                   1, TTS_NOEXS);
  assert_asm_entry(reused_run, 202, (void *)&s_asm_exinf_two,
                   s_asm_reused_stack, 2, TTS_NOEXS);
}

static const test_case_t s_cases[] = {
    CASE(asm_task_runs_entry_and_ends_by_every_termination_path),
    CASE(asm_task_restart_rebuilds_entry_and_arguments),
    CASE(asm_task_tcb_reuse_starts_with_fresh_arguments),
};

TEST_SUITE_MAIN("asm-task", s_cases, 5000)
