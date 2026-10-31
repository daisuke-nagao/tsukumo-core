/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "panic.h"

volatile tkmc_trap_record tkmc_last_trap;
volatile tkmc_startup_record tkmc_last_startup_failure;
volatile tkmc_context_record tkmc_last_context_failure;

// The record is written here, not in tkmc_panic(), so that it exists even when
// a board or a test replaces the hook, and a test can check what the kernel
// recorded.
void tkmc_fatal_trap(UW mcause, UW mepc, UW mtval) {
  tkmc_last_trap.mcause = mcause;
  tkmc_last_trap.mepc = mepc;
  tkmc_last_trap.mtval = mtval;
  tkmc_panic(mcause, mepc, mtval);
}

// Weak so that a board can report the cause where it has a console. The kernel
// does not print it itself: the UART address belongs to the board, and the
// kernel should not depend on it.
__attribute__((weak)) void tkmc_panic(UW mcause, UW mepc, UW mtval) {
  (void)mcause;
  (void)mepc;
  (void)mtval;
  for (;;) {
    __asm__ volatile("wfi");
  }
}

void tkmc_fatal_startup(tkmc_startup_stage stage, ER error) {
  UINT intsts;
  DI(intsts);
  tkmc_last_startup_failure.stage = stage;
  tkmc_last_startup_failure.error = error;
  tkmc_startup_panic(stage, error);
}

__attribute__((weak)) void tkmc_startup_panic(tkmc_startup_stage stage,
                                              ER error) {
  (void)stage;
  (void)error;
  for (;;) {
    __asm__ volatile("wfi");
  }
}

void tkmc_fatal_context(tkmc_context_api api) {
  UINT intsts;
  DI(intsts);
  tkmc_last_context_failure.api = api;
  tkmc_last_context_failure.error = E_CTX;
  tkmc_context_panic(api);
}

__attribute__((weak)) void tkmc_context_panic(tkmc_context_api api) {
  (void)api;
  for (;;) {
    __asm__ volatile("wfi");
  }
}
