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
