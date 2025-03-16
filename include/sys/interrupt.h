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

#define DI(intsts)                                                             \
  do {                                                                         \
    UINT status = 0;                                                           \
    asm volatile("csrrci %0, mstatus, %1"                                      \
                 : "=r"(status)                                                \
                 : "i"(MSTATUS_MIE)                                            \
                 : "memory");                                                  \
    intsts = status | ~MSTATUS_MIE;                                            \
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
