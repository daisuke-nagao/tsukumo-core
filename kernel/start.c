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

static ID tkmc_create_task(void *sp, SZ stksz, FP fp) {
  UW *stack_begin = (UW *)sp;
  UW *stack_end = stack_begin + (stksz >> 2);

  ID new_id = sizeof(tkmc_tcbs) / sizeof(tkmc_tcbs[0]);
  for (unsigned int i = 0; i < sizeof(tkmc_tcbs) / sizeof(tkmc_tcbs[0]); ++i) {
    if (tkmc_tcbs[i].sp == NULL) {
      new_id = i;
      break;
    }
  }

  if (new_id < sizeof(tkmc_tcbs) / sizeof(tkmc_tcbs[0])) {
    TCB *new_tcb = tkmc_tcbs + new_id;
    new_tcb->tskid = new_id;
    new_tcb->state = DORMANT;
    stack_end += -13;
    for (int i = 0; i < 12; ++i) {
      stack_end[i] = 0xdeadbeef;
    }
    stack_end[12] = (UW)fp;
    new_tcb->sp = stack_end;
    new_tcb->task = fp;
  } else {
    new_id = (ID)E_LIMIT;
  }

  return new_id;
}

static ER tkmc_start_task(ID tskid) {
  TCB *tcb = tkmc_tcbs + tskid;
  tcb->state = READY;
  return E_OK;
}

static TCB *current = NULL;

void tkmc_start(int a0, int a1) {
  clear_bss();

  tkmc_init_tcb();

  ID task1_id = tkmc_create_task(task1_stack, sizeof(task1_stack), (FP)task1);
  ID task2_id = tkmc_create_task(task2_stack, sizeof(task2_stack), (FP)task2);

  tkmc_start_task(task1_id);
  tkmc_start_task(task2_id);

  TCB *tcb1 = tkmc_tcbs + task1_id;
  tcb1->state = RUNNING;
  current = tcb1;

  __launch_task(&tcb1->sp);

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
