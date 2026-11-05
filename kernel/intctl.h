/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01A1AACA_9754_7402_8F40_3FAEF30C2CC4
#define UUID_01A1AACA_9754_7402_8F40_3FAEF30C2CC4

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// Brings the interrupt controller to a known state: no handler defined, every
// source off and with priority 0, and the threshold at 0. Called once at boot,
// after .bss is cleared and before interrupts are enabled.
extern void tkmc_init_intctl(void);

// Serves one external interrupt: claims the source, calls its handler and
// completes it. Called by the trap entry of the machine external interrupt, in
// the ISR state with interrupts disabled. The handler may turn interrupts on:
// the PLIC threshold is at the priority of its source while it runs, so only a
// source of a higher priority interrupts it. It must turn them off again
// before it returns.
extern void tkmc_external_interrupt(void);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A1AACA_9754_7402_8F40_3FAEF30C2CC4 */
