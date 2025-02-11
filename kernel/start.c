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

extern ID tkmc_create_task(const T_CTSK *pk_ctsk);
extern ER tkmc_start_task(ID tskid, INT stacd);
extern TCB *tkmc_get_highest_priority_task(void);

void tkmc_start(int a0, int a1) {
  clear_bss();

  tkmc_init_tcb();

  T_CTSK pk_ctsk1 = {
      .task = task1,
      .itskpri = 1,
      .stksz = sizeof(task1_stack),
      .bufptr = task1_stack,
  };

  T_CTSK pk_ctsk2 = {
      .task = task2,
      .itskpri = 1,
      .stksz = sizeof(task2_stack),
      .bufptr = task2_stack,
  };
  ID task1_id = tkmc_create_task(&pk_ctsk1);
  ID task2_id = tkmc_create_task(&pk_ctsk2);

  tkmc_start_task(task1_id, 1);
  tkmc_start_task(task2_id, 2);

  TCB *tcb = tkmc_get_highest_priority_task();
  tcb->state = RUNNING;
  current = tcb;

  __launch_task(&tcb->sp);

  return;
}

static void clear_bss(void) {
  extern int *_bss_start;
  extern int *_bss_end;
  for (int *ptr = _bss_start; ptr < _bss_end; ++ptr) {
    *ptr = 0;
  }
}

void **schedule(void *sp) {

  TCB *tmp = current;
  tmp->sp = sp;

  TCB *next = tkmc_get_highest_priority_task();

  current = next;

  return &next->sp;
}