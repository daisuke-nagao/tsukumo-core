/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_0195A180_E38C_74B5_9314_CA66B708899A
#define UUID_0195A180_E38C_74B5_9314_CA66B708899A

#include "asm/rv32/address.h"
#include <tk/tkernel.h>

#define tkmc_compiler_barrier() asm volatile("" ::: "memory")

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// Defined in start.c and called to switch tasks on the way out of a trap (by
// the trap entries in start.S, and by tkmc_ret_int_from_sp() in intctl.c, where
// tk_ret_int() goes); returns the frame to resume. The QEMU suites that wrap
// it declare their wrappers with __typeof__(schedule), so that a wrapper of
// another type fails to compile: void * and void ** come back the same way on
// RV32, so no test run would show the difference.
extern void *schedule(void *sp);

static inline void dispatch(void) {
  tkmc_compiler_barrier();
  // Trigger a machine software interrupt
  out_w((CLINT_BASE_ADDRESS + CLINT_MSIP_OFFSET), 1);
  tkmc_compiler_barrier();
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_0195A180_E38C_74B5_9314_CA66B708899A */
