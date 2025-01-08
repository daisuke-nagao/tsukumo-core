/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <typedef.h>

static void clear_bss(void);

extern void task1(void);

static unsigned int task1_stack[1024];
extern void tkmc_launch_task(unsigned int *sp_end, void (*f)(void));

/* Task Control Block */
typedef struct TCB {
  void *sp;
} TCB;

static TCB tcbs[1] = {
    {NULL},
};

static ID tkmc_create_task(void *sp, SZ stksz, FP fp) {
  B *stack_begin = (B *)sp;
  B *stack_end = stack_begin + stksz;

  ID new_id = sizeof(tcbs) / sizeof(tcbs[0]);
  for (unsigned int i = 0; i < sizeof(tcbs) / sizeof(tcbs[0]); ++i) {
    if (tcbs[i].sp != NULL) {
      new_id = i;
      break;
    }
  }

  if (new_id < sizeof(tcbs) / sizeof(tcbs[0])) {
    TCB *new_tcb = tcbs + new_id;
    new_tcb->sp = stack_end;
  }

  return new_id;
}

void tkmc_start(int a0, int a1) {
  clear_bss();
  tkmc_launch_task(task1_stack + sizeof(task1_stack) / sizeof(task1_stack[0]),
                   task1);
  return;
}

static void clear_bss(void) {
  extern int *_bss_start;
  extern int *_bss_end;
  for (int *ptr = _bss_start; ptr < _bss_end; ++ptr) {
    *ptr = 0;
  }
}
