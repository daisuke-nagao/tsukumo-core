/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "panic.h"

volatile tkmc_trap_record tkmc_last_trap;

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
