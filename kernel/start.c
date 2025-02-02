/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>

#include "task.h"

static void clear_bss(void);

extern void task1(void);
extern void task2(void);

static UW task1_stack[1024];
static UW task2_stack[1024];

extern void __launch_task(void **sp_end);
extern void __context_switch(void **next_sp, void **current_sp);

extern ID tkmc_create_task(void *sp, SZ stksz, PRI itskpri, FP fp);
extern ER tkmc_start_task(ID tskid);
extern TCB *tkmc_get_highest_tcb(void);

void tkmc_start(int a0, int a1) {
  clear_bss();

  tkmc_init_tcb();

  ID task1_id =
      tkmc_create_task(task1_stack, sizeof(task1_stack), 1, (FP)task1);
  ID task2_id =
      tkmc_create_task(task2_stack, sizeof(task2_stack), 1, (FP)task2);

  tkmc_start_task(task1_id);
  tkmc_start_task(task2_id);

  TCB *tcb = tkmc_get_highest_tcb();
  tcb->state = RUNNING;
  current = tcb;

  __launch_task(&tcb->sp);

  return;
}

void tkmc_context_switch(ID tskid) {
  TCB *prev = current;
  prev->state = READY;
  TCB *next = tkmc_tcbs + tskid;
  next->state = RUNNING;
  current = next;
  __context_switch(&next->sp, &prev->sp);
}

static void clear_bss(void) {
  extern int *_bss_start;
  extern int *_bss_end;
  for (int *ptr = _bss_start; ptr < _bss_end; ++ptr) {
    *ptr = 0;
  }
}
