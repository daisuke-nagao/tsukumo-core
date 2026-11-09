/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// The fake DI()/EI() keep the contract of the real ones (include/sys/
// interrupt.h): EI() puts back the interrupt state that DI() saved, whatever
// the state is when EI() runs, and EI(0) turns interrupts on. The QEMU suite
// interrupt-state checks the real header; these check that the PC tests run the
// kernel on the same contract, and that an EI() that turns interrupts off does
// not take the switch the fake machine takes when they come on.

#include <setjmp.h>

#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "fakes.h"

enum { PRIORITY = 4, MSIP = CLINT_BASE_ADDRESS + CLINT_MSIP_OFFSET };

typedef struct {
  BOOL from_di;  // The saved value comes from DI(), else it is 0.
  BOOL on_at_di; // Interrupts are on when DI() saves them.
  BOOL on_before_ei;
  BOOL expected_on; // Interrupts are on after EI().
} row_t;

static const row_t s_rows[] = {
    {TRUE, FALSE, FALSE, FALSE}, {TRUE, FALSE, TRUE, FALSE},
    {TRUE, TRUE, FALSE, TRUE},   {TRUE, TRUE, TRUE, TRUE},
    {FALSE, FALSE, FALSE, TRUE}, {FALSE, FALSE, TRUE, TRUE},
};

// What a row checks, as bit numbers within the row's bits of the mask. The
// fake EI() takes its argument by value, so unlike on the target there is no
// saved value it could change.
enum {
  CHECK_ON,    // Interrupts after EI() are as expected.
  CHECK_SETUP, // Interrupts before EI() and isDI() are as the row says.
  CHECKS
};
enum { ROWS = sizeof(s_rows) / sizeof(s_rows[0]) };
_Static_assert((ROWS * CHECKS) <= 32, "the mismatches must fit in one UINT");

// Interrupts are turned on by a fresh fake machine and off by DI(), so that
// the EI() under test is not what sets a row up.
static void interrupts(BOOL on) {
  host_reset();
  if (!on) {
    UINT ignored;
    DI(ignored);
  }
}

static UINT run_row(const row_t *row) {
  UINT saved = 0;
  if (row->from_di) {
    interrupts(row->on_at_di);
    DI(saved);
  }
  interrupts(row->on_before_ei);
  const BOOL before = host_interrupts_enabled();
  EI(saved);
  const BOOL after = host_interrupts_enabled();

  UINT mismatches = 0;
  if (after != row->expected_on) {
    mismatches |= 1U << CHECK_ON;
  }
  if (before != row->on_before_ei ||
      (row->from_di && isDI(saved) == row->on_at_di)) {
    mismatches |= 1U << CHECK_SETUP;
  }
  return mismatches;
}

void test_ei_restores_the_state_di_saved_whatever_the_state_before_it(void) {
  UINT mask = 0;
  for (UINT row = 0; row < ROWS; ++row) {
    mask |= run_row(&s_rows[row]) << (row * CHECKS);
  }
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(
      0, mask,
      "mismatch bit = row * 2 + check; rows (saved by, on before EI): "
      "0 DI while off, off; 1 DI while off, on; 2 DI while on, off; "
      "3 DI while on, on; 4 EI(0), off; 5 EI(0), on; checks: 0 state after "
      "EI, 1 setup");
}

void test_nested_di_ei_restores_each_saved_state(void) {
  BOOL outer_di[2];
  BOOL inner_di[2];
  BOOL after_inner_ei[2];
  BOOL after_outer_ei[2];

  for (UINT row = 0; row < 2; ++row) {
    const BOOL on_at_outer_di = row != 0;
    interrupts(on_at_outer_di);

    UINT outer, inner;
    DI(outer);
    DI(inner);
    outer_di[row] = isDI(outer);
    inner_di[row] = isDI(inner);
    EI(inner);
    after_inner_ei[row] = host_interrupts_enabled();
    EI(outer);
    after_outer_ei[row] = host_interrupts_enabled();
  }

  interrupts(TRUE);
  TEST_ASSERT_TRUE_MESSAGE(outer_di[0], "DI while off did not save off");
  TEST_ASSERT_FALSE_MESSAGE(outer_di[1], "DI while on did not save on");
  TEST_ASSERT_TRUE(inner_di[0]);
  TEST_ASSERT_TRUE(inner_di[1]);
  TEST_ASSERT_FALSE_MESSAGE(after_inner_ei[0], "inner EI turned interrupts on");
  TEST_ASSERT_FALSE_MESSAGE(after_inner_ei[1], "inner EI turned interrupts on");
  TEST_ASSERT_FALSE(after_outer_ei[0]);
  TEST_ASSERT_TRUE(after_outer_ei[1]);
}

// A task that has put itself to sleep, with a switch requested, is switched
// out by the fake machine at the EI() that turns interrupts on. An EI() that
// turns them off must neither switch nor drop the request: the switch comes
// at the next EI() that turns them on.
void test_an_ei_that_turns_interrupts_off_keeps_the_switch_for_later(void) {
  // Saved here, while nothing is armed, so this EI() cannot switch.
  UINT outer, off;
  DI(outer);
  DI(off);
  EI(outer);

  host_set_current(host_make_task(PRIORITY));
  host_waited = 0;
  if (setjmp(host_wait_jmp) == 0) {
    host_arm_wait();
    tk_slp_tsk(TMO_FEVR);
  } else {
    host_waited = 1;
  }
  host_wait_armed = 0;
  const BOOL normal_wait_switched = host_waited;
  const BOOL normal_wait_enabled = host_interrupts_enabled();
  if (!normal_wait_switched || !normal_wait_enabled) {
    host_reset();
    TEST_ASSERT_TRUE_MESSAGE(normal_wait_switched,
                             "tk_slp_tsk did not take the normal switch");
    TEST_ASSERT_TRUE_MESSAGE(normal_wait_enabled,
                             "tk_slp_tsk did not restore interrupts");
  }

  // Arm a second wait; this request must survive EI(off) and switch at EI(0).
  host_arm_wait();
  out_w(MSIP, 1);

  // Each EI() installs its own jump target in this frame so a jump returns to
  // the matching restore.
  volatile BOOL switched = FALSE;
  if (setjmp(host_wait_jmp) == 0) {
    EI(off);
  } else {
    switched = TRUE;
  }
  const BOOL disabled_restore_did_not_switch = !switched;
  const BOOL request_survived_disabled_restore = in_w(MSIP) == 1;
  const BOOL disabled_after_restore = !host_interrupts_enabled();

  switched = FALSE;
  if (setjmp(host_wait_jmp) == 0) {
    EI(0);
  } else {
    switched = TRUE;
  }
  const BOOL enabled_restore_switched = switched;
  const BOOL enabled_after_restore = host_interrupts_enabled();
  const BOOL request_consumed_by_enabled_restore = in_w(MSIP) == 0;

  host_reset();
  TEST_ASSERT_TRUE_MESSAGE(disabled_restore_did_not_switch,
                           "EI() switched as it turned interrupts off");
  TEST_ASSERT_TRUE_MESSAGE(request_survived_disabled_restore,
                           "the switch request was lost");
  TEST_ASSERT_TRUE_MESSAGE(disabled_after_restore,
                           "EI() did not turn interrupts off");
  TEST_ASSERT_TRUE_MESSAGE(enabled_restore_switched,
                           "EI(0) did not take the switch");
  TEST_ASSERT_TRUE_MESSAGE(enabled_after_restore,
                           "EI(0) did not turn interrupts on");
  TEST_ASSERT_TRUE_MESSAGE(request_consumed_by_enabled_restore,
                           "EI(0) did not consume the switch request");
}
