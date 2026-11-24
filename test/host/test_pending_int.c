/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// CheckInt() and ClearInt(): reading whether a source is pending without
// claiming it, and clearing a pending request without disturbing the other
// sources, the PLIC configuration or a claim a handler holds.

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

// The value of the last read of the claim register in the access log.
static UD last_claim_read(void) {
  for (UINT i = host_log_count; i > 0; --i) {
    if (host_log[i - 1].port == host_plic_claim_port() &&
        !host_log[i - 1].is_write) {
      return host_log[i - 1].value;
    }
  }
  TEST_FAIL_MESSAGE("no read of the claim register in the log");
  return 0;
}

// Fails the test, naming the rule and the case, unless the access log shows
// ClearInt() of target bracketing its claim with two threshold writes, MIE off:
// 0 before the claim, with only target enabled, and restored_threshold after
// the completion and the restored enable bits (and priority, if it was 0).
#define SEQUENCE_RULE(cond, rule)                                              \
  do {                                                                         \
    if (!(cond)) {                                                             \
      char message_[160];                                                      \
      snprintf(message_, sizeof(message_), "%s: %s", label, rule);             \
      TEST_FAIL_MESSAGE(message_);                                             \
    }                                                                          \
  } while (0)

static void check_threshold_sequence(UINT target, UW priority,
                                     UW restored_threshold, const char *label) {
  enum { ENABLE_WORDS = (PLIC_SOURCE_COUNT + 31) / 32 };
  const INT enable_first = PLIC_BASE_ADDRESS + PLIC_ENABLE_OFFSET;
  const INT enable_last = enable_first + 4 * ((PLIC_SOURCE_COUNT - 1) / 32);
  const INT threshold_port = PLIC_BASE_ADDRESS + PLIC_THRESHOLD_OFFSET;
  INT threshold_writes[2] = {-1, -1};
  INT claim_read = -1;
  INT claim_write = -1;
  INT last_enable_write = -1;
  INT last_priority_write = -1;
  BOOL threshold_writes_mie_off = TRUE;
  UW enable_words[ENABLE_WORDS] = {0};
  BOOL enable_words_written[ENABLE_WORDS] = {FALSE};
  UINT threshold_write_count = 0;
  for (UINT i = 0; i < host_log_count; ++i) {
    const host_access_t access = host_log[i];
    if (access.is_write && access.port >= enable_first &&
        access.port <= enable_last) {
      last_enable_write = (INT)i;
      UINT word = (access.port - enable_first) / 4;
      enable_words[word] = (UW)access.value;
      enable_words_written[word] = TRUE;
    }
    if (access.is_write && access.port == threshold_port) {
      if (threshold_write_count < 2) {
        threshold_writes[threshold_write_count] = (INT)i;
      }
      threshold_write_count += 1;
      threshold_writes_mie_off &= !access.interrupts_enabled;
    }
    if (access.port == host_plic_claim_port()) {
      if (access.is_write) {
        claim_write = (INT)i;
      } else {
        claim_read = (INT)i;
        if (threshold_writes[0] >= 0) {
          BOOL target_only = TRUE;
          for (UINT word = 0; word < ENABLE_WORDS; ++word) {
            UW expected = word == target / 32 ? 1u << (target % 32) : 0;
            target_only &=
                enable_words_written[word] && enable_words[word] == expected;
          }
          SEQUENCE_RULE(target_only,
                        "the claim saw a source other than the target enabled");
        }
      }
    }
    if (access.is_write && access.port == host_plic_priority_port(target)) {
      last_priority_write = (INT)i;
    }
  }
  SEQUENCE_RULE(threshold_write_count == 2, "not two threshold writes");
  SEQUENCE_RULE(claim_read >= 0, "no claim read");
  SEQUENCE_RULE(claim_write >= 0, "no completion");
  SEQUENCE_RULE(threshold_writes[0] < claim_read,
                "the threshold was not lowered before the claim");
  SEQUENCE_RULE(claim_read < claim_write,
                "the completion came before the claim");
  SEQUENCE_RULE(claim_write < threshold_writes[1],
                "the threshold was restored before the completion");
  SEQUENCE_RULE(last_enable_write >= 0, "no enable word was written");
  SEQUENCE_RULE(last_enable_write < threshold_writes[1],
                "an enable word was written after the threshold was restored");
  SEQUENCE_RULE(threshold_writes_mie_off,
                "a threshold write came with interrupts on");
  SEQUENCE_RULE(!host_log[claim_read].interrupts_enabled,
                "the claim came with interrupts on");
  SEQUENCE_RULE(host_log[threshold_writes[0]].value == 0,
                "the first threshold write was not 0");
  SEQUENCE_RULE(host_log[threshold_writes[1]].value == restored_threshold,
                "the threshold was not restored");
  if (priority == 0) {
    SEQUENCE_RULE(last_priority_write >= claim_write &&
                      last_priority_write < threshold_writes[1] &&
                      host_log[last_priority_write].value == priority,
                  "priority 0 was not restored between the completion and the "
                  "threshold");
  }
  for (INT i = threshold_writes[0] + 1; i < claim_read; ++i) {
    SEQUENCE_RULE(!(host_log[i].is_write && host_log[i].port >= enable_first &&
                    host_log[i].port <= enable_last),
                  "an enable word was written between the threshold and the "
                  "claim");
  }
}
#undef SEQUENCE_RULE

static BOOL plic_pending_was_written(void) {
  const INT first = PLIC_BASE_ADDRESS + PLIC_PENDING_OFFSET;
  const INT last = first + 4 * ((PLIC_SOURCE_COUNT - 1) / 32);
  for (UINT i = 0; i < host_log_count; ++i) {
    if (host_log[i].is_write && host_log[i].port >= first &&
        host_log[i].port <= last) {
      return TRUE;
    }
  }
  return FALSE;
}

static void
test_clear_int_refuses_invalid_numbers_without_mmio_and_keeps_mie(void) {
  for (UINT mie = 0; mie < 2; ++mie) {
    UINT saved = 0;
    if (mie != 0) {
      EI(0);
    } else {
      DI(saved);
    }
    host_log_count = 0;
    ClearInt(0);
    ClearInt(CFN_MAX_INTNO);
    ClearInt(~(UINT)0);
    TEST_ASSERT_EQUAL_UINT(0, host_log_count);
    TEST_ASSERT_EQUAL(mie != 0, host_interrupts_enabled());
    EI(saved);
  }
}

static void
test_clear_int_without_pending_never_completes_and_restores_configuration(
    void) {
  static const UW priorities[] = {0, 1, 7};
  static const UW thresholds[] = {0, 7};
  for (UINT registered = 0; registered < 2; ++registered) {
    for (UINT enabled = 0; enabled < 2; ++enabled) {
      for (UINT p = 0; p < HOST_COUNT(priorities); ++p) {
        for (UINT t = 0; t < HOST_COUNT(thresholds); ++t) {
          for (UINT mie = 0; mie < 2; ++mie) {
            host_reset();
            if (registered != 0) {
              TEST_ASSERT_EQUAL(E_OK,
                                tk_def_int(SOME_SOURCE, &host_counting_hlng));
            }
            host_plic_set_enabled(SOME_SOURCE, enabled != 0);
            host_plic_set_priority(SOME_SOURCE, priorities[p]);
            host_plic_set_threshold(thresholds[t]);
            char label[80];
            snprintf(label, sizeof(label),
                     "registered %u, enabled %u, priority %u, threshold %u, "
                     "MIE %u",
                     registered, enabled, (unsigned)priorities[p],
                     (unsigned)thresholds[t], mie);
            UINT saved = 0;
            if (mie != 0) {
              EI(0);
            } else {
              DI(saved);
            }

            // Twice: a second ClearInt() finds nothing either.
            for (UINT call = 0; call < 2; ++call) {
              host_log_count = 0;
              ClearInt(SOME_SOURCE);
              TEST_ASSERT_EQUAL_UINT_MESSAGE(
                  1, host_access_count(host_plic_claim_port(), FALSE), label);
              TEST_ASSERT_EQUAL_UINT_MESSAGE(
                  0, host_access_count(host_plic_claim_port(), TRUE), label);
              TEST_ASSERT_EQUAL_UINT_MESSAGE(0, last_claim_read(), label);
              TEST_ASSERT_FALSE_MESSAGE(plic_pending_was_written(), label);
              TEST_ASSERT_EQUAL_MESSAGE(mie != 0, host_interrupts_enabled(),
                                        label);
            }
            TEST_ASSERT_EQUAL_MESSAGE(enabled != 0,
                                      host_plic_is_enabled(SOME_SOURCE), label);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(
                priorities[p], in_w(host_plic_priority_port(SOME_SOURCE)),
                label);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(thresholds[t], host_plic_threshold(),
                                           label);
            TEST_ASSERT_FALSE_MESSAGE(CheckInt(SOME_SOURCE), label);
            TEST_ASSERT_EQUAL_MESSAGE(mie != 0, host_interrupts_enabled(),
                                      label);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, host_plic_ignored_completions(),
                                           label);
            EI(saved);
          }
        }
      }
    }
  }
}

static void
test_clear_int_claims_only_its_pending_source_and_preserves_all_words(void) {
  static const UW priorities[] = {0, 1, 7};
  static const UW thresholds[] = {0, 7};
  for (UINT registered = 0; registered < 2; ++registered) {
    for (UINT enabled = 0; enabled < 2; ++enabled) {
      for (UINT p = 0; p < HOST_COUNT(priorities); ++p) {
        for (UINT t = 0; t < HOST_COUNT(thresholds); ++t) {
          for (UINT mie = 0; mie < 2; ++mie) {
            host_reset();
            if (registered != 0) {
              TEST_ASSERT_EQUAL(E_OK,
                                tk_def_int(SOME_SOURCE, &host_counting_hlng));
            }
            const UINT smaller = 1, other_word = 40, last_word = 70;
            UW priority_before[PLIC_SOURCE_COUNT] = {0};
            BOOL enabled_before[PLIC_SOURCE_COUNT] = {0};
            host_plic_set_enabled(SOME_SOURCE, enabled != 0);
            host_plic_set_priority(SOME_SOURCE, priorities[p]);
            host_plic_set_enabled(smaller, TRUE);
            host_plic_set_priority(smaller, 7);
            host_plic_set_enabled(other_word, TRUE);
            host_plic_set_priority(other_word, 1);
            host_plic_set_enabled(last_word, TRUE);
            host_plic_set_priority(last_word, 7);
            host_plic_set_threshold(thresholds[t]);
            host_plic_raise(SOME_SOURCE);
            host_plic_raise(smaller);
            host_plic_raise(other_word);
            host_plic_raise(last_word);
            for (UINT src = 1; src < PLIC_SOURCE_COUNT; ++src) {
              enabled_before[src] = host_plic_is_enabled(src);
              priority_before[src] = in_w(host_plic_priority_port(src));
            }

            char label[80];
            snprintf(label, sizeof(label),
                     "registered %u, enabled %u, priority %u, threshold %u, "
                     "MIE %u",
                     registered, enabled, (unsigned)priorities[p],
                     (unsigned)thresholds[t], mie);
            UINT saved = 0;
            if (mie != 0) {
              EI(0);
            } else {
              DI(saved);
            }
            host_log_count = 0;
            host_snapshot_t before;
            host_take_snapshot(&before);
            ClearInt(SOME_SOURCE);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(
                1, host_access_count(host_plic_claim_port(), FALSE), label);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(
                1, host_access_count(host_plic_claim_port(), TRUE), label);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(
                SOME_SOURCE, host_last_write(host_plic_claim_port()), label);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, host_plic_ignored_completions(),
                                           label);
            TEST_ASSERT_FALSE_MESSAGE(plic_pending_was_written(), label);
            const INT threshold_port =
                PLIC_BASE_ADDRESS + PLIC_THRESHOLD_OFFSET;
            TEST_ASSERT_EQUAL_UINT_MESSAGE(
                2, host_access_count(threshold_port, TRUE), label);
            check_threshold_sequence(SOME_SOURCE, priorities[p], thresholds[t],
                                     label);
            TEST_ASSERT_EQUAL_MESSAGE(mie != 0, host_interrupts_enabled(),
                                      label);
            TEST_ASSERT_FALSE_MESSAGE(host_plic_pending(SOME_SOURCE), label);
            TEST_ASSERT_TRUE_MESSAGE(host_plic_pending(smaller), label);
            TEST_ASSERT_TRUE_MESSAGE(host_plic_pending(other_word), label);
            TEST_ASSERT_TRUE_MESSAGE(host_plic_pending(last_word), label);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(thresholds[t], host_plic_threshold(),
                                           label);
            for (UINT src = 1; src < PLIC_SOURCE_COUNT; ++src) {
              TEST_ASSERT_EQUAL_MESSAGE(enabled_before[src],
                                        host_plic_is_enabled(src), label);
              TEST_ASSERT_EQUAL_UINT_MESSAGE(priority_before[src],
                                             in_w(host_plic_priority_port(src)),
                                             label);
            }
            const UINT claim_order[] = {smaller, last_word, other_word};
            for (UINT i = 0; i < HOST_COUNT(claim_order); ++i) {
              UINT claimed = in_w(host_plic_claim_port());
              TEST_ASSERT_EQUAL_UINT_MESSAGE(claim_order[i], claimed, label);
              out_w(host_plic_claim_port(), claimed);
            }
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, in_w(host_plic_claim_port()),
                                           label);
            TEST_ASSERT_FALSE_MESSAGE(host_plic_pending(smaller), label);
            TEST_ASSERT_FALSE_MESSAGE(host_plic_pending(other_word), label);
            TEST_ASSERT_FALSE_MESSAGE(host_plic_pending(last_word), label);
            TEST_ASSERT_EQUAL_MESSAGE(mie != 0, host_interrupts_enabled(),
                                      label);
            host_assert_unchanged(&before,
                                  "ClearInt changed queues or dispatch");
            EI(saved);
          }
        }
      }
    }
  }
}

static void
test_clear_int_in_nested_handler_preserves_outer_claim_and_record(void) {
  _Alignas(16) UW outer_frame[32] = {0};
  _Alignas(16) UW inner_frame[32] = {0};
  tkmc_interrupt_context outer, inner;
  const T_DINT assembler = {.intatr = TA_ASM,
                            .inthdr = (FP)host_counting_handler};
  ID task = host_make_task(1);
  host_set_current(task);
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(OUTER_SOURCE, &assembler));
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(INNER_SOURCE, &assembler));
  EnableInt(OUTER_SOURCE, 2);
  EnableInt(INNER_SOURCE, 5);
  host_plic_set_threshold(1);
  UINT saved;
  DI(saved);

  tkmc_isr_depth = 1;
  host_plic_raise(OUTER_SOURCE);
  tkmc_prepare_external_interrupt(&outer, outer_frame);
  tkmc_isr_depth = 2;
  host_plic_raise(INNER_SOURCE);
  tkmc_prepare_external_interrupt(&inner, inner_frame);
  tkmc_interrupt_context outer_before = outer;
  tkmc_interrupt_context inner_before = inner;
  host_snapshot_t queues_before;
  host_log_count = 0;
  host_take_snapshot(&queues_before);

  // ClearInt() of the source the outer handler is serving finds its claim
  // outstanding and leaves both records alone.
  ClearInt(OUTER_SOURCE);
  TEST_ASSERT_EQUAL_UINT(1, host_access_count(host_plic_claim_port(), FALSE));
  TEST_ASSERT_EQUAL_UINT(0, host_access_count(host_plic_claim_port(), TRUE));
  TEST_ASSERT_EQUAL_MEMORY(&outer_before, &outer, sizeof(outer));
  TEST_ASSERT_EQUAL_MEMORY(&inner_before, &inner, sizeof(inner));
  TEST_ASSERT_EQUAL_PTR(&inner, tkmc_active_interrupt_context);
  TEST_ASSERT_EQUAL_UINT(2, tkmc_isr_depth);
  TEST_ASSERT_EQUAL_UINT(5, host_plic_threshold());
  host_plic_raise(OUTER_SOURCE);
  TEST_ASSERT_FALSE_MESSAGE(CheckInt(OUTER_SOURCE),
                            "the gateway let a claimed source be raised");
  EndOfInt(OUTER_SOURCE);
  TEST_ASSERT_EQUAL_UINT(1, host_access_count(host_plic_claim_port(), TRUE));
  TEST_ASSERT_TRUE((outer.kind & TKMC_INTERRUPT_COMPLETED) != 0);
  TEST_ASSERT_TRUE((inner.kind & TKMC_INTERRUPT_COMPLETED) == 0);

  // After EndOfInt() a new request of the source is pending, and ClearInt()
  // claims and completes it, still leaving the nested records alone.
  host_plic_raise(OUTER_SOURCE);
  TEST_ASSERT_TRUE(CheckInt(OUTER_SOURCE));
  // The log is cleared below; the first ClearInt() and EndOfInt() must have
  // asked for no task switch.
  TEST_ASSERT_EQUAL_UINT(0, host_write_count(HOST_MSIP));
  outer_before = outer;
  inner_before = inner;
  host_log_count = 0;
  ClearInt(OUTER_SOURCE);
  TEST_ASSERT_EQUAL_UINT(1, host_access_count(host_plic_claim_port(), FALSE));
  TEST_ASSERT_EQUAL_UINT(1, host_access_count(host_plic_claim_port(), TRUE));
  TEST_ASSERT_EQUAL_UINT(OUTER_SOURCE, host_last_write(host_plic_claim_port()));
  TEST_ASSERT_FALSE(CheckInt(OUTER_SOURCE));
  TEST_ASSERT_EQUAL_MEMORY(&outer_before, &outer, sizeof(outer));
  TEST_ASSERT_EQUAL_MEMORY(&inner_before, &inner, sizeof(inner));
  TEST_ASSERT_EQUAL_PTR(&inner, tkmc_active_interrupt_context);
  TEST_ASSERT_EQUAL_PTR(&outer, inner.previous);
  TEST_ASSERT_EQUAL_UINT(2, tkmc_isr_depth);
  TEST_ASSERT_EQUAL_UINT(5, host_plic_threshold());
  TEST_ASSERT_FALSE(host_interrupts_enabled());
  TEST_ASSERT_TRUE(host_plic_is_enabled(OUTER_SOURCE));
  TEST_ASSERT_TRUE(host_plic_is_enabled(INNER_SOURCE));
  TEST_ASSERT_EQUAL_UINT(2, in_w(host_plic_priority_port(OUTER_SOURCE)));
  TEST_ASSERT_EQUAL_UINT(5, in_w(host_plic_priority_port(INNER_SOURCE)));
  TEST_ASSERT_FALSE(plic_pending_was_written());
  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());

  // The handlers exit: only the inner claim is left to complete, so three
  // completions in all.
  tkmc_finish_external_interrupt(&inner);
  tkmc_isr_depth = 1;
  tkmc_finish_external_interrupt(&outer);
  TEST_ASSERT_EQUAL_UINT(2, host_access_count(host_plic_claim_port(), TRUE));
  TEST_ASSERT_EQUAL_UINT(1, host_plic_threshold());
  TEST_ASSERT_NULL(tkmc_active_interrupt_context);
  tkmc_isr_depth = 0;
  EI(saved);
  host_assert_unchanged(&queues_before,
                        "nested ClearInt changed queues or dispatch");
  TEST_ASSERT_TRUE(host_interrupts_enabled());
}

// The access log holds exactly one 32 bit read, of port.
static void check_one_read_of(INT port, const char *label) {
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, host_log_count, label);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(port, host_log[0].port, label);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(4, host_log[0].width, label);
  TEST_ASSERT_FALSE_MESSAGE(host_log[0].is_write, label);
}

static void test_check_int_reads_one_pending_word_without_claiming(void) {
  enum { NO_CURRENT, TASK_CONTEXT, ISR_CONTEXT, CONTEXTS };
  static const char *const context_name[CONTEXTS] = {"no current task", "task",
                                                     "ISR"};
  const INT pending_port =
      PLIC_BASE_ADDRESS + PLIC_PENDING_OFFSET + 4 * (31 / 32);
  ID task = host_make_task(1);

  host_plic_set_enabled(31, FALSE);
  host_plic_set_priority(31, 0);
  host_plic_set_threshold(7);
  host_plic_raise(1);
  host_log_count = 0;
  TEST_ASSERT_FALSE(CheckInt(0));
  TEST_ASSERT_FALSE(CheckInt(CFN_MAX_INTNO));
  TEST_ASSERT_FALSE(CheckInt(33));
  TEST_ASSERT_FALSE(CheckInt(0xffffffffu));
  TEST_ASSERT_EQUAL_UINT(0, host_log_count);

  // Source 1 is the first valid number and can be pending without source 31.
  host_log_count = 0;
  TEST_ASSERT_TRUE(CheckInt(1));
  check_one_read_of(PLIC_BASE_ADDRESS + PLIC_PENDING_OFFSET, "source 1");
  host_log_count = 0;
  TEST_ASSERT_FALSE(CheckInt(31));
  check_one_read_of(pending_port, "source 31 not raised");

  // A disabled source of priority 0 is pending all the same, in every context.
  host_plic_raise(31);
  for (UINT context = 0; context < CONTEXTS; ++context) {
    for (UINT mie = 0; mie < 2; ++mie) {
      char label[40];
      snprintf(label, sizeof(label), "%s, MIE %u", context_name[context], mie);
      if (context == NO_CURRENT) {
        current = next = NULL;
      } else {
        host_set_current(task);
      }
      const host_context_t entered =
          host_enter(context == ISR_CONTEXT, mie == 0);

      host_log_count = 0;
      host_snapshot_t before;
      host_take_snapshot(&before);
      TEST_ASSERT_TRUE_MESSAGE(CheckInt(31), label);
      check_one_read_of(pending_port, label);
      TEST_ASSERT_EQUAL_MESSAGE(mie != 0, host_interrupts_enabled(), label);
      TEST_ASSERT_EQUAL_UINT_MESSAGE(context == ISR_CONTEXT ? 1 : 0,
                                     tkmc_isr_depth, label);
      host_leave(entered);
      host_assert_unchanged(&before, label);
    }
  }

  current = next = NULL;
  host_plic_set_enabled(31, TRUE);
  host_plic_set_priority(31, 1);
  TEST_ASSERT_TRUE(host_plic_is_enabled(31));
  host_log_count = 0;
  TEST_ASSERT_TRUE(CheckInt(31));
  check_one_read_of(pending_port, "source 31 enabled");
  TEST_ASSERT_EQUAL_UINT(7, host_plic_threshold());
  TEST_ASSERT_EQUAL_UINT(0, host_write_count(host_plic_claim_port()));

  // Repeated pending reads leave the notification for the later claim.
  host_reset();
  host_plic_raise(31);
  TEST_ASSERT_TRUE(CheckInt(31));
  TEST_ASSERT_TRUE(CheckInt(31));
  host_handler_calls = 0;
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(31, &host_counting_hlng));
  EnableInt(31, 1);
  tkmc_external_interrupt();
  TEST_ASSERT_EQUAL_UINT(1, host_handler_calls);
  TEST_ASSERT_FALSE(CheckInt(31));
}

void run_pending_int_tests(void) {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_clear_int_refuses_invalid_numbers_without_mmio_and_keeps_mie);
  RUN_TEST(
      test_clear_int_without_pending_never_completes_and_restores_configuration);
  RUN_TEST(
      test_clear_int_claims_only_its_pending_source_and_preserves_all_words);
  RUN_TEST(test_clear_int_in_nested_handler_preserves_outer_claim_and_record);
  RUN_TEST(test_check_int_reads_one_pending_word_without_claiming);
}
