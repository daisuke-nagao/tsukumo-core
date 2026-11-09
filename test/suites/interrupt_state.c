/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// EI() puts back the interrupt state that DI() saved, whatever the state is
// when EI() runs, and EI(0) turns interrupts on.
//
// The test sets mstatus.MIE and reads mstatus and mie with its own CSR
// instructions, never with the EI() under test. It masks every interrupt source
// (mie = 0) while it does so, so no trap can come between the instructions and
// what is read is the CSR state EI() left. The second case unmasks only the
// software interrupt, with a request pending, to catch a moment of interrupts
// on that the final state would not show.

#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "runner.h"

enum { MSIP = CLINT_BASE_ADDRESS + CLINT_MSIP_OFFSET, MIE_MSIE = 1U << 3 };

typedef struct {
  BOOL from_di;   // The saved value comes from DI(), else it is 0.
  UINT mie_at_di; // MIE when DI() saves it (rows from DI() only).
  UINT mie_before_ei;
  UINT expected_mie; // MIE after EI().
} row_t;

static const row_t s_rows[] = {
    {TRUE, 0, 0, 0},
    {TRUE, 0, MSTATUS_MIE, 0},
    {TRUE, MSTATUS_MIE, 0, MSTATUS_MIE},
    {TRUE, MSTATUS_MIE, MSTATUS_MIE, MSTATUS_MIE},
    {FALSE, 0, 0, MSTATUS_MIE},
    {FALSE, 0, MSTATUS_MIE, MSTATUS_MIE},
};

// What a row checks, as bit numbers within the row's bits of the mask.
enum {
  CHECK_MIE,        // MIE after EI() is the expected one.
  CHECK_SETUP,      // MIE before EI() and isDI() are as the row says.
  CHECK_OTHER_BITS, // EI() changed no mstatus bit but MIE.
  CHECK_SOURCES,    // EI() left mie at 0.
  CHECK_TOKEN,      // EI() left the saved value as it was.
  CHECKS
};
enum { ROWS = sizeof(s_rows) / sizeof(s_rows[0]) };
_Static_assert((ROWS * CHECKS) <= 32, "the mismatches must fit in one UINT");

static inline UINT read_mstatus(void) {
  UINT value;
  asm volatile("csrr %0, mstatus" : "=r"(value)::"memory");
  return value;
}

static inline void set_mie(UINT mie) {
  if (mie != 0) {
    asm volatile("csrsi mstatus, %0" ::"i"(MSTATUS_MIE) : "memory");
  } else {
    asm volatile("csrci mstatus, %0" ::"i"(MSTATUS_MIE) : "memory");
  }
}

// Runs one row and returns its mismatches, one bit per CHECK_*. Interrupts and
// their sources are as they were when it returns, mismatch or not, so the
// caller can assert after every row has run.
static UINT run_row(const row_t *row) {
  UINT sources;
  asm volatile("csrrw %0, mie, zero" : "=r"(sources)::"memory");
  const UINT entry_mie = read_mstatus() & MSTATUS_MIE;

  UINT saved = 0;
  if (row->from_di) {
    set_mie(row->mie_at_di);
    DI(saved);
  }
  const UINT token = saved;
  set_mie(row->mie_before_ei);
  const UINT before = read_mstatus();
  EI(saved);
  const UINT after = read_mstatus();
  UINT sources_after;
  asm volatile("csrr %0, mie" : "=r"(sources_after)::"memory");

  set_mie(entry_mie);
  asm volatile("csrw mie, %0" ::"r"(sources) : "memory");

  UINT mismatches = 0;
  if ((after & MSTATUS_MIE) != row->expected_mie) {
    mismatches |= 1U << CHECK_MIE;
  }
  if ((before & MSTATUS_MIE) != row->mie_before_ei ||
      (row->from_di && isDI(token) != (row->mie_at_di == 0))) {
    mismatches |= 1U << CHECK_SETUP;
  }
  if ((after & ~MSTATUS_MIE) != (before & ~MSTATUS_MIE)) {
    mismatches |= 1U << CHECK_OTHER_BITS;
  }
  if (sources_after != 0) {
    mismatches |= 1U << CHECK_SOURCES;
  }
  if (saved != token) {
    mismatches |= 1U << CHECK_TOKEN;
  }
  return mismatches;
}

static UINT run_nested(UINT mie_at_outer_di) {
  UINT sources;
  asm volatile("csrrw %0, mie, zero" : "=r"(sources)::"memory");
  const UINT entry_mie = read_mstatus() & MSTATUS_MIE;

  set_mie(mie_at_outer_di);
  UINT outer, inner;
  DI(outer);
  DI(inner);
  const BOOL inner_di = isDI(inner);
  EI(inner);
  const UINT after_inner_ei = read_mstatus() & MSTATUS_MIE;
  const BOOL outer_di = isDI(outer);
  EI(outer);
  const UINT after_outer_ei = read_mstatus() & MSTATUS_MIE;

  set_mie(entry_mie);
  asm volatile("csrw mie, %0" : : "r"(sources) : "memory");

  UINT mismatches = 0;
  if (!inner_di || outer_di != (mie_at_outer_di == 0)) {
    mismatches |= 1U << 0;
  }
  if (after_inner_ei != 0) {
    mismatches |= 1U << 1;
  }
  if (after_outer_ei != mie_at_outer_di) {
    mismatches |= 1U << 2;
  }
  return mismatches;
}

static void ei_restores_the_state_di_saved_whatever_mie_is_before_it(void) {
  UINT mask = 0;
  for (UINT row = 0; row < ROWS; ++row) {
    mask |= run_row(&s_rows[row]) << (row * CHECKS);
  }
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(
      0, mask,
      "mismatch bit = row * 5 + check; rows (saved by, MIE before EI): "
      "0 DI with MIE 0, 0; 1 DI with MIE 0, 1; 2 DI with MIE 1, 0; "
      "3 DI with MIE 1, 1; 4 EI(0), 0; 5 EI(0), 1; checks: 0 MIE after EI, "
      "1 setup, 2 other mstatus bits, 3 mie, 4 saved value");
}

static void nested_di_ei_restores_each_saved_state(void) {
  const UINT off = run_nested(0);
  const UINT on = run_nested(MSTATUS_MIE);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(
      0, off,
      "nested DI/EI while interrupts start off: bit 0 tokens, "
      "bit 1 inner EI state, bit 2 outer EI state");
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(
      0, on,
      "nested DI/EI while interrupts start on: bit 0 tokens, "
      "bit 1 inner EI state, bit 2 outer EI state");
}

// An EI() that puts interrupts back off must not have them on on the way, as an
// EI() that set MIE and then cleared it would. A software interrupt request
// left pending through it shows such a moment: the trap would be taken there,
// and the trap entry clears the request (mtrap_handler in start.S).
static void an_ei_back_to_off_takes_no_pending_interrupt(void) {
  const UW entry_msip = in_w(MSIP);
  UINT sources;
  asm volatile("csrrw %0, mie, zero" : "=r"(sources)::"memory");
  const UINT entry_mie = read_mstatus() & MSTATUS_MIE;
  set_mie(0);
  UINT saved;
  DI(saved);
  out_w(MSIP, 1);
  asm volatile("csrw mie, %0" ::"r"(MIE_MSIE) : "memory");

  EI(saved);
  const UINT mie_after = read_mstatus() & MSTATUS_MIE;
  const UW request = in_w(MSIP);
  UINT sources_after;
  asm volatile("csrr %0, mie" : "=r"(sources_after)::"memory");

  set_mie(0);
  out_w(MSIP, entry_msip);
  asm volatile("csrw mie, %0" ::"r"(sources) : "memory");
  set_mie(entry_mie);

  TEST_ASSERT_TRUE(isDI(saved));
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, request,
                                 "the pending software interrupt was taken");
  TEST_ASSERT_EQUAL_HEX32(0, mie_after);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(MIE_MSIE, sources_after,
                                  "EI() changed the interrupt sources");
}

static const test_case_t s_cases[] = {
    {"ei_restores_the_state_di_saved_whatever_mie_is_before_it",
     ei_restores_the_state_di_saved_whatever_mie_is_before_it},
    {"nested_di_ei_restores_each_saved_state",
     nested_di_ei_restores_each_saved_state},
    {"an_ei_back_to_off_takes_no_pending_interrupt",
     an_ei_back_to_off_takes_no_pending_interrupt},
};

TEST_SUITE_MAIN("interrupt-state", s_cases, 5000)
