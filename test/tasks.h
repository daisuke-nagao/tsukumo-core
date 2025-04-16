/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01963230_562D_7651_A760_462C7A1BE55F
#define UUID_01963230_562D_7651_A760_462C7A1BE55F

#include <tk/tkernel.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum TASK_INDEX {
  TASK1 = 0, // Index for task1
  TASK2,     // Index for task2
  TASK3,     // Index for task3
  TASK4,     // Index for task4
  TASK_NBOF, // Total number of tasks
};

extern ID get_tskid(unsigned int index);
extern ER set_tskid(unsigned int index, ID tskid);

extern void task1(INT stacd, void *exinf);
extern void task2(INT stacd, void *exinf);
extern void task3(INT stacd, void *exinf);
extern void task4(INT stacd, void *exinf);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01963230_562D_7651_A760_462C7A1BE55F */
