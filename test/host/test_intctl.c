/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// The external interrupt entry, on the claims the QEMU machine cannot be made
// to give: a claim that finds nothing, and a source that is not in the table of
// handlers. The rest of the interrupt API is tested on QEMU
// (test/suites/interrupt.c), where a real device raises the source.

#include <string.h>

#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "fakes.h"
#include "intctl.h"

enum { SOME_SOURCE = 10 };
enum { OUTER_SOURCE = 10, INNER_SOURCE = 11, QUIET_SOURCE = 12 };

static UINT s_calls;
extern volatile UINT tkmc_isr_depth;

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
  TEST_ASSERT_EQUAL_UINT(0, s_calls);
}

static UINT s_last_intno;

static void recording_handler(UINT intno) {
  s_last_intno = intno;
  s_calls += 1;
}

static const T_DINT s_recording = {.intatr = TA_HLNG,
                                   .inthdr = (FP)recording_handler};

static UINT plic_access_count(INT port, BOOL is_write) {
  UINT count = 0;
  for (UINT i = 0; i < host_log_count; ++i) {
    count += host_log[i].port == port && host_log[i].is_write == is_write;
  }
  return count;
}

static INT plic_priority_port(UINT src) {
  return PLIC_BASE_ADDRESS + PLIC_PRIORITY_OFFSET + 4 * src;
}

static BOOL plic_threshold_sequence_is_safe(UINT target, UW priority,
                                            UW restored_threshold) {
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
    if (access.port == (INT)host_plic_claim_port()) {
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
          if (!target_only) {
            return FALSE;
          }
        }
      }
    }
    if (access.is_write && access.port == plic_priority_port(target)) {
      last_priority_write = (INT)i;
    }
  }
  if (threshold_write_count != 2 || threshold_writes[0] < 0 ||
      threshold_writes[1] < 0 || claim_read < 0 || claim_write < 0 ||
      threshold_writes[0] >= claim_read || claim_read >= claim_write ||
      claim_write >= threshold_writes[1] || last_enable_write < 0 ||
      last_enable_write >= threshold_writes[1] || !threshold_writes_mie_off ||
      host_log[claim_read].interrupts_enabled ||
      host_log[threshold_writes[0]].value != 0 ||
      host_log[threshold_writes[1]].value != restored_threshold) {
    return FALSE;
  }
  if (priority == 0 && (last_priority_write < claim_write ||
                        last_priority_write >= threshold_writes[1] ||
                        host_log[last_priority_write].value != priority)) {
    return FALSE;
  }
  for (INT i = threshold_writes[0] + 1; i < claim_read; ++i) {
    if (host_log[i].is_write && host_log[i].port >= enable_first &&
        host_log[i].port <= enable_last) {
      return FALSE;
    }
  }
  return TRUE;
}

static BOOL plic_source_pending(UINT src) {
  if (src >= PLIC_SOURCE_COUNT) {
    return FALSE;
  }
  INT port = PLIC_BASE_ADDRESS + PLIC_PENDING_OFFSET + 4 * (src / 32);
  return (in_w(port) & (1u << (src % 32))) != 0;
}

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

void test_host_plic_claims_enabled_pending_sources_by_priority_then_id(void) {
  const UINT highest = 70, tied_small = 33, tied_large = 40, lowest = 10;
  const UINT disabled = 11, zero_priority = 12;
  host_plic_set_enabled(highest, TRUE);
  host_plic_set_enabled(tied_small, TRUE);
  host_plic_set_enabled(tied_large, TRUE);
  host_plic_set_enabled(lowest, TRUE);
  host_plic_set_enabled(zero_priority, TRUE);
  host_plic_set_priority(highest, 7);
  host_plic_set_priority(tied_small, 3);
  host_plic_set_priority(tied_large, 3);
  host_plic_set_priority(lowest, 1);
  host_plic_set_priority(disabled, 7);
  host_plic_set_priority(zero_priority, 0);
  host_plic_set_threshold(7);
  host_plic_raise(highest);
  host_plic_raise(tied_small);
  host_plic_raise(tied_large);
  host_plic_raise(lowest);
  host_plic_raise(disabled);
  host_plic_raise(zero_priority);

  UINT first = in_w(host_plic_claim_port());
  host_plic_raise(highest);
  BOOL claimed_source_stayed_not_pending = !plic_source_pending(highest);
  out_w(host_plic_claim_port(), highest);
  host_plic_raise(highest);
  BOOL reasserted_after_completion = plic_source_pending(highest);
  UINT second = in_w(host_plic_claim_port());
  out_w(host_plic_claim_port(), second);
  UINT third = in_w(host_plic_claim_port());
  out_w(host_plic_claim_port(), third);
  UINT fourth = in_w(host_plic_claim_port());
  out_w(host_plic_claim_port(), fourth);
  UINT fifth = in_w(host_plic_claim_port());
  out_w(host_plic_claim_port(), fifth);
  UINT empty = in_w(host_plic_claim_port());

  TEST_ASSERT_EQUAL_UINT(highest, first);
  TEST_ASSERT_TRUE(claimed_source_stayed_not_pending);
  TEST_ASSERT_TRUE(reasserted_after_completion);
  TEST_ASSERT_EQUAL_UINT(highest, second);
  TEST_ASSERT_EQUAL_UINT(tied_small, third);
  TEST_ASSERT_EQUAL_UINT(tied_large, fourth);
  TEST_ASSERT_EQUAL_UINT(lowest, fifth);
  TEST_ASSERT_EQUAL_UINT(0, empty);
  TEST_ASSERT_TRUE(CheckInt(disabled));
  TEST_ASSERT_TRUE(CheckInt(zero_priority));
  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());
}

void test_host_plic_disabled_completion_keeps_the_claim_until_reenabled(void) {
  const UINT src = 30;
  host_plic_set_enabled(src, TRUE);
  host_plic_set_priority(src, 3);
  host_plic_raise(src);
  UINT claimed = in_w(host_plic_claim_port());
  host_plic_set_enabled(src, FALSE);
  out_w(host_plic_claim_port(), src);
  host_plic_raise(src);
  BOOL still_claimed = !CheckInt(src);
  UINT ignored_completion = host_plic_ignored_completions();
  host_plic_set_enabled(src, TRUE);
  out_w(host_plic_claim_port(), src);
  host_plic_raise(src);
  BOOL released = CheckInt(src);
  UINT reclaimed = in_w(host_plic_claim_port());
  out_w(host_plic_claim_port(), reclaimed);

  TEST_ASSERT_EQUAL_UINT(src, claimed);
  TEST_ASSERT_TRUE(still_claimed);
  TEST_ASSERT_TRUE(released);
  TEST_ASSERT_EQUAL_UINT(src, reclaimed);
  TEST_ASSERT_EQUAL_UINT(1, ignored_completion);
}

void test_clear_int_invalid_numbers_do_no_mmio_and_keep_mie(void) {
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
    UINT accesses = host_log_count;
    BOOL mie_preserved = host_interrupts_enabled() == (mie != 0);
    EI(saved);

    TEST_ASSERT_EQUAL_UINT(0, accesses);
    TEST_ASSERT_TRUE(mie_preserved);
  }
}

void test_clear_int_without_pending_never_completes_and_restores_configuration(
    void) {
  static const UW priorities[] = {0, 1, 7};
  static const UW thresholds[] = {0, 7};
  for (UINT registered = 0; registered < 2; ++registered) {
    for (UINT enabled = 0; enabled < 2; ++enabled) {
      for (UINT p = 0; p < sizeof(priorities) / sizeof(priorities[0]); ++p) {
        for (UINT t = 0; t < sizeof(thresholds) / sizeof(thresholds[0]); ++t) {
          for (UINT mie = 0; mie < 2; ++mie) {
            host_reset();
            if (registered != 0) {
              TEST_ASSERT_EQUAL(E_OK, tk_def_int(SOME_SOURCE, &s_recording));
            }
            host_plic_set_enabled(SOME_SOURCE, enabled != 0);
            host_plic_set_priority(SOME_SOURCE, priorities[p]);
            host_plic_set_threshold(thresholds[t]);
            UINT saved = 0;
            if (mie != 0) {
              EI(0);
            } else {
              DI(saved);
            }

            host_log_count = 0;
            ClearInt(SOME_SOURCE);
            UINT first_reads = plic_access_count(host_plic_claim_port(), FALSE);
            UINT first_writes = plic_access_count(host_plic_claim_port(), TRUE);
            BOOL first_read_zero = FALSE;
            for (UINT i = 0; i < host_log_count; ++i) {
              if (host_log[i].port == (INT)host_plic_claim_port() &&
                  !host_log[i].is_write) {
                first_read_zero = host_log[i].value == 0;
              }
            }
            BOOL first_no_pending_write = !plic_pending_was_written();
            BOOL first_mie_preserved = host_interrupts_enabled() == (mie != 0);

            host_log_count = 0;
            ClearInt(SOME_SOURCE);
            UINT second_reads =
                plic_access_count(host_plic_claim_port(), FALSE);
            UINT second_writes =
                plic_access_count(host_plic_claim_port(), TRUE);
            BOOL second_read_zero = FALSE;
            for (UINT i = 0; i < host_log_count; ++i) {
              if (host_log[i].port == (INT)host_plic_claim_port() &&
                  !host_log[i].is_write) {
                second_read_zero = host_log[i].value == 0;
              }
            }
            BOOL second_no_pending_write = !plic_pending_was_written();
            BOOL config_preserved =
                host_plic_is_enabled(SOME_SOURCE) == (enabled != 0) &&
                in_w(plic_priority_port(SOME_SOURCE)) == priorities[p] &&
                host_plic_threshold() == thresholds[t] &&
                !CheckInt(SOME_SOURCE);
            BOOL second_mie_preserved = host_interrupts_enabled() == (mie != 0);
            UINT ignored_completions = host_plic_ignored_completions();
            EI(saved);

            TEST_ASSERT_EQUAL_UINT(1, first_reads);
            TEST_ASSERT_EQUAL_UINT(0, first_writes);
            TEST_ASSERT_TRUE(first_read_zero);
            TEST_ASSERT_TRUE(first_no_pending_write);
            TEST_ASSERT_TRUE(first_mie_preserved);
            TEST_ASSERT_EQUAL_UINT(1, second_reads);
            TEST_ASSERT_EQUAL_UINT(0, second_writes);
            TEST_ASSERT_TRUE(second_read_zero);
            TEST_ASSERT_TRUE(second_no_pending_write);
            TEST_ASSERT_TRUE(second_mie_preserved);
            TEST_ASSERT_TRUE(config_preserved);
            TEST_ASSERT_EQUAL_UINT(0, ignored_completions);
          }
        }
      }
    }
  }
}

void test_clear_int_claims_only_its_pending_source_and_preserves_all_words(
    void) {
  static const UW priorities[] = {0, 1, 7};
  static const UW thresholds[] = {0, 7};
  for (UINT registered = 0; registered < 2; ++registered) {
    for (UINT enabled = 0; enabled < 2; ++enabled) {
      for (UINT p = 0; p < sizeof(priorities) / sizeof(priorities[0]); ++p) {
        for (UINT t = 0; t < sizeof(thresholds) / sizeof(thresholds[0]); ++t) {
          for (UINT mie = 0; mie < 2; ++mie) {
            host_reset();
            if (registered != 0) {
              TEST_ASSERT_EQUAL(E_OK, tk_def_int(SOME_SOURCE, &s_recording));
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
              priority_before[src] = in_w(plic_priority_port(src));
            }

            UINT saved = 0;
            if (mie != 0) {
              EI(0);
            } else {
              DI(saved);
            }
            host_snapshot_t before;
            host_take_snapshot(&before);
            host_log_count = 0;
            ClearInt(SOME_SOURCE);
            UINT claim_reads = plic_access_count(host_plic_claim_port(), FALSE);
            UINT eoi_writes = plic_access_count(host_plic_claim_port(), TRUE);
            UINT eoi_source =
                eoi_writes != 0 ? (UINT)host_last_write(host_plic_claim_port())
                                : 0;
            UINT ignored_completions = host_plic_ignored_completions();
            BOOL no_pending_write = !plic_pending_was_written();
            const INT threshold_port =
                PLIC_BASE_ADDRESS + PLIC_THRESHOLD_OFFSET;
            BOOL threshold_sequence_safe =
                plic_access_count(threshold_port, TRUE) == 2 &&
                plic_threshold_sequence_is_safe(SOME_SOURCE, priorities[p],
                                                thresholds[t]);
            BOOL mie_preserved = host_interrupts_enabled() == (mie != 0);
            BOOL target_cleared = !plic_source_pending(SOME_SOURCE);
            BOOL competitors_pending = plic_source_pending(smaller) &&
                                       plic_source_pending(other_word) &&
                                       plic_source_pending(last_word);
            BOOL config_preserved = host_plic_threshold() == thresholds[t];
            for (UINT src = 1; src < PLIC_SOURCE_COUNT; ++src) {
              config_preserved &=
                  host_plic_is_enabled(src) == enabled_before[src];
              config_preserved &=
                  in_w(plic_priority_port(src)) == priority_before[src];
            }
            BOOL all_competitors_claimed_in_order = TRUE;
            const UINT claim_order[] = {smaller, last_word, other_word};
            for (UINT i = 0; i < sizeof(claim_order) / sizeof(claim_order[0]);
                 ++i) {
              UINT claimed = in_w(host_plic_claim_port());
              all_competitors_claimed_in_order &= claimed == claim_order[i];
              out_w(host_plic_claim_port(), claimed);
            }
            UINT no_more_claims = in_w(host_plic_claim_port());
            BOOL no_other_pending = !plic_source_pending(smaller) &&
                                    !plic_source_pending(other_word) &&
                                    !plic_source_pending(last_word);
            BOOL current_mie_preserved =
                host_interrupts_enabled() == (mie != 0);
            host_assert_unchanged(&before,
                                  "ClearInt changed queues or dispatch");
            EI(saved);

            TEST_ASSERT_EQUAL_UINT(1, claim_reads);
            TEST_ASSERT_EQUAL_UINT(1, eoi_writes);
            TEST_ASSERT_EQUAL_UINT(SOME_SOURCE, eoi_source);
            TEST_ASSERT_TRUE(no_pending_write);
            TEST_ASSERT_TRUE(threshold_sequence_safe);
            TEST_ASSERT_TRUE(mie_preserved);
            TEST_ASSERT_TRUE(current_mie_preserved);
            TEST_ASSERT_TRUE(target_cleared);
            TEST_ASSERT_TRUE(competitors_pending);
            TEST_ASSERT_TRUE(config_preserved);
            TEST_ASSERT_TRUE(all_competitors_claimed_in_order);
            TEST_ASSERT_EQUAL_UINT(0, no_more_claims);
            TEST_ASSERT_TRUE(no_other_pending);
            TEST_ASSERT_EQUAL_UINT(0, ignored_completions);
          }
        }
      }
    }
  }
}

void test_clear_int_in_a_nested_handler_preserves_outer_claim_and_record(void) {
  _Alignas(16) UW outer_frame[32] = {0};
  _Alignas(16) UW inner_frame[32] = {0};
  tkmc_interrupt_context outer, inner;
  const T_DINT assembler = {.intatr = TA_ASM, .inthdr = (FP)counting_handler};
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

  ClearInt(OUTER_SOURCE);
  UINT outstanding_claim_reads =
      plic_access_count(host_plic_claim_port(), FALSE);
  UINT outstanding_eois = plic_access_count(host_plic_claim_port(), TRUE);
  BOOL outstanding_record_unchanged =
      memcmp(&outer, &outer_before, sizeof(outer)) == 0 &&
      memcmp(&inner, &inner_before, sizeof(inner)) == 0 &&
      tkmc_active_interrupt_context == &inner && tkmc_isr_depth == 2 &&
      host_plic_threshold() == 5;
  host_plic_raise(OUTER_SOURCE);
  BOOL gateway_held_until_completion = !CheckInt(OUTER_SOURCE);
  EndOfInt(OUTER_SOURCE);
  UINT end_of_int_eois = plic_access_count(host_plic_claim_port(), TRUE);
  BOOL end_of_int_completed_outer =
      (outer.kind & TKMC_INTERRUPT_COMPLETED) != 0 &&
      (inner.kind & TKMC_INTERRUPT_COMPLETED) == 0;
  host_plic_raise(OUTER_SOURCE);
  BOOL new_request_is_pending = CheckInt(OUTER_SOURCE);
  outer_before = outer;
  inner_before = inner;
  host_log_count = 0;

  ClearInt(OUTER_SOURCE);
  UINT new_claim_reads = plic_access_count(host_plic_claim_port(), FALSE);
  UINT new_eois = plic_access_count(host_plic_claim_port(), TRUE);
  UINT new_eoi_source =
      new_eois != 0 ? (UINT)host_last_write(host_plic_claim_port()) : 0;
  BOOL new_request_cleared = !CheckInt(OUTER_SOURCE);
  BOOL nested_chain_unchanged =
      memcmp(&outer, &outer_before, sizeof(outer)) == 0 &&
      memcmp(&inner, &inner_before, sizeof(inner)) == 0 &&
      tkmc_active_interrupt_context == &inner && inner.previous == &outer &&
      tkmc_isr_depth == 2 && host_plic_threshold() == 5;
  BOOL mie_preserved = !host_interrupts_enabled();
  BOOL source_states_preserved = host_plic_is_enabled(OUTER_SOURCE) &&
                                 host_plic_is_enabled(INNER_SOURCE) &&
                                 in_w(plic_priority_port(OUTER_SOURCE)) == 2 &&
                                 in_w(plic_priority_port(INNER_SOURCE)) == 5;
  BOOL no_pending_write = !plic_pending_was_written();
  UINT ignored_completions = host_plic_ignored_completions();

  tkmc_finish_external_interrupt(&inner);
  tkmc_isr_depth = 1;
  tkmc_finish_external_interrupt(&outer);
  UINT total_eois =
      end_of_int_eois + plic_access_count(host_plic_claim_port(), TRUE);
  BOOL restored_threshold = host_plic_threshold() == 1;
  BOOL restored_chain = tkmc_active_interrupt_context == NULL;
  tkmc_isr_depth = 0;
  EI(saved);
  host_assert_unchanged(&queues_before,
                        "nested ClearInt changed queues or dispatch");

  TEST_ASSERT_EQUAL_UINT(1, task);
  TEST_ASSERT_EQUAL_UINT(1, end_of_int_eois);
  TEST_ASSERT_EQUAL_UINT(1, outstanding_claim_reads);
  TEST_ASSERT_EQUAL_UINT(0, outstanding_eois);
  TEST_ASSERT_TRUE(outstanding_record_unchanged);
  TEST_ASSERT_TRUE(gateway_held_until_completion);
  TEST_ASSERT_TRUE(end_of_int_completed_outer);
  TEST_ASSERT_TRUE(new_request_is_pending);
  TEST_ASSERT_EQUAL_UINT(1, new_claim_reads);
  TEST_ASSERT_EQUAL_UINT(1, new_eois);
  TEST_ASSERT_EQUAL_UINT(OUTER_SOURCE, new_eoi_source);
  TEST_ASSERT_TRUE(new_request_cleared);
  TEST_ASSERT_TRUE(nested_chain_unchanged);
  TEST_ASSERT_TRUE(mie_preserved);
  TEST_ASSERT_TRUE(source_states_preserved);
  TEST_ASSERT_TRUE(no_pending_write);
  TEST_ASSERT_EQUAL_UINT(0, ignored_completions);
  TEST_ASSERT_EQUAL_UINT(3, total_eois);
  TEST_ASSERT_TRUE(restored_threshold);
  TEST_ASSERT_TRUE(restored_chain);
  TEST_ASSERT_TRUE(host_interrupts_enabled());
}

// The first and the last number of the table are served like any other, and
// with their own number.
void test_the_first_and_the_last_interrupt_number_have_a_handler(void) {
  static const UINT edges[] = {1, CFN_MAX_INTNO - 1};
  for (UINT i = 0; i < 2; ++i) {
    s_calls = 0;
    s_last_intno = 0;
    TEST_ASSERT_EQUAL(E_OK, tk_def_int(edges[i], &s_recording));
    EnableInt(edges[i], 1);
    host_plic_raise(edges[i]);
    tkmc_external_interrupt();
    TEST_ASSERT_EQUAL_UINT(1, s_calls);
    TEST_ASSERT_EQUAL_UINT(edges[i], s_last_intno);
  }
}

void test_check_int_reads_one_pending_word_without_claiming(void) {
  enum { NO_CURRENT, TASK_CONTEXT, ISR_CONTEXT, CONTEXTS, MIE_STATES = 2 };
  enum { MATRIX_SIZE = CONTEXTS * MIE_STATES };
  BOOL results[MATRIX_SIZE], mie_preserved[MATRIX_SIZE];
  UINT depths[MATRIX_SIZE], access_counts[MATRIX_SIZE];
  host_access_t accesses[MATRIX_SIZE] = {0};
  BOOL invalid_results[4];
  UINT invalid_access_count;
  BOOL enabled_pending;
  BOOL enabled_source;
  UINT enabled_access_count;
  host_access_t enabled_access = {0};
  const INT pending_port =
      PLIC_BASE_ADDRESS + PLIC_PENDING_OFFSET + 4 * (31 / 32);
  ID task = host_make_task(1);
  if (task <= 0) {
    host_reset();
    TEST_ASSERT_GREATER_THAN(0, task);
    return;
  }

  host_plic_set_enabled(31, FALSE);
  host_plic_set_priority(31, 0);
  host_plic_set_threshold(7);
  host_plic_raise(1);
  host_log_count = 0;
  invalid_results[0] = CheckInt(0);
  invalid_results[1] = CheckInt(CFN_MAX_INTNO);
  invalid_results[2] = CheckInt(33);
  invalid_results[3] = CheckInt(0xffffffffu);
  invalid_access_count = host_log_count;

  // Source 1 is the first valid number and can be pending without source 31.
  host_log_count = 0;
  BOOL first_pending = CheckInt(1);
  UINT first_access_count = host_log_count;
  host_access_t first_access =
      host_log_count != 0 ? host_log[0] : (host_access_t){0};
  host_log_count = 0;
  BOOL other_source_pending = CheckInt(31);
  UINT other_access_count = host_log_count;
  host_access_t other_access =
      host_log_count != 0 ? host_log[0] : (host_access_t){0};

  host_plic_raise(31);
  for (UINT context = 0; context < CONTEXTS; ++context) {
    for (UINT mie = 0; mie < MIE_STATES; ++mie) {
      UINT index = context * MIE_STATES + mie;
      if (context == NO_CURRENT) {
        current = next = NULL;
      } else {
        host_set_current(task);
      }
      tkmc_isr_depth = context == ISR_CONTEXT ? 1 : 0;
      if (mie != 0) {
        EI(0);
      } else {
        UINT intsts;
        DI(intsts);
        (void)intsts;
      }

      host_snapshot_t before;
      host_take_snapshot(&before);
      host_log_count = 0;
      results[index] = CheckInt(31);
      access_counts[index] = host_log_count;
      if (host_log_count != 0) {
        accesses[index] = host_log[0];
      }
      mie_preserved[index] = host_interrupts_enabled() == (mie != 0);
      depths[index] = tkmc_isr_depth;
      // Restore the fixture before any assertion can abort this test.
      tkmc_isr_depth = 0;
      EI(0);
      host_assert_unchanged(&before, "CheckInt changed kernel state");
    }
  }

  current = next = NULL;
  host_plic_set_enabled(31, TRUE);
  host_plic_set_priority(31, 1);
  enabled_source = host_plic_is_enabled(31);
  host_log_count = 0;
  enabled_pending = CheckInt(31);
  enabled_access_count = host_log_count;
  if (host_log_count != 0) {
    enabled_access = host_log[0];
  }
  UINT threshold = host_plic_threshold();
  UINT claim_writes = host_write_count(host_plic_claim_port());
  host_reset();

  TEST_ASSERT_EQUAL_UINT(1, task);
  TEST_ASSERT_EQUAL_UINT(0, invalid_access_count);
  for (UINT i = 0; i < 4; ++i) {
    TEST_ASSERT_FALSE(invalid_results[i]);
  }
  TEST_ASSERT_TRUE(first_pending);
  TEST_ASSERT_EQUAL_UINT(1, first_access_count);
  TEST_ASSERT_EQUAL_UINT(4, first_access.width);
  TEST_ASSERT_EQUAL_UINT(PLIC_BASE_ADDRESS + PLIC_PENDING_OFFSET,
                         first_access.port);
  TEST_ASSERT_FALSE(first_access.is_write);
  TEST_ASSERT_FALSE(other_source_pending);
  TEST_ASSERT_EQUAL_UINT(1, other_access_count);
  TEST_ASSERT_EQUAL_UINT(pending_port, other_access.port);
  TEST_ASSERT_EQUAL_UINT(4, other_access.width);
  TEST_ASSERT_FALSE(other_access.is_write);
  for (UINT i = 0; i < MATRIX_SIZE; ++i) {
    TEST_ASSERT_TRUE(results[i]);
    TEST_ASSERT_TRUE(mie_preserved[i]);
    TEST_ASSERT_EQUAL_UINT(i / MIE_STATES == ISR_CONTEXT ? 1 : 0, depths[i]);
    TEST_ASSERT_EQUAL_UINT(1, access_counts[i]);
    TEST_ASSERT_EQUAL_UINT(pending_port, accesses[i].port);
    TEST_ASSERT_EQUAL_UINT(4, accesses[i].width);
    TEST_ASSERT_FALSE(accesses[i].is_write);
  }
  TEST_ASSERT_TRUE(enabled_pending);
  TEST_ASSERT_TRUE(enabled_source);
  TEST_ASSERT_EQUAL_UINT(1, enabled_access_count);
  TEST_ASSERT_EQUAL_UINT(pending_port, enabled_access.port);
  TEST_ASSERT_EQUAL_UINT(4, enabled_access.width);
  TEST_ASSERT_FALSE(enabled_access.is_write);
  TEST_ASSERT_EQUAL_UINT(7, threshold);
  TEST_ASSERT_EQUAL_UINT(0, claim_writes);

  // Repeated pending reads leave the notification for the later claim.
  host_plic_raise(31);
  TEST_ASSERT_TRUE(CheckInt(31));
  TEST_ASSERT_TRUE(CheckInt(31));
  s_calls = 0;
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(31, &s_recording));
  EnableInt(31, 1);
  tkmc_external_interrupt();
  TEST_ASSERT_EQUAL_UINT(1, s_calls);
  TEST_ASSERT_FALSE(CheckInt(31));
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

void test_a_driver_written_to_the_profile_enables_a_source_at_its_level(void) {
  enum { LEVEL = 3 };
  enable_as_the_profile_says(SOME_SOURCE, LEVEL);
  TEST_ASSERT_TRUE(host_plic_is_enabled(SOME_SOURCE));
  TEST_ASSERT_EQUAL_UINT32(
      LEVEL, in_w(PLIC_BASE_ADDRESS + PLIC_PRIORITY_OFFSET + 4 * SOME_SOURCE));
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
  T_DINT unknown_attribute = {.intatr = 2, .inthdr = (FP)counting_handler};
  TEST_ASSERT_EQUAL(E_PAR, tk_def_int(0, NULL));
  TEST_ASSERT_EQUAL(E_PAR, tk_def_int(CFN_MAX_INTNO, NULL));
  TEST_ASSERT_EQUAL(E_PAR, tk_def_int(0xffffffffu, NULL));
  TEST_ASSERT_EQUAL(E_PAR, tk_def_int(SOME_SOURCE, &no_handler));
  TEST_ASSERT_EQUAL(E_RSATR, tk_def_int(SOME_SOURCE, &unknown_attribute));
  EnableInt(SOME_SOURCE, 1);
  host_plic_raise(SOME_SOURCE);
  tkmc_external_interrupt();
  TEST_ASSERT_EQUAL_UINT(1, s_calls);
  TEST_ASSERT_EQUAL_UINT(SOME_SOURCE, s_last_intno);
}

void test_asm_interrupt_uses_the_raw_prepare_finish_path(void) {
  _Alignas(16) UW frame[32] = {0};
  static void (*const entry)(UINT) = counting_handler;
  const T_DINT assembler = {.intatr = TA_ASM, .inthdr = (FP)entry};
  tkmc_interrupt_context context;
  UINT saved;

  s_calls = 0;
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(SOME_SOURCE, &assembler));
  EnableInt(SOME_SOURCE, 4);
  host_plic_set_threshold(2);
  host_plic_raise(SOME_SOURCE);
  DI(saved);
  tkmc_isr_depth = 1;
  tkmc_prepare_external_interrupt(&context, frame);
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_ASM, context.kind);
  TEST_ASSERT_EQUAL_UINT(SOME_SOURCE, context.intno);
  TEST_ASSERT_EQUAL_PTR(entry, context.handler);
  TEST_ASSERT_EQUAL_PTR(frame, context.frame);
  TEST_ASSERT_EQUAL_PTR(&context, context.entry_sp);
  TEST_ASSERT_EQUAL_UINT(1, context.depth);
  TEST_ASSERT_EQUAL_UINT(2, context.saved_threshold);
  TEST_ASSERT_EQUAL_UINT(4, host_plic_threshold());
  TEST_ASSERT_EQUAL_UINT(0, s_calls);

  DisableInt(SOME_SOURCE);
  tkmc_finish_external_interrupt(&context);
  TEST_ASSERT_EQUAL_UINT(2, host_plic_threshold());
  TEST_ASSERT_FALSE(host_plic_is_enabled(SOME_SOURCE));
  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());
  TEST_ASSERT_EQUAL_UINT(1, host_write_count(host_plic_claim_port()));
  TEST_ASSERT_EQUAL_UINT(SOME_SOURCE, host_last_write(host_plic_claim_port()));
  TEST_ASSERT_EQUAL_UINT(0, s_calls);
  TEST_ASSERT_FALSE(host_interrupts_enabled());
  tkmc_isr_depth = 0;
  EI(saved);
}

void test_asm_prepare_without_a_claim_is_a_no_op(void) {
  _Alignas(16) UW frame[32] = {0};
  tkmc_interrupt_context context;
  UINT saved;
  host_plic_set_threshold(3);
  host_plic_raise(0);
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

void test_nested_asm_contexts_restore_each_claim_and_threshold(void) {
  _Alignas(16) UW outer_frame[32] = {0};
  _Alignas(16) UW inner_frame[32] = {0};
  const T_DINT outer = {.intatr = TA_ASM, .inthdr = (FP)counting_handler};
  const T_DINT inner = {.intatr = TA_ASM, .inthdr = (FP)counting_handler};
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

// The ISR state alone refuses the call, with interrupts on: on the target a
// handler runs with them off, and the state is what a later handler that turns
// them on would be judged by.
void test_a_handler_cannot_be_defined_in_the_isr_state(void) {
  // Depth 2 is a trap taken inside a trap: still the ISR state.
  for (UINT depth = 1; depth <= 2; ++depth) {
    tkmc_isr_depth = depth;
    TEST_ASSERT_EQUAL(E_CTX, tk_def_int(SOME_SOURCE, &s_recording));
  }
  tkmc_isr_depth = 0;
  EnableInt(SOME_SOURCE, 1);
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
  EnableInt(SOME_SOURCE, 1);
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

void test_end_of_int_completes_once_before_a_handler_exits(void) {
  _Alignas(16) UW frame[32] = {0};
  tkmc_interrupt_context context;
  UINT saved;
  const T_DINT assembler = {.intatr = TA_ASM, .inthdr = (FP)counting_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(SOME_SOURCE, &assembler));
  EnableInt(SOME_SOURCE, 3);
  host_plic_set_threshold(2);
  host_plic_raise(SOME_SOURCE);
  DI(saved);
  tkmc_isr_depth = 1;
  tkmc_prepare_external_interrupt(&context, frame);
  host_plic_set_enabled(SOME_SOURCE, FALSE);
  host_plic_raise(INNER_SOURCE);
  host_log_count = 0;

  EndOfInt(SOME_SOURCE);
  UINT immediate_writes = host_write_count(host_plic_claim_port());
  UINT immediate_last = (UINT)host_last_write(host_plic_claim_port());
  UINT claim_reads = 0;
  for (UINT i = 0; i < host_log_count; ++i) {
    claim_reads += host_log[i].port == (INT)host_plic_claim_port() &&
                   !host_log[i].is_write;
  }
  BOOL disabled_restored = !host_plic_is_enabled(SOME_SOURCE);
  BOOL other_pending = CheckInt(INNER_SOURCE);
  EndOfInt(SOME_SOURCE);
  UINT duplicate_writes = host_write_count(host_plic_claim_port());
  BOOL mie_restored = !host_interrupts_enabled();
  tkmc_finish_external_interrupt(&context);
  UINT total_writes = host_write_count(host_plic_claim_port());
  tkmc_isr_depth = 0;
  EI(saved);

  TEST_ASSERT_EQUAL_UINT(1, immediate_writes);
  TEST_ASSERT_EQUAL_UINT(SOME_SOURCE, immediate_last);
  TEST_ASSERT_EQUAL_UINT(0, claim_reads);
  TEST_ASSERT_TRUE(disabled_restored);
  TEST_ASSERT_TRUE(other_pending);
  TEST_ASSERT_EQUAL_UINT(1, duplicate_writes);
  TEST_ASSERT_EQUAL_UINT(1, total_writes);
  TEST_ASSERT_TRUE(mie_restored);
  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());
  TEST_ASSERT_EQUAL_UINT(2, host_plic_threshold());
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_ASM | TKMC_INTERRUPT_COMPLETED,
                         context.kind);
  TEST_ASSERT_TRUE(host_interrupts_enabled());
}

void test_end_of_int_can_complete_an_outer_nested_claim_without_popping(void) {
  _Alignas(16) UW outer_frame[32] = {0};
  _Alignas(16) UW inner_frame[32] = {0};
  tkmc_interrupt_context outer, inner;
  UINT saved;
  const T_DINT assembler = {.intatr = TA_ASM, .inthdr = (FP)counting_handler};
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
  UINT writes_before_exit = host_write_count(host_plic_claim_port());
  BOOL outer_completed = (outer.kind & TKMC_INTERRUPT_COMPLETED) != 0;
  BOOL inner_untouched = (inner.kind & TKMC_INTERRUPT_COMPLETED) == 0;
  BOOL chain_untouched = tkmc_active_interrupt_context == &inner &&
                         inner.previous == &outer && tkmc_isr_depth == 2 &&
                         host_plic_threshold() == 5;
  tkmc_finish_external_interrupt(&inner);
  tkmc_isr_depth = 1;
  tkmc_finish_external_interrupt(&outer);
  UINT writes_after_exit = host_write_count(host_plic_claim_port());
  tkmc_isr_depth = 0;
  EI(saved);

  TEST_ASSERT_EQUAL_UINT(1, writes_before_exit);
  TEST_ASSERT_EQUAL_UINT(2, writes_after_exit);
  TEST_ASSERT_TRUE(outer_completed);
  TEST_ASSERT_TRUE(inner_untouched);
  TEST_ASSERT_TRUE(chain_untouched);
  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());
  TEST_ASSERT_EQUAL_UINT(1, host_plic_threshold());
}

void test_end_of_int_preserves_enabled_mie_and_current_independence(void) {
  _Alignas(16) UW frame[32] = {0};
  tkmc_interrupt_context context;
  tkmc_interrupt_context other = {.intno = INNER_SOURCE,
                                  .depth = 0,
                                  .saved_threshold = 7,
                                  .kind = TKMC_INTERRUPT_KIND_HLNG};
  tkmc_interrupt_context other_before = other;
  tkmc_interrupt_context expected;
  const T_DINT assembler = {.intatr = TA_ASM, .inthdr = (FP)counting_handler};
  const INT priority_port =
      PLIC_BASE_ADDRESS + PLIC_PRIORITY_OFFSET + 4 * SOME_SOURCE;
  const INT other_priority_port =
      PLIC_BASE_ADDRESS + PLIC_PRIORITY_OFFSET + 4 * INNER_SOURCE;
  host_plic_set_threshold(1);
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(SOME_SOURCE, &assembler));
  EnableInt(SOME_SOURCE, 2);
  EnableInt(INNER_SOURCE, 5);
  tkmc_active_interrupt_context = &other;
  host_plic_raise(SOME_SOURCE);
  UINT saved;
  DI(saved);
  tkmc_isr_depth = 1;
  tkmc_prepare_external_interrupt(&context, frame);
  expected = context;
  expected.kind |= TKMC_INTERRUPT_COMPLETED;
  current = next = NULL;
  EI(0);
  host_plic_raise(INNER_SOURCE);
  host_snapshot_t before;
  host_take_snapshot(&before);
  const UW priority_before = in_w(priority_port);
  const UW other_priority_before = in_w(other_priority_port);
  const UW threshold_before = host_plic_threshold();
  host_log_count = 0;

  EndOfInt(SOME_SOURCE);
  UINT writes = host_write_count(host_plic_claim_port());
  UINT claim_reads = 0;
  for (UINT i = 0; i < host_log_count; ++i) {
    claim_reads += host_log[i].port == (INT)host_plic_claim_port() &&
                   !host_log[i].is_write;
  }
  host_assert_unchanged(&before,
                        "EndOfInt changed task queues or dispatch state");
  UINT depth_after_eoi = tkmc_isr_depth;
  BOOL chain_preserved =
      tkmc_active_interrupt_context == &context && context.previous == &other;
  BOOL context_preserved =
      context.previous == expected.previous &&
      context.frame == expected.frame &&
      context.entry_sp == expected.entry_sp &&
      context.handler == expected.handler && context.depth == expected.depth &&
      context.intno == expected.intno &&
      context.saved_threshold == expected.saved_threshold &&
      context.kind == expected.kind;
  BOOL other_context_preserved =
      other.previous == other_before.previous &&
      other.frame == other_before.frame &&
      other.entry_sp == other_before.entry_sp &&
      other.handler == other_before.handler &&
      other.depth == other_before.depth && other.intno == other_before.intno &&
      other.saved_threshold == other_before.saved_threshold &&
      other.kind == other_before.kind;
  BOOL enabled_preserved =
      host_plic_is_enabled(SOME_SOURCE) && host_plic_is_enabled(INNER_SOURCE);
  BOOL priorities_preserved =
      in_w(priority_port) == priority_before &&
      in_w(other_priority_port) == other_priority_before;
  BOOL threshold_preserved = host_plic_threshold() == threshold_before;
  BOOL other_pending = CheckInt(INNER_SOURCE);
  BOOL mie_restored = host_interrupts_enabled();
  tkmc_finish_external_interrupt(&context);
  tkmc_isr_depth = 0;
  tkmc_active_interrupt_context = NULL;
  current = next = NULL;
  EI(saved);

  TEST_ASSERT_EQUAL_UINT(1, writes);
  TEST_ASSERT_EQUAL_UINT(0, claim_reads);
  TEST_ASSERT_TRUE(context_preserved);
  TEST_ASSERT_TRUE(other_context_preserved);
  TEST_ASSERT_EQUAL_UINT(1, depth_after_eoi);
  TEST_ASSERT_TRUE(chain_preserved);
  TEST_ASSERT_TRUE(enabled_preserved);
  TEST_ASSERT_TRUE(priorities_preserved);
  TEST_ASSERT_TRUE(threshold_preserved);
  TEST_ASSERT_TRUE(other_pending);
  TEST_ASSERT_TRUE(mie_restored);
  TEST_ASSERT_TRUE(host_interrupts_enabled());
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_ASM | TKMC_INTERRUPT_COMPLETED,
                         expected.kind);
  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());
}

void test_end_of_int_invalid_inactive_and_unmatched_calls_do_no_mmio(void) {
  _Alignas(16) UW frame[32] = {0};
  tkmc_interrupt_context context;
  UINT saved;
  host_log_count = 0;
  EndOfInt(0);
  EndOfInt(CFN_MAX_INTNO);
  EndOfInt(0xffffffffu);
  EndOfInt(SOME_SOURCE);
  UINT inactive_accesses = host_log_count;

  const T_DINT assembler = {.intatr = TA_ASM, .inthdr = (FP)counting_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(SOME_SOURCE, &assembler));
  EnableInt(SOME_SOURCE, 2);
  host_plic_raise(SOME_SOURCE);
  DI(saved);
  tkmc_isr_depth = 1;
  tkmc_prepare_external_interrupt(&context, frame);
  host_log_count = 0;
  EndOfInt(INNER_SOURCE);
  UINT unmatched_accesses = host_log_count;
  BOOL unmatched_not_completed = (context.kind & TKMC_INTERRUPT_COMPLETED) == 0;
  tkmc_finish_external_interrupt(&context);
  tkmc_isr_depth = 0;
  EI(saved);

  TEST_ASSERT_EQUAL_UINT(0, inactive_accesses);
  TEST_ASSERT_EQUAL_UINT(0, unmatched_accesses);
  TEST_ASSERT_TRUE(unmatched_not_completed);
  TEST_ASSERT_EQUAL_UINT(1, host_write_count(host_plic_claim_port()));
}

void test_end_of_int_stops_at_a_completed_newest_matching_record(void) {
  tkmc_interrupt_context older = {.intno = SOME_SOURCE,
                                  .kind = TKMC_INTERRUPT_KIND_ASM};
  tkmc_interrupt_context newer = {.previous = &older,
                                  .intno = SOME_SOURCE,
                                  .kind = TKMC_INTERRUPT_KIND_HLNG |
                                          TKMC_INTERRUPT_COMPLETED};
  tkmc_active_interrupt_context = &newer;
  host_log_count = 0;
  EndOfInt(SOME_SOURCE);
  UINT accesses = host_log_count;
  tkmc_active_interrupt_context = NULL;

  TEST_ASSERT_EQUAL_UINT(0, accesses);
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_ASM, older.kind);
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_HLNG | TKMC_INTERRUPT_COMPLETED,
                         newer.kind);
}

void test_prepare_clears_stale_completion_from_a_reused_record(void) {
  _Alignas(16) UW frame[32] = {0};
  tkmc_interrupt_context context = {.kind = TKMC_INTERRUPT_KIND_ASM |
                                            TKMC_INTERRUPT_COMPLETED};
  UINT saved;
  const T_DINT assembler = {.intatr = TA_ASM, .inthdr = (FP)counting_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(SOME_SOURCE, &assembler));
  EnableInt(SOME_SOURCE, 2);
  host_plic_raise(SOME_SOURCE);
  DI(saved);
  tkmc_isr_depth = 1;
  tkmc_prepare_external_interrupt(&context, frame);
  BOOL cleared = context.kind == TKMC_INTERRUPT_KIND_ASM;
  tkmc_finish_external_interrupt(&context);
  tkmc_isr_depth = 0;
  EI(saved);

  TEST_ASSERT_TRUE(cleared);
  TEST_ASSERT_EQUAL_UINT(1, host_write_count(host_plic_claim_port()));
}
