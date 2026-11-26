/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// The way out of an external interrupt: the TA_ASM path through
// tkmc_prepare_external_interrupt() and tkmc_finish_external_interrupt(),
// EndOfInt() completing a claim before the handler exits, and tk_ret_int()
// refusing every context it cannot return from.

#include <stdio.h>
#include <string.h>

#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "context.h"
#include "fakes.h"
#include "intctl.h"

enum { SOME_SOURCE = 10 };
enum { OUTER_SOURCE = 10, INNER_SOURCE = 11 };

// Defines intno as a TA_ASM handler at level, raises it, and takes it as the
// RV32 trap entry would: in the ISR state, with interrupts off. The caller
// finishes the interrupt and leaves the context.
static host_context_t take_asm_interrupt(tkmc_interrupt_context *context,
                                         UINT intno, INT level, UW *frame) {
  const T_DINT assembler = {.intatr = TA_ASM,
                            .inthdr = (FP)host_counting_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(intno, &assembler));
  EnableInt(intno, level);
  host_plic_raise(intno);
  const host_context_t entered = host_enter(TRUE, TRUE);
  tkmc_prepare_external_interrupt(context, frame);
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_ASM, context->kind);
  TEST_ASSERT_EQUAL_UINT(intno, context->intno);
  return entered;
}

static void
test_asm_interrupt_prepare_and_finish_restore_threshold_and_complete_claim(
    void) {
  _Alignas(16) UW frame[32] = {0};
  tkmc_interrupt_context context;

  host_plic_set_threshold(2);
  const host_context_t entered =
      take_asm_interrupt(&context, SOME_SOURCE, 4, frame);
  TEST_ASSERT_EQUAL_PTR(host_counting_handler, context.handler);
  TEST_ASSERT_EQUAL_PTR(frame, context.frame);
  TEST_ASSERT_EQUAL_PTR(&context, context.entry_sp);
  TEST_ASSERT_EQUAL_UINT(1, context.depth);
  TEST_ASSERT_EQUAL_UINT(2, context.saved_threshold);
  TEST_ASSERT_EQUAL_UINT(4, host_plic_threshold());
  TEST_ASSERT_EQUAL_UINT(0, host_handler_calls);

  DisableInt(SOME_SOURCE);
  tkmc_finish_external_interrupt(&context);
  TEST_ASSERT_EQUAL_UINT(2, host_plic_threshold());
  TEST_ASSERT_FALSE(host_plic_is_enabled(SOME_SOURCE));
  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());
  TEST_ASSERT_EQUAL_UINT(1, host_write_count(host_plic_claim_port()));
  TEST_ASSERT_EQUAL_UINT(SOME_SOURCE, host_last_write(host_plic_claim_port()));
  TEST_ASSERT_EQUAL_UINT(0, host_handler_calls);
  TEST_ASSERT_FALSE(host_interrupts_enabled());
  host_leave(entered);
}

static void test_asm_interrupt_prepare_without_claim_is_no_op(void) {
  _Alignas(16) UW frame[32] = {0};
  tkmc_interrupt_context context;
  UINT saved;
  host_plic_set_threshold(3);
  host_log_count = 0;
  DI(saved);
  tkmc_isr_depth = 1;
  tkmc_prepare_external_interrupt(&context, frame);
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_NONE, context.kind);
  TEST_ASSERT_EQUAL_UINT(0, context.intno);
  tkmc_finish_external_interrupt(&context);
  TEST_ASSERT_EQUAL_UINT(3, host_plic_threshold());
  TEST_ASSERT_EQUAL_UINT(0, host_write_count(host_plic_claim_port()));
  TEST_ASSERT_FALSE(host_interrupts_enabled());
  tkmc_isr_depth = 0;
  EI(saved);
}

static void
test_asm_interrupt_nested_contexts_restore_each_claim_and_threshold(void) {
  _Alignas(16) UW outer_frame[32] = {0};
  _Alignas(16) UW inner_frame[32] = {0};
  const T_DINT outer = {.intatr = TA_ASM, .inthdr = (FP)host_counting_handler};
  const T_DINT inner = {.intatr = TA_ASM, .inthdr = (FP)host_counting_handler};
  tkmc_interrupt_context outer_context, inner_context;
  UINT saved;

  TEST_ASSERT_EQUAL(E_OK, tk_def_int(OUTER_SOURCE, &outer));
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(INNER_SOURCE, &inner));
  EnableInt(OUTER_SOURCE, 2);
  EnableInt(INNER_SOURCE, 5);
  host_plic_set_threshold(1);
  tkmc_isr_depth = 1;
  host_plic_raise(OUTER_SOURCE);
  DI(saved);
  tkmc_prepare_external_interrupt(&outer_context, outer_frame);
  TEST_ASSERT_EQUAL_UINT(2, host_plic_threshold());

  tkmc_isr_depth = 2;
  host_plic_raise(INNER_SOURCE);
  tkmc_prepare_external_interrupt(&inner_context, inner_frame);
  TEST_ASSERT_EQUAL_PTR(&outer_context, inner_context.previous);
  TEST_ASSERT_EQUAL_UINT(5, host_plic_threshold());
  tkmc_finish_external_interrupt(&inner_context);
  tkmc_isr_depth = 1;
  TEST_ASSERT_EQUAL_UINT(2, host_plic_threshold());
  tkmc_finish_external_interrupt(&outer_context);
  TEST_ASSERT_EQUAL_UINT(1, host_plic_threshold());
  TEST_ASSERT_EQUAL_UINT(2, host_write_count(host_plic_claim_port()));
  TEST_ASSERT_EQUAL_UINT(OUTER_SOURCE, host_last_write(host_plic_claim_port()));
  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());
  TEST_ASSERT_FALSE(host_interrupts_enabled());

  tkmc_isr_depth = 0;
  EI(saved);
}

static void test_end_of_int_completes_claim_once_before_handler_exits(void) {
  _Alignas(16) UW frame[32] = {0};
  tkmc_interrupt_context context;
  host_plic_set_threshold(2);
  const host_context_t entered =
      take_asm_interrupt(&context, SOME_SOURCE, 3, frame);
  host_plic_set_enabled(SOME_SOURCE, FALSE);
  host_plic_raise(INNER_SOURCE);
  host_log_count = 0;

  EndOfInt(SOME_SOURCE);
  TEST_ASSERT_EQUAL_UINT(1, host_write_count(host_plic_claim_port()));
  TEST_ASSERT_EQUAL_UINT(SOME_SOURCE, host_last_write(host_plic_claim_port()));
  TEST_ASSERT_EQUAL_UINT(0, host_access_count(host_plic_claim_port(), FALSE));
  TEST_ASSERT_FALSE(host_plic_is_enabled(SOME_SOURCE));
  TEST_ASSERT_TRUE(CheckInt(INNER_SOURCE));
  EndOfInt(SOME_SOURCE);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, host_write_count(host_plic_claim_port()),
                                 "a second EndOfInt() completed again");
  TEST_ASSERT_FALSE(host_interrupts_enabled());
  tkmc_finish_external_interrupt(&context);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, host_write_count(host_plic_claim_port()),
                                 "the handler exit completed again");
  host_leave(entered);

  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());
  TEST_ASSERT_EQUAL_UINT(2, host_plic_threshold());
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_ASM | TKMC_INTERRUPT_COMPLETED,
                         context.kind);
  TEST_ASSERT_TRUE(host_interrupts_enabled());
}

static void test_end_of_int_completes_outer_nested_claim_without_popping(void) {
  _Alignas(16) UW outer_frame[32] = {0};
  _Alignas(16) UW inner_frame[32] = {0};
  tkmc_interrupt_context outer, inner;
  UINT saved;
  const T_DINT assembler = {.intatr = TA_ASM,
                            .inthdr = (FP)host_counting_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(OUTER_SOURCE, &assembler));
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(INNER_SOURCE, &assembler));
  EnableInt(OUTER_SOURCE, 2);
  EnableInt(INNER_SOURCE, 5);
  host_plic_set_threshold(1);
  DI(saved);
  tkmc_isr_depth = 1;
  host_plic_raise(OUTER_SOURCE);
  tkmc_prepare_external_interrupt(&outer, outer_frame);
  tkmc_isr_depth = 2;
  host_plic_raise(INNER_SOURCE);
  tkmc_prepare_external_interrupt(&inner, inner_frame);
  host_log_count = 0;

  EndOfInt(OUTER_SOURCE);
  TEST_ASSERT_EQUAL_UINT(1, host_write_count(host_plic_claim_port()));
  TEST_ASSERT_TRUE((outer.kind & TKMC_INTERRUPT_COMPLETED) != 0);
  TEST_ASSERT_TRUE((inner.kind & TKMC_INTERRUPT_COMPLETED) == 0);
  TEST_ASSERT_EQUAL_PTR(&inner, tkmc_active_interrupt_context);
  TEST_ASSERT_EQUAL_PTR(&outer, inner.previous);
  TEST_ASSERT_EQUAL_UINT(2, tkmc_isr_depth);
  TEST_ASSERT_EQUAL_UINT(5, host_plic_threshold());
  tkmc_finish_external_interrupt(&inner);
  tkmc_isr_depth = 1;
  tkmc_finish_external_interrupt(&outer);
  TEST_ASSERT_EQUAL_UINT(2, host_write_count(host_plic_claim_port()));
  tkmc_isr_depth = 0;
  EI(saved);

  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());
  TEST_ASSERT_EQUAL_UINT(1, host_plic_threshold());
}

static void test_end_of_int_works_with_mie_on_and_no_current_task(void) {
  _Alignas(16) UW frame[32] = {0};
  tkmc_interrupt_context context;
  tkmc_interrupt_context other = {.intno = INNER_SOURCE,
                                  .depth = 0,
                                  .saved_threshold = 7,
                                  .kind = TKMC_INTERRUPT_KIND_HLNG};
  const tkmc_interrupt_context other_before = other;
  tkmc_interrupt_context expected;
  const INT priority_port = host_plic_priority_port(SOME_SOURCE);
  const INT other_priority_port = host_plic_priority_port(INNER_SOURCE);
  host_plic_set_threshold(1);
  EnableInt(INNER_SOURCE, 5);
  tkmc_active_interrupt_context = &other;
  const host_context_t entered =
      take_asm_interrupt(&context, SOME_SOURCE, 2, frame);
  expected = context;
  expected.kind |= TKMC_INTERRUPT_COMPLETED;
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_ASM | TKMC_INTERRUPT_COMPLETED,
                         expected.kind);
  current = next = NULL;
  EI(0);
  host_plic_raise(INNER_SOURCE);
  const UW priority_before = in_w(priority_port);
  const UW other_priority_before = in_w(other_priority_port);
  const UW threshold_before = host_plic_threshold();
  host_log_count = 0;
  host_snapshot_t before;
  host_take_snapshot(&before);

  EndOfInt(SOME_SOURCE);
  TEST_ASSERT_EQUAL_UINT(1, host_write_count(host_plic_claim_port()));
  TEST_ASSERT_EQUAL_UINT(0, host_access_count(host_plic_claim_port(), FALSE));
  host_assert_unchanged(&before,
                        "EndOfInt changed task queues or dispatch state");
  TEST_ASSERT_EQUAL_UINT(1, tkmc_isr_depth);
  TEST_ASSERT_EQUAL_PTR(&context, tkmc_active_interrupt_context);
  TEST_ASSERT_EQUAL_PTR(&other, context.previous);
  TEST_ASSERT_EQUAL_MEMORY(&expected, &context, sizeof(context));
  TEST_ASSERT_EQUAL_MEMORY(&other_before, &other, sizeof(other));
  TEST_ASSERT_TRUE(host_plic_is_enabled(SOME_SOURCE));
  TEST_ASSERT_TRUE(host_plic_is_enabled(INNER_SOURCE));
  TEST_ASSERT_EQUAL_UINT(priority_before, in_w(priority_port));
  TEST_ASSERT_EQUAL_UINT(other_priority_before, in_w(other_priority_port));
  TEST_ASSERT_EQUAL_UINT(threshold_before, host_plic_threshold());
  TEST_ASSERT_TRUE(CheckInt(INNER_SOURCE));
  TEST_ASSERT_TRUE(host_interrupts_enabled());
  tkmc_finish_external_interrupt(&context);
  tkmc_active_interrupt_context = NULL;
  host_leave(entered);

  TEST_ASSERT_TRUE(host_interrupts_enabled());
  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());
}

static void
test_end_of_int_refuses_invalid_inactive_unmatched_calls_without_mmio(void) {
  _Alignas(16) UW frame[32] = {0};
  tkmc_interrupt_context context;
  host_log_count = 0;
  EndOfInt(0);
  EndOfInt(CFN_MAX_INTNO);
  EndOfInt(0xffffffffu);
  EndOfInt(SOME_SOURCE);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, host_log_count, "no handler is running");

  const host_context_t entered =
      take_asm_interrupt(&context, SOME_SOURCE, 2, frame);
  host_log_count = 0;
  EndOfInt(INNER_SOURCE);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, host_log_count, "another source");
  TEST_ASSERT_TRUE((context.kind & TKMC_INTERRUPT_COMPLETED) == 0);
  tkmc_finish_external_interrupt(&context);
  host_leave(entered);

  TEST_ASSERT_EQUAL_UINT(1, host_write_count(host_plic_claim_port()));
}

static void test_end_of_int_stops_at_completed_newest_matching_record(void) {
  tkmc_interrupt_context older = {.intno = SOME_SOURCE,
                                  .kind = TKMC_INTERRUPT_KIND_ASM};
  tkmc_interrupt_context newer = {.previous = &older,
                                  .intno = SOME_SOURCE,
                                  .kind = TKMC_INTERRUPT_KIND_HLNG |
                                          TKMC_INTERRUPT_COMPLETED};
  tkmc_active_interrupt_context = &newer;
  host_log_count = 0;
  EndOfInt(SOME_SOURCE);
  TEST_ASSERT_EQUAL_UINT(0, host_log_count);
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_ASM, older.kind);
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_HLNG | TKMC_INTERRUPT_COMPLETED,
                         newer.kind);
  tkmc_active_interrupt_context = NULL;
}

static void
test_asm_interrupt_prepare_clears_stale_completion_of_reused_record(void) {
  _Alignas(16) UW frame[32] = {0};
  tkmc_interrupt_context context = {.kind = TKMC_INTERRUPT_KIND_ASM |
                                            TKMC_INTERRUPT_COMPLETED};
  const host_context_t entered =
      take_asm_interrupt(&context, SOME_SOURCE, 2, frame);
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_ASM, context.kind);
  tkmc_finish_external_interrupt(&context);
  host_leave(entered);

  TEST_ASSERT_EQUAL_UINT(1, host_write_count(host_plic_claim_port()));
}

// tk_ret_int() from entry_sp stops in tkmc_fatal_context() before it changes
// anything: no task, no claim completed, the interrupt records and the
// threshold as they were.
static void assert_ret_int_stops(void *entry_sp) {
  host_log_count = 0;
  host_snapshot_t before;
  host_take_snapshot(&before);
  tkmc_interrupt_context *const active_before = tkmc_active_interrupt_context;
  const tkmc_interrupt_context record_before =
      active_before != NULL ? *active_before : (tkmc_interrupt_context){0};
  const UW threshold_before = host_plic_threshold();
  host_fatal_t fatal;
  HOST_CATCH_FATAL_CONTEXT(fatal, tkmc_ret_int_from_sp(entry_sp));
  TEST_ASSERT_TRUE_MESSAGE(fatal.stopped,
                           "tk_ret_int returned in a bad context");
  TEST_ASSERT_EQUAL(TKMC_CONTEXT_RET_INT, fatal.api);
  TEST_ASSERT_TRUE(fatal.interrupts_off);
  host_assert_unchanged(&before, "invalid tk_ret_int changed kernel state");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, host_write_count(host_plic_claim_port()),
                                 "invalid tk_ret_int completed a claim");
  TEST_ASSERT_EQUAL_PTR(active_before, tkmc_active_interrupt_context);
  if (active_before != NULL) {
    TEST_ASSERT_EQUAL_MEMORY(&record_before, active_before,
                             sizeof(record_before));
  }
  TEST_ASSERT_EQUAL_UINT(threshold_before, host_plic_threshold());
  host_check_kernel_queues();
}

static void test_tk_ret_int_stops_in_non_asm_contexts(void) {
  UINT saved;
  // Task context with interrupts on or off.
  for (UINT enabled = 0; enabled < 2; ++enabled) {
    host_reset();
    host_two_tasks_and_objects();
    UINT saved = 0;
    if (!enabled) {
      DI(saved);
    }
    assert_ret_int_stops(NULL);
    EI(saved);
  }

  // No current task, and an ISR context without an active ASM external frame
  // (the host model cannot distinguish timer from software trap entry).
  host_reset();
  host_two_tasks_and_objects();
  current = NULL;
  assert_ret_int_stops(NULL);
  EI(0);

  host_reset();
  host_two_tasks_and_objects();
  tkmc_isr_depth = 1;
  DI(saved);
  assert_ret_int_stops(NULL);
  tkmc_isr_depth = 0;
  EI(saved);

  // An active high-level external handler is still not a tk_ret_int caller.
  host_reset();
  host_two_tasks_and_objects();
  TEST_ASSERT_EQUAL(
      E_OK, tk_def_int(10, &(T_DINT){.intatr = TA_HLNG,
                                     .inthdr = (FP)host_counting_handler}));
  EnableInt(10, 1);
  host_plic_raise(10);
  tkmc_isr_depth = 1;
  DI(saved);
  _Alignas(16) UW frame[32] = {0};
  tkmc_interrupt_context context;
  tkmc_prepare_external_interrupt(&context, frame);
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_HLNG, context.kind);
  // Keep task tables unchanged, then finish the synthetic trap so its claim
  // does not leak into another host test.
  assert_ret_int_stops(&context);
  tkmc_finish_external_interrupt(&context);
  tkmc_isr_depth = 0;
  EI(0);
}

static void test_tk_ret_int_rejects_invalid_asm_contexts(void) {
  _Alignas(16) UW frame[32] = {0};
  tkmc_interrupt_context context;

  // A context record is usable only from the entry stack pointer associated
  // with that exact claim.
  host_reset();
  host_two_tasks_and_objects();
  (void)take_asm_interrupt(&context, 10, 3, frame);
  assert_ret_int_stops((unsigned char *)&context + sizeof(void *));
  tkmc_finish_external_interrupt(&context);
  tkmc_isr_depth = 0;
  EI(0);

  // A record from an outer depth cannot be returned while a nested trap is
  // active.
  host_reset();
  host_two_tasks_and_objects();
  (void)take_asm_interrupt(&context, 10, 3, frame);
  tkmc_isr_depth = 2;
  assert_ret_int_stops(&context);
  tkmc_isr_depth = 1;
  tkmc_finish_external_interrupt(&context);
  tkmc_isr_depth = 0;
  EI(0);

  // The public entry is an interrupt exit and requires MIE to be off at its
  // call site. A valid ASM record with MIE on must fail before completing it.
  host_reset();
  host_two_tasks_and_objects();
  (void)take_asm_interrupt(&context, 10, 3, frame);
  EI(0);
  assert_ret_int_stops(&context);
  tkmc_finish_external_interrupt(&context);
  tkmc_isr_depth = 0;
  EI(0);

  // A valid active ASM record is still rejected when no task is current.
  host_reset();
  host_two_tasks_and_objects();
  (void)take_asm_interrupt(&context, 10, 3, frame);
  TCB *saved_current = current;
  current = NULL;
  assert_ret_int_stops(&context);
  current = saved_current;
  tkmc_finish_external_interrupt(&context);
  tkmc_isr_depth = 0;
  EI(0);

  // The inner HLNG record remains invalid even when its previous record is a
  // valid ASM external interrupt.
  _Alignas(16) UW inner_frame[32] = {0};
  tkmc_interrupt_context outer, inner;
  host_reset();
  host_two_tasks_and_objects();
  TEST_ASSERT_EQUAL(
      E_OK, tk_def_int(11, &(T_DINT){.intatr = TA_HLNG,
                                     .inthdr = (FP)host_counting_handler}));
  EnableInt(11, 5);
  (void)take_asm_interrupt(&outer, 10, 3, frame);
  host_plic_raise(11);
  tkmc_isr_depth = 2;
  tkmc_prepare_external_interrupt(&inner, inner_frame);
  TEST_ASSERT_EQUAL_PTR(&outer, inner.previous);
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_HLNG, inner.kind);
  assert_ret_int_stops(&inner);
  tkmc_finish_external_interrupt(&inner);
  tkmc_isr_depth = 1;
  tkmc_finish_external_interrupt(&outer);
  tkmc_isr_depth = 0;
  EI(0);
}

void run_asm_interrupt_tests(void) {
  UnitySetTestFile(__FILE__);
  RUN_TEST(
      test_asm_interrupt_prepare_and_finish_restore_threshold_and_complete_claim);
  RUN_TEST(test_asm_interrupt_prepare_without_claim_is_no_op);
  RUN_TEST(test_asm_interrupt_nested_contexts_restore_each_claim_and_threshold);
  RUN_TEST(test_end_of_int_completes_claim_once_before_handler_exits);
  RUN_TEST(test_end_of_int_completes_outer_nested_claim_without_popping);
  RUN_TEST(test_end_of_int_works_with_mie_on_and_no_current_task);
  RUN_TEST(
      test_end_of_int_refuses_invalid_inactive_unmatched_calls_without_mmio);
  RUN_TEST(test_end_of_int_stops_at_completed_newest_matching_record);
  RUN_TEST(test_asm_interrupt_prepare_clears_stale_completion_of_reused_record);
  RUN_TEST(test_tk_ret_int_stops_in_non_asm_contexts);
  RUN_TEST(test_tk_ret_int_rejects_invalid_asm_contexts);
}
