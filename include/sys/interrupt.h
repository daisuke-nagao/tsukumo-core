/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_019542DF_8F10_70F0_AE6B_848A71C04DB1
#define UUID_019542DF_8F10_70F0_AE6B_848A71C04DB1

#include <tk/typedef.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define MSTATUS_MIE ((UINT)(1 << 3))

// intsts is a UINT variable of the caller. The CSR is read straight into it:
// a local of the macro's own would be shadowed by a caller's variable of the
// same name, which would then never be assigned. The bits other than MIE are
// set so that intsts is never 0, which EI() takes as the "enable" of boot.
#define DI(intsts)                                                             \
  do {                                                                         \
    asm volatile("csrrci %0, mstatus, %1"                                      \
                 : "=r"(intsts)                                                \
                 : "i"(MSTATUS_MIE)                                            \
                 : "memory");                                                  \
    intsts |= ~MSTATUS_MIE;                                                    \
  } while (0)

#define EI(intsts)                                                             \
  do {                                                                         \
    if (intsts == 0) {                                                         \
      asm volatile("csrsi mstatus, %0" ::"i"(MSTATUS_MIE) : "memory");         \
    } else {                                                                   \
      asm volatile("csrs mstatus, %0" ::"r"(intsts & MSTATUS_MIE) : "memory"); \
    }                                                                          \
  } while (0)

static inline BOOL isDI(UINT intsts) {
  return (intsts & MSTATUS_MIE) == 0 ? TRUE : FALSE;
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_019542DF_8F10_70F0_AE6B_848A71C04DB1 */
