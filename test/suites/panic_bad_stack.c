/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// An illegal instruction runs with a stack pointer that points at nothing, as
// after a stack overflow. The exception handler must not use that stack: it
// takes the kernel's own, and the kernel still reports the cause. A handler
// that kept the task's stack would fault again on its first push, which is the
// failure of the case that matters most.

#include <tk/tkernel.h>

#include "../panic_expect.h"
#include "../runner.h"

enum { ILLEGAL_INSTRUCTION = 2 };

#define ILLEGAL_WORD 0xffffffffu

// Between the boot ROM and the test finisher of the QEMU virt machine there is
// nothing.
#define UNMAPPED_ADDRESS 0x00010000u

static void illegal_instruction_with_a_bad_stack(void) {
  panic_expect.mcause = ILLEGAL_INSTRUCTION;
  panic_expect.mtval = ILLEGAL_WORD;
  // The trap never returns, so the stack pointer is not restored.
  __asm__ volatile("la t0, 1f\n"
                   "sw t0, %0\n"
                   "li sp, %1\n"
                   "1: .word %2\n"
                   : "=m"(panic_expect.mepc)
                   : "i"(UNMAPPED_ADDRESS), "i"(ILLEGAL_WORD)
                   : "t0");
}

static const test_case_t s_cases[] = {
    {"illegal_instruction_with_a_bad_stack",
     illegal_instruction_with_a_bad_stack},
};

TEST_SUITE_MAIN("panic-bad-stack", s_cases, 2000)
