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

typedef enum {
  TKMC_STARTUP_NONE = 0,
  TKMC_STARTUP_INITIAL_CREATE,
  TKMC_STARTUP_INITIAL_START,
  TKMC_STARTUP_IDLE_CREATE,
  TKMC_STARTUP_IDLE_START
} tkmc_startup_stage;

typedef struct {
  tkmc_startup_stage stage;
  ER error;
} tkmc_startup_record;

/// The last startup API failure, separate from CPU trap CSRs. BSS clearing
/// leaves stage == TKMC_STARTUP_NONE until a failure is recorded.
extern volatile tkmc_startup_record tkmc_last_startup_failure;

/// Disables interrupts, records the failed stage and API result, then calls
/// tkmc_startup_panic(). Does not undo partial startup or retry. Never returns.
extern __attribute__((noreturn)) void
tkmc_fatal_startup(tkmc_startup_stage stage, ER error);

/// Startup failure hook. The weak default stops in a wfi loop. A board may
/// replace it to report the record. Entered with interrupts disabled, before
/// scheduling starts: do not enable interrupts, call task APIs or return.
extern __attribute__((noreturn)) void
tkmc_startup_panic(tkmc_startup_stage stage, ER error);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A17C76_39E8_7853_AA4A_9D23FC61FDFF */
