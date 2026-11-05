/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01A19136_FEE9_7893_9BDD_1562393FACCE
#define UUID_01A19136_FEE9_7893_9BDD_1562393FACCE

#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// How many trap entries the CPU is inside: the RV32 timer, external-interrupt
// and dispatch trap entries add 1 on entry and take it off before the task
// resumes. It is above 1 only in an external interrupt that a handler with
// interrupts on lets a source of a higher priority interrupt (see
// docs/kernel/interrupts.md). Internal to the kernel: callers ask
// tkmc_in_isr().
// Single hart only.
extern volatile UINT tkmc_isr_depth;

static inline BOOL tkmc_in_isr(void) { return tkmc_isr_depth != 0; }

// intsts is the interrupt state saved on API entry, before DI().
static inline BOOL tkmc_can_block(UINT intsts) {
  return !isDI(intsts) && current != NULL && !tkmc_in_isr();
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A19136_FEE9_7893_9BDD_1562393FACCE */
