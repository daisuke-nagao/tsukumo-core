/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01A1AACA_9754_7402_8F40_3FAEF30C2CC4
#define UUID_01A1AACA_9754_7402_8F40_3FAEF30C2CC4

#include <tk/asm/rv32/interrupt.h>
#include <tk/tkernel.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct tkmc_interrupt_context {
  struct tkmc_interrupt_context *previous;
  void *frame;
  void *entry_sp;
  FP handler;
  UINT depth;
  UINT intno;
  UW saved_threshold;
  UINT kind;
} tkmc_interrupt_context;

extern tkmc_interrupt_context *volatile tkmc_active_interrupt_context;

// The trap entry allocates ctx on the ISR stack; it remains live through
// matching finish/return processing.
extern void tkmc_prepare_external_interrupt(tkmc_interrupt_context *ctx,
                                            void *frame);
extern void tkmc_finish_external_interrupt(tkmc_interrupt_context *ctx);
extern __attribute__((__noreturn__)) void tkmc_ret_int_from_sp(void *entry_sp);
extern __attribute__((__noreturn__)) void
tkmc_restore_external_interrupt_frame(void *frame, UW saved_mie_bits);
extern __attribute__((__noreturn__)) void tkmc_invalid_ret_int(void);

// Brings the interrupt controller to a known state: no handler defined, every
// source off and with priority 0, and the threshold at 0. Called once at boot,
// after .bss is cleared and before interrupts are enabled.
extern void tkmc_init_intctl(void);

// Compatibility C entry used by host tests/wrappers. The RV32 trap entry uses
// prepare/finish directly so TA_ASM handlers are entered without a live C
// dispatcher frame; encountering TA_ASM here is a fatal context error.
extern void tkmc_external_interrupt(void);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A1AACA_9754_7402_8F40_3FAEF30C2CC4 */
