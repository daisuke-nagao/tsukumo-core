/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01A17C76_39E8_7853_AA4A_9D23FC61FDFF
#define UUID_01A17C76_39E8_7853_AA4A_9D23FC61FDFF

#include <tk/tkernel.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/// The trap CSRs as the kernel read them when a synchronous exception (illegal
/// instruction, access fault, ...) stopped the system.
typedef struct {
  UW mcause;
  UW mepc;
  UW mtval;
} tkmc_trap_record;

/// The last fatal trap. Not static, so a debugger or a test can read it after
/// the system has stopped.
extern volatile tkmc_trap_record tkmc_last_trap;

/// Entered from the trap vector with the CSRs in the arguments. Records them
/// in tkmc_last_trap and calls tkmc_panic(). Never returns.
extern __attribute__((noreturn)) void tkmc_fatal_trap(UW mcause, UW mepc,
                                                      UW mtval);

/// Stops the system after a fatal trap. The default definition (weak) halts
/// with a wfi loop. A board or a test replaces it to report the cause; the
/// kernel itself has no console to report on. Never returns. A replacement must
/// not fault: a second trap re-enters the handler and overwrites
/// tkmc_last_trap, so the first cause is lost.
extern __attribute__((noreturn)) void tkmc_panic(UW mcause, UW mepc, UW mtval);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A17C76_39E8_7853_AA4A_9D23FC61FDFF */
