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
  TTS_NOEXS = 0x0000,
  TTS_RUN = 0x0001,
  TTS_RDY = 0x0002,
  TTS_WAI = 0x0004,
  TTS_SUS = 0x0008,
  TTS_WAS = 0x000c,
  TTS_DMT = 0x0010,
  TTS_NODISWAI = 0x0080,
};

/* Task Control Block */
typedef struct TCB {
  tkmc_list_head head;
  ID tskid;
  PRI itskpri;
  UINT state;
  void *sp;
  void *initial_sp;
  FP task;
  void *exinf;
  UINT delay_ticks;
  ER wupcause;
} TCB;

extern TCB *current;

extern void tkmc_init_tcb(void);

extern void __launch_task(void **sp_end);
extern TCB *tkmc_get_highest_priority_task(void);
extern void tkmc_yield(void);

extern TCB *current;
extern TCB *next;
extern tkmc_list_head tkmc_ready_queue[CFN_MAX_PRI];
extern TCB *current;
extern TCB *next;
extern TCB tkmc_tcbs[CFN_MAX_TSKID];

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01948452_C50C_7B25_A131_C2733EF5E067 */
