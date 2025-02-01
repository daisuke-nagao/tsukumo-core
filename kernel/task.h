/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01948452_C50C_7B25_A131_C2733EF5E067
#define UUID_01948452_C50C_7B25_A131_C2733EF5E067

#include <tk/tkernel.h>

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum TaskState {
  NON_EXISTENT = 0,
  DORMANT,
  READY,
  RUNNING,
};

/* Task Control Block */
typedef struct TCB {
  tkmc_list_head head;
  ID tskid;
  enum TaskState state;
  void *sp;
  FP task;
} TCB;

extern TCB tkmc_tcbs[CFN_MAX_TSKID];
extern TCB tkmc_free_tcb;
extern TCB *current;

extern void tkmc_init_tcb(void);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01948452_C50C_7B25_A131_C2733EF5E067 */