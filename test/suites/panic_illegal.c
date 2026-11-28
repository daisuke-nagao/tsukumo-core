/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// A task executes an illegal instruction. The kernel must stop and report the
// cause (mcause 2), the address of the instruction and the instruction word.
// Before it did, the exception handler returned to the same instruction for
// ever. The verdict comes from the replaced tkmc_panic() in test/panic_hook.c.

#include <tk/tkernel.h>

#include "panic_expect.h"
#include "runner.h"

static void panic_illegal_instruction_reports_cause_and_address(void) {
  panic_expect.mcause = ILLEGAL_INSTRUCTION;
  panic_expect.mtval = ILLEGAL_WORD;
  // The address of the illegal word is the expected mepc, stored just before.
  __asm__ volatile("la t0, 1f\n"
                   "sw t0, %0\n"
                   "1: .word %1\n"
                   : "=m"(panic_expect.mepc)
                   : "i"(ILLEGAL_WORD)
                   : "t0");
}

static const test_case_t s_cases[] = {
    CASE(panic_illegal_instruction_reports_cause_and_address),
};

TEST_SUITE_MAIN("panic-illegal", s_cases, 2000)
