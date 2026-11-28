/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01A17C76_39F3_76F0_B79A_7533876764DC
#define UUID_01A17C76_39F3_76F0_B79A_7533876764DC

#include <tk/tkernel.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/// What the trap a case causes must report. The case fills it in just before
/// it traps; panic_hook.c checks it when the kernel stops, and prints "PANIC
/// OK" only if the kernel reported exactly this.
typedef struct {
  UW mcause;
  UW mepc;
  UW mtval;
} panic_expect_t;

extern volatile panic_expect_t panic_expect;

/// The mcause values of the exceptions the panic suites cause.
enum { ILLEGAL_INSTRUCTION = 2, LOAD_ACCESS_FAULT = 5 };

/// All ones: no instruction has this encoding. A #define, as the cases use
/// it as an "i" operand of inline assembly.
#define ILLEGAL_WORD 0xffffffffu

/// Between the boot ROM (0x1000) and the test finisher (0x100000) of the QEMU
/// virt machine there is nothing.
#define UNMAPPED_ADDRESS 0x00010000u

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A17C76_39F3_76F0_B79A_7533876764DC */
