/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01955F79_CF88_76A9_B43B_7899A0391E87
#define UUID_01955F79_CF88_76A9_B43B_7899A0391E87

#include <tk/tkernel.h>

#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern void tkmc_start_timer(void);
extern void tkmc_init_timer(void);
extern void tkmc_schedule_timer(TCB *tcb, UINT delay_ticks, UINT tskwait);

/**
 * @brief Convert a time limit in milliseconds to the ticks a wait lasts.
 *
 * @param ms Time limit, greater than 0.
 * @return ms in 10 ms ticks, rounded up, plus one tick: the first tick comes at
 *         any time after the wait begins.
 */
static inline UINT tkmc_ms_to_ticks(TMO ms) {
  // Not (ms + 9) / 10: the + 9 overflows TMO for ms above INT_MAX - 9.
  return (UINT)ms / 10 + ((UINT)ms % 10 != 0) + 1;
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01955F79_CF88_76A9_B43B_7899A0391E87 */
