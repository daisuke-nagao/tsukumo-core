/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// The external interrupt entry, on the claims the QEMU machine cannot be made
// to give: a claim that finds nothing, and a source that is not in the table of
// handlers. The rest of the interrupt API is tested on QEMU
// (test/suites/interrupt.c), where a real device raises the source.

#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "fakes.h"
#include "intctl.h"

enum { SOME_SOURCE = 10 };

static UINT s_calls;

static void counting_handler(UINT intno) {
  (void)intno;
  s_calls += 1;
}

void test_a_claim_of_no_source_calls_no_handler_and_completes_nothing(void) {
  s_calls = 0;
  TEST_ASSERT_EQUAL(
      E_OK, tk_def_int(SOME_SOURCE, &(T_DINT){.intatr = TA_HLNG,
                                              .inthdr = (FP)counting_handler}));
  host_plic_set_enabled(SOME_SOURCE, TRUE);
  host_plic_raise(0);
  tkmc_external_interrupt();
  TEST_ASSERT_EQUAL_UINT(0, s_calls);
  TEST_ASSERT_EQUAL_UINT(0, host_write_count(host_plic_claim_port()));
  TEST_ASSERT_TRUE(host_plic_is_enabled(SOME_SOURCE));
}

// A source above the table has no handler, so the default handler runs for it:
// it is switched off, and no other source is. Its word of enable bits is the
// one of its own number, not the first.
void test_a_source_beyond_the_table_is_switched_off_and_completed(void) {
  s_calls = 0;
  TEST_ASSERT_EQUAL(
      E_OK, tk_def_int(SOME_SOURCE, &(T_DINT){.intatr = TA_HLNG,
                                              .inthdr = (FP)counting_handler}));
  for (UINT src = CFN_MAX_INTNO; src < PLIC_SOURCE_COUNT; ++src) {
    for (UINT other = 1; other < PLIC_SOURCE_COUNT; ++other) {
      host_plic_set_enabled(other, TRUE);
    }
    host_log_count = 0; // One claim leaves a few entries; the log is small.
    host_plic_raise(src);
    tkmc_external_interrupt();
    for (UINT other = 1; other < PLIC_SOURCE_COUNT; ++other) {
      TEST_ASSERT_EQUAL_MESSAGE(other != src, host_plic_is_enabled(other),
                                "only the claimed source is switched off");
    }
    TEST_ASSERT_EQUAL_UINT(src, host_last_write(host_plic_claim_port()));
  }
  TEST_ASSERT_EQUAL_UINT(0, s_calls);
}

static UINT s_last_intno;

static void recording_handler(UINT intno) {
  s_last_intno = intno;
  s_calls += 1;
}

static const T_DINT s_recording = {.intatr = TA_HLNG,
                                   .inthdr = (FP)recording_handler};

// The first and the last number of the table are served like any other, and
// with their own number.
void test_the_first_and_the_last_interrupt_number_have_a_handler(void) {
  static const UINT edges[] = {1, CFN_MAX_INTNO - 1};
  for (UINT i = 0; i < 2; ++i) {
    s_calls = 0;
    s_last_intno = 0;
    TEST_ASSERT_EQUAL(E_OK, tk_def_int(edges[i], &s_recording));
    host_plic_raise(edges[i]);
    tkmc_external_interrupt();
    TEST_ASSERT_EQUAL_UINT(1, s_calls);
    TEST_ASSERT_EQUAL_UINT(edges[i], s_last_intno);
  }
}

// The enable bits of 32 sources share a word: switching one on or off keeps the
// others, in its own word and in the next.
void test_enabling_and_disabling_a_source_keeps_the_others(void) {
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

// A number outside the table, and a level that is no priority, are ignored
// before any register is read or written (the fake fails on a wild address).
void test_a_bad_number_or_level_touches_no_register(void) {
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
void test_a_refused_definition_leaves_the_old_one_in_place(void) {
  s_calls = 0;
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(SOME_SOURCE, &s_recording));
  T_DINT no_handler = {.intatr = TA_HLNG, .inthdr = NULL};
  T_DINT assembler = {.intatr = TA_ASM, .inthdr = (FP)counting_handler};
  TEST_ASSERT_EQUAL(E_PAR, tk_def_int(0, NULL));
  TEST_ASSERT_EQUAL(E_PAR, tk_def_int(CFN_MAX_INTNO, NULL));
  TEST_ASSERT_EQUAL(E_PAR, tk_def_int(0xffffffffu, NULL));
  TEST_ASSERT_EQUAL(E_PAR, tk_def_int(SOME_SOURCE, &no_handler));
  TEST_ASSERT_EQUAL(E_RSATR, tk_def_int(SOME_SOURCE, &assembler));
  host_plic_raise(SOME_SOURCE);
  tkmc_external_interrupt();
  TEST_ASSERT_EQUAL_UINT(1, s_calls);
  TEST_ASSERT_EQUAL_UINT(SOME_SOURCE, s_last_intno);
}

// The ISR state alone refuses the call, with interrupts on: on the target a
// handler runs with them off, and the state is what a later handler that turns
// them on would be judged by.
extern volatile UINT tkmc_isr_depth;

void test_a_handler_cannot_be_defined_in_the_isr_state(void) {
  // Depth 2 is a trap taken inside a trap: still the ISR state.
  for (UINT depth = 1; depth <= 2; ++depth) {
    tkmc_isr_depth = depth;
    TEST_ASSERT_EQUAL(E_CTX, tk_def_int(SOME_SOURCE, &s_recording));
  }
  tkmc_isr_depth = 0;
  host_plic_raise(SOME_SOURCE);
  s_calls = 0;
  tkmc_external_interrupt();
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, s_calls,
                                 "a refused call defined a handler");
}

static void disabling_handler(UINT intno) {
  s_calls += 1;
  DisableInt(intno);
}

// A handler may switch its own source off, as a driver does until the deferred
// work is done, and the source is still completed: the PLIC ignores the
// completion of a source that is not enabled, which would leave it claimed.
void test_a_source_a_handler_switched_off_is_still_completed(void) {
  s_calls = 0;
  TEST_ASSERT_EQUAL(
      E_OK,
      tk_def_int(SOME_SOURCE, &(T_DINT){.intatr = TA_HLNG,
                                        .inthdr = (FP)disabling_handler}));
  EnableInt(SOME_SOURCE, 1);
  host_plic_raise(SOME_SOURCE);
  tkmc_external_interrupt();
  TEST_ASSERT_EQUAL_UINT(1, s_calls);
  TEST_ASSERT_FALSE_MESSAGE(host_plic_is_enabled(SOME_SOURCE),
                            "the handler's DisableInt() was undone");
  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());
  TEST_ASSERT_EQUAL_UINT(SOME_SOURCE, host_last_write(host_plic_claim_port()));
}

// A source that the default handler switches off is completed while it is
// still enabled, for the same reason.
void test_the_default_handler_completes_before_it_switches_off(void) {
  host_plic_set_enabled(SOME_SOURCE, TRUE);
  host_plic_raise(SOME_SOURCE);
  tkmc_external_interrupt();
  TEST_ASSERT_FALSE(host_plic_is_enabled(SOME_SOURCE));
  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());
}

// The calls that save and restore the interrupt flag put it back as they found
// it: with interrupts off, they stay off.
void test_the_interrupt_calls_leave_interrupts_off_when_they_found_them_off(
    void) {
  UINT intsts;
  DI(intsts);
  EnableInt(SOME_SOURCE, 1);
  DisableInt(SOME_SOURCE);
  EnableInt(0, 1);
  TEST_ASSERT_EQUAL(E_CTX, tk_def_int(SOME_SOURCE, &s_recording));
  TEST_ASSERT_FALSE(host_interrupts_enabled());
  EI(intsts);
  TEST_ASSERT_TRUE(host_interrupts_enabled());
}

// The threshold is what keeps a source of the same or a lower priority out of a
// handler that has interrupts on: the PLIC raises only a source whose priority
// is above it. It is the priority of the source being served while the handler
// runs, and what it was before afterwards.
enum { OUTER_SOURCE = 10, INNER_SOURCE = 11, QUIET_SOURCE = 12 };

static UW s_threshold_seen;
static UW s_threshold_seen_after_nested;
static UW s_inner_threshold_seen;

static void threshold_probe(UINT intno) {
  (void)intno;
  s_calls += 1;
  s_threshold_seen = host_plic_threshold();
}

static void defines(UINT intno, void (*handler)(UINT)) {
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(intno, &(T_DINT){.intatr = TA_HLNG,
                                                      .inthdr = (FP)handler}));
}

void test_a_handler_runs_with_the_threshold_at_the_priority_of_its_source(
    void) {
  s_calls = 0;
  defines(OUTER_SOURCE, threshold_probe);
  EnableInt(OUTER_SOURCE, 3);
  host_plic_raise(OUTER_SOURCE);
  tkmc_external_interrupt();
  TEST_ASSERT_EQUAL_UINT(1, s_calls);
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

void test_a_nested_trap_raises_the_threshold_and_gives_back_the_outer_one(
    void) {
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
void test_a_source_without_a_handler_does_not_raise_the_threshold(void) {
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
void test_enable_int_keeps_the_threshold_it_finds(void) {
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
