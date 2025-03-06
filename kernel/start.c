/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>

#include "ini_tsk.h"
#include "task.h"
#include "timer.h"

static void clear_bss(void);

static UINT ini_tsk_stack[128] __attribute__((aligned(16)));

void tkmc_start(int a0, int a1) {
  clear_bss();

  tkmc_init_tcb();

  T_CTSK pk_ctsk = {
      .exinf = NULL,
      .tskatr = TA_USERBUF,
      .task = (FP)tkmc_ini_tsk,
      .itskpri = 1,
      .stksz = sizeof(ini_tsk_stack),
      .bufptr = ini_tsk_stack,
  };

  ID ini_tsk_id = tk_cre_tsk(&pk_ctsk);
  tk_sta_tsk(ini_tsk_id, a0);

  TCB *tcb = tkmc_get_highest_priority_task();
  tcb->state = RUNNING;
  current = tcb;

  tkmc_start_timer();
  EI(0);

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

  if (current->state == RUNNING) {
    current->state = READY;
  }
  next->state = RUNNING;

  current = next;

  return &current->sp;
}