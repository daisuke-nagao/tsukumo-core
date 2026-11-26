/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Interrupt handlers and the interrupt controller: tk_def_int(), the external
// interrupt entry with its default handler, EnableInt() and DisableInt(), and
// the PLIC threshold while a handler runs. The claims the QEMU machine cannot
// be made to give (a claim that finds nothing, a source above the table) are
// only tested here.

#include <stdio.h>
#include <string.h>

#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "context.h"
#include "fakes.h"
#include "intctl.h"

enum { SOME_SOURCE = 10 };
enum { OUTER_SOURCE = 10, INNER_SOURCE = 11, QUIET_SOURCE = 12 };

static void
test_external_interrupt_with_no_source_calls_no_handler_and_completes_nothing(
    void) {
  TEST_ASSERT_EQUAL(
      E_OK,
      tk_def_int(SOME_SOURCE, &(T_DINT){.intatr = TA_HLNG,
                                        .inthdr = (FP)host_counting_handler}));
  host_plic_set_enabled(SOME_SOURCE, TRUE);
  tkmc_external_interrupt();
  TEST_ASSERT_EQUAL_UINT(0, host_handler_calls);
  TEST_ASSERT_EQUAL_UINT(0, host_write_count(host_plic_claim_port()));
  TEST_ASSERT_TRUE(host_plic_is_enabled(SOME_SOURCE));
}

// A source above the table has no handler, so the default handler runs for it:
// it is switched off, and no other source is. Its word of enable bits is the
// one of its own number, not the first.
static void
test_default_handler_switches_off_source_beyond_table_and_completes_it(void) {
  TEST_ASSERT_EQUAL(
      E_OK,
      tk_def_int(SOME_SOURCE, &(T_DINT){.intatr = TA_HLNG,
                                        .inthdr = (FP)host_counting_handler}));
  for (UINT src = CFN_MAX_INTNO; src < PLIC_SOURCE_COUNT; ++src) {
    for (UINT other = 1; other < PLIC_SOURCE_COUNT; ++other) {
      host_plic_set_enabled(other, TRUE);
    }
    host_plic_set_priority(src, 1);
    host_log_count = 0; // One claim leaves a few entries; the log is small.
    host_plic_raise(src);
    tkmc_external_interrupt();
    for (UINT other = 1; other < PLIC_SOURCE_COUNT; ++other) {
      TEST_ASSERT_EQUAL_MESSAGE(other != src, host_plic_is_enabled(other),
                                "only the claimed source is switched off");
    }
    TEST_ASSERT_EQUAL_UINT(src, host_last_write(host_plic_claim_port()));
  }
  TEST_ASSERT_EQUAL_UINT(0, host_handler_calls);
}

// The first and the last number of the table are served like any other, and
// with their own number.
static void
test_tk_def_int_serves_first_and_last_interrupt_number_with_own_number(void) {
  static const UINT edges[] = {1, CFN_MAX_INTNO - 1};
  for (UINT i = 0; i < 2; ++i) {
    host_handler_calls = 0;
    host_handler_intno = 0;
    TEST_ASSERT_EQUAL(E_OK, tk_def_int(edges[i], &host_counting_hlng));
    EnableInt(edges[i], 1);
    host_plic_raise(edges[i]);
    tkmc_external_interrupt();
    TEST_ASSERT_EQUAL_UINT(1, host_handler_calls);
    TEST_ASSERT_EQUAL_UINT(edges[i], host_handler_intno);
  }
}

// The enable bits of 32 sources share a word: switching one on or off keeps the
// others, in its own word and in the next.
static void
test_enable_int_and_disable_int_keep_other_sources_in_shared_word(void) {
  host_plic_set_enabled(11, TRUE);
  host_plic_set_enabled(33, TRUE);
  EnableInt(10, 1);
  TEST_ASSERT_TRUE(host_plic_is_enabled(10));
  TEST_ASSERT_TRUE(host_plic_is_enabled(11));
  TEST_ASSERT_TRUE(host_plic_is_enabled(33));
  DisableInt(10);
  TEST_ASSERT_FALSE(host_plic_is_enabled(10));
  TEST_ASSERT_TRUE(host_plic_is_enabled(11));
  TEST_ASSERT_TRUE(host_plic_is_enabled(33));
  TEST_ASSERT_TRUE(host_interrupts_enabled());
}

// A driver written to the service profile: it calls the controller functions
// only when the profile says they are there, and gives EnableInt() a level only
// when the profile says it takes one. The kernel exports EnableInt(intno,
// level) and sets the PLIC priority from the level, so the profile must say
// both, or such a driver loses the call or fails to compile.
#if !TK_SUPPORT_INTCTRL
#error "TK_SUPPORT_INTCTRL must say EnableInt() and DisableInt() exist"
#endif

static void enable_as_the_profile_says(UINT intno, INT level) {
#if TK_HAS_ENAINTLEVEL
  EnableInt(intno, level);
#else
  (void)level;
  EnableInt(intno);
#endif
}

static void test_enable_int_sets_source_at_its_level_for_profile_driver(void) {
  enum { LEVEL = 3 };
  enable_as_the_profile_says(SOME_SOURCE, LEVEL);
  TEST_ASSERT_TRUE(host_plic_is_enabled(SOME_SOURCE));
  TEST_ASSERT_EQUAL_UINT32(
      LEVEL, in_w(PLIC_BASE_ADDRESS + PLIC_PRIORITY_OFFSET + 4 * SOME_SOURCE));
}

// A number outside the table, and a level that is no priority, are ignored
// before any register is read or written (the fake fails on a wild address).
static void
test_enable_int_ignores_bad_number_or_level_without_register_access(void) {
  host_plic_set_enabled(CFN_MAX_INTNO, TRUE);
  host_log_count = 0;
  EnableInt(0, 1);
  EnableInt(CFN_MAX_INTNO, 1);
  EnableInt(0xffffffffu, 1);
  EnableInt(SOME_SOURCE, 0);
  EnableInt(SOME_SOURCE, PLIC_MAX_PRIORITY + 1);
  EnableInt(SOME_SOURCE, -1);
  DisableInt(0);
  DisableInt(CFN_MAX_INTNO);
  DisableInt(0xffffffffu);
  TEST_ASSERT_EQUAL_UINT(0, host_log_count);
  TEST_ASSERT_TRUE(host_plic_is_enabled(CFN_MAX_INTNO));
  TEST_ASSERT_TRUE(host_interrupts_enabled());
}

// A call that is refused changes nothing, and the handler defined before it is
// still the one that runs.
static void test_tk_def_int_refused_definition_keeps_old_handler(void) {
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(SOME_SOURCE, &host_counting_hlng));
  T_DINT no_handler = {.intatr = TA_HLNG, .inthdr = NULL};
  T_DINT unknown_attribute = {.intatr = 2, .inthdr = (FP)host_counting_handler};
  TEST_ASSERT_EQUAL(E_PAR, tk_def_int(0, NULL));
  TEST_ASSERT_EQUAL(E_PAR, tk_def_int(CFN_MAX_INTNO, NULL));
  TEST_ASSERT_EQUAL(E_PAR, tk_def_int(0xffffffffu, NULL));
  TEST_ASSERT_EQUAL(E_PAR, tk_def_int(SOME_SOURCE, &no_handler));
  TEST_ASSERT_EQUAL(E_RSATR, tk_def_int(SOME_SOURCE, &unknown_attribute));
  EnableInt(SOME_SOURCE, 1);
  host_plic_raise(SOME_SOURCE);
  tkmc_external_interrupt();
  TEST_ASSERT_EQUAL_UINT(1, host_handler_calls);
  TEST_ASSERT_EQUAL_UINT(SOME_SOURCE, host_handler_intno);
}

// The ISR state alone refuses the call, with interrupts on: on the target a
// handler runs with them off, and the state is what a later handler that turns
// them on would be judged by.
static void test_tk_def_int_refuses_call_in_isr_state(void) {
  // Depth 2 is a trap taken inside a trap: still the ISR state.
  for (UINT depth = 1; depth <= 2; ++depth) {
    tkmc_isr_depth = depth;
    TEST_ASSERT_EQUAL(E_CTX, tk_def_int(SOME_SOURCE, &host_counting_hlng));
  }
  tkmc_isr_depth = 0;
  EnableInt(SOME_SOURCE, 1);
  host_plic_raise(SOME_SOURCE);
  tkmc_external_interrupt();
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, host_handler_calls,
                                 "a refused call defined a handler");
}

static void disabling_handler(UINT intno) {
  host_handler_calls += 1;
  DisableInt(intno);
}

// A handler may switch its own source off, as a driver does until the deferred
// work is done, and the source is still completed: the PLIC ignores the
// completion of a source that is not enabled, which would leave it claimed.
static void test_disable_int_in_handler_still_completes_source(void) {
  TEST_ASSERT_EQUAL(
      E_OK,
      tk_def_int(SOME_SOURCE, &(T_DINT){.intatr = TA_HLNG,
                                        .inthdr = (FP)disabling_handler}));
  EnableInt(SOME_SOURCE, 1);
  host_plic_raise(SOME_SOURCE);
  tkmc_external_interrupt();
  TEST_ASSERT_EQUAL_UINT(1, host_handler_calls);
  TEST_ASSERT_FALSE_MESSAGE(host_plic_is_enabled(SOME_SOURCE),
                            "the handler's DisableInt() was undone");
  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());
  TEST_ASSERT_EQUAL_UINT(SOME_SOURCE, host_last_write(host_plic_claim_port()));
}

// A source that the default handler switches off is completed while it is
// still enabled, for the same reason.
static void
test_default_handler_completes_source_before_switching_it_off(void) {
  EnableInt(SOME_SOURCE, 1);
  host_plic_raise(SOME_SOURCE);
  tkmc_external_interrupt();
  TEST_ASSERT_FALSE(host_plic_is_enabled(SOME_SOURCE));
  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());
  TEST_ASSERT_EQUAL_UINT(SOME_SOURCE, host_last_write(host_plic_claim_port()));
}

// The calls that save and restore the interrupt flag put it back as they found
// it: with interrupts off, they stay off.
static void test_interrupt_calls_leave_interrupts_off_when_found_off(void) {
  UINT intsts;
  DI(intsts);
  EnableInt(SOME_SOURCE, 1);
  DisableInt(SOME_SOURCE);
  EnableInt(0, 1);
  TEST_ASSERT_EQUAL(E_CTX, tk_def_int(SOME_SOURCE, &host_counting_hlng));
  TEST_ASSERT_FALSE(host_interrupts_enabled());
  EI(intsts);
  TEST_ASSERT_TRUE(host_interrupts_enabled());
}

// The threshold is what keeps a source of the same or a lower priority out of a
// handler that has interrupts on: the PLIC raises only a source whose priority
// is above it. It is the priority of the source being served while the handler
// runs, and what it was before afterwards.
static UW s_threshold_seen;
static UW s_threshold_seen_after_nested;
static UW s_inner_threshold_seen;

static void threshold_probe(UINT intno) {
  (void)intno;
  host_handler_calls += 1;
  s_threshold_seen = host_plic_threshold();
}

static void defines(UINT intno, void (*handler)(UINT)) {
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(intno, &(T_DINT){.intatr = TA_HLNG,
                                                      .inthdr = (FP)handler}));
}

static void test_handler_runs_with_threshold_at_priority_of_its_source(void) {
  defines(OUTER_SOURCE, threshold_probe);
  EnableInt(OUTER_SOURCE, 3);
  host_plic_raise(OUTER_SOURCE);
  tkmc_external_interrupt();
  TEST_ASSERT_EQUAL_UINT(1, host_handler_calls);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(3, s_threshold_seen,
                                 "the threshold in the handler");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, host_plic_threshold(),
                                 "the threshold after the handler");
}

static void nesting_handler(UINT intno) {
  (void)intno;
  s_threshold_seen = host_plic_threshold();
  host_plic_raise(INNER_SOURCE);
  tkmc_external_interrupt(); // The trap of a higher priority source.
  s_threshold_seen_after_nested = host_plic_threshold();
}

static void inner_handler(UINT intno) {
  (void)intno;
  s_inner_threshold_seen = host_plic_threshold();
}

static void test_nested_trap_raises_threshold_and_restores_outer_one(void) {
  defines(OUTER_SOURCE, nesting_handler);
  defines(INNER_SOURCE, inner_handler);
  EnableInt(OUTER_SOURCE, 2);
  EnableInt(INNER_SOURCE, 5);
  host_plic_raise(OUTER_SOURCE);
  tkmc_external_interrupt();
  TEST_ASSERT_EQUAL_UINT(2, s_threshold_seen);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(5, s_inner_threshold_seen,
                                 "the threshold in the nested handler");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      2, s_threshold_seen_after_nested,
      "the nested trap did not give back the outer threshold");
  TEST_ASSERT_EQUAL_UINT(0, host_plic_threshold());
}

// A source with no handler is switched off and completed, which is short, so
// the threshold is left alone.
static void
test_default_handler_leaves_threshold_alone_for_source_without_handler(void) {
  EnableInt(QUIET_SOURCE, 6);
  host_plic_raise(QUIET_SOURCE);
  tkmc_external_interrupt();
  TEST_ASSERT_FALSE(host_plic_is_enabled(QUIET_SOURCE));
  TEST_ASSERT_EQUAL_UINT(0, host_plic_threshold_highest());
}

static void enabling_handler(UINT intno) {
  (void)intno;
  EnableInt(QUIET_SOURCE, 1);
  s_threshold_seen = host_plic_threshold();
}

// EnableInt() writes the threshold so that the PLIC looks for a source again
// (see kernel/intctl.c). It writes the value the threshold has: a 0 would let
// every source into the handler that called it.
static void test_enable_int_keeps_threshold_it_finds(void) {
  defines(OUTER_SOURCE, enabling_handler);
  EnableInt(OUTER_SOURCE, 4);
  host_plic_raise(OUTER_SOURCE);
  tkmc_external_interrupt();
  TEST_ASSERT_EQUAL_UINT_MESSAGE(4, s_threshold_seen,
                                 "EnableInt() changed the threshold");

  host_plic_set_threshold(3);
  EnableInt(QUIET_SOURCE, 2);
  TEST_ASSERT_EQUAL_UINT(3, host_plic_threshold());
}

void run_intctl_tests(void) {
  UnitySetTestFile(__FILE__);
  RUN_TEST(
      test_external_interrupt_with_no_source_calls_no_handler_and_completes_nothing);
  RUN_TEST(
      test_default_handler_switches_off_source_beyond_table_and_completes_it);
  RUN_TEST(
      test_tk_def_int_serves_first_and_last_interrupt_number_with_own_number);
  RUN_TEST(test_enable_int_and_disable_int_keep_other_sources_in_shared_word);
  RUN_TEST(test_enable_int_sets_source_at_its_level_for_profile_driver);
  RUN_TEST(test_enable_int_ignores_bad_number_or_level_without_register_access);
  RUN_TEST(test_tk_def_int_refused_definition_keeps_old_handler);
  RUN_TEST(test_tk_def_int_refuses_call_in_isr_state);
  RUN_TEST(test_disable_int_in_handler_still_completes_source);
  RUN_TEST(test_default_handler_completes_source_before_switching_it_off);
  RUN_TEST(test_interrupt_calls_leave_interrupts_off_when_found_off);
  RUN_TEST(test_handler_runs_with_threshold_at_priority_of_its_source);
  RUN_TEST(test_nested_trap_raises_threshold_and_restores_outer_one);
  RUN_TEST(
      test_default_handler_leaves_threshold_alone_for_source_without_handler);
  RUN_TEST(test_enable_int_keeps_threshold_it_finds);
}
