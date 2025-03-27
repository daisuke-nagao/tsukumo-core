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

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01955F79_CF88_76A9_B43B_7899A0391E87 */
