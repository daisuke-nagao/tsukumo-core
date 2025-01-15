/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <typedef.h>

static void clear_bss(void);

extern void task1(void);
extern void task2(void);

static UW task1_stack[1024];
static UW task2_stack[1024];
extern void tkmc_launch_task(unsigned int *sp_end, void (*f)(void));

/* Task Control Block */
typedef struct TCB {
  void *sp;
  FP task;
} TCB;

static TCB tcbs[2] = {
    {NULL, NULL},
    {NULL, NULL},
};

static ID tkmc_create_task(void *sp, SZ stksz, FP fp) {
  UW *stack_begin = (UW *)sp;
  UW *stack_end = stack_begin + (stksz >> 2);

  ID new_id = sizeof(tcbs) / sizeof(tcbs[0]);
  for (unsigned int i = 0; i < sizeof(tcbs) / sizeof(tcbs[0]); ++i) {
    if (tcbs[i].sp == NULL) {
      new_id = i;
      break;
    }
  }

  if (new_id < sizeof(tcbs) / sizeof(tcbs[0])) {
    TCB *new_tcb = tcbs + new_id;
    new_tcb->sp = stack_end;
    new_tcb->task = fp;
  }

  return new_id;
}

static ER tkmc_start_task(ID id) {
  TCB *tcb = tcbs + id;
  tkmc_launch_task(tcb->sp, tcb->task);
  return 0;
}

void tkmc_start(int a0, int a1) {
  clear_bss();
  ID task1_id = tkmc_create_task(task1_stack, sizeof(task1_stack), (FP)task1);
  ID task2_id = tkmc_create_task(task2_stack, sizeof(task2_stack), (FP)task2);
  tkmc_start_task(task1_id);
  return;
}

static void clear_bss(void) {
  extern int *_bss_start;
  extern int *_bss_end;
  for (int *ptr = _bss_start; ptr < _bss_end; ++ptr) {
    *ptr = 0;
  }
}
