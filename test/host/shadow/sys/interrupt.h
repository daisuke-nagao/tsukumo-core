/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Stands in for include/sys/interrupt.h in the PC unit tests. The real header
// reads and writes the RISC-V mstatus CSR, which a PC cannot run. The kernel
// finds this one first because test/host/shadow comes before include/ on the
// include path, so the kernel sources are compiled unchanged.

#ifndef UUID_01A17C3E_90A9_7333_8320_BB6CE3F8888B
#define UUID_01A17C3E_90A9_7333_8320_BB6CE3F8888B

#include <tk/typedef.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define MSTATUS_MIE ((UINT)(1 << 3))

// The fake interrupt state lives in test/host/fakes.c. host_di() returns the
// previous state and disables interrupts; host_ei() restores it, and is also
// where a blocking kernel call "waits": on the target the task is switched out
// at that point.
extern UINT host_di(void);
extern void host_ei(UINT intsts);

#define DI(intsts)                                                             \
  do {                                                                         \
    intsts = host_di();                                                        \
  } while (0)

#define EI(intsts) host_ei(intsts)

static inline BOOL isDI(UINT intsts) {
  return (intsts & MSTATUS_MIE) == 0 ? TRUE : FALSE;
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A17C3E_90A9_7333_8320_BB6CE3F8888B */
