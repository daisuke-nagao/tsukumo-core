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

// Set by the RV32 timer/dispatch trap entries, cleared before task execution.
// Single hart, single-level interrupts only; this is not nesting support.
extern volatile UINT tkmc_isr_active;

static inline BOOL tkmc_in_isr(void) { return tkmc_isr_active != 0; }

// intsts is the interrupt state saved on API entry, before DI().
static inline BOOL tkmc_can_block(UINT intsts) {
  return !isDI(intsts) && current != NULL && !tkmc_in_isr();
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A19136_FEE9_7893_9BDD_1562393FACCE */
