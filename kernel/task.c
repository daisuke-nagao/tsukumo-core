/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "task.h"

TCB tkmc_tcbs[CFN_MAX_TSKID];
tkmc_list_head tkmc_free_tcb;
tkmc_list_head tkmc_ready_queue[CFN_MAX_PRI];
TCB *current = NULL;

void tkmc_init_tcb(void) {
  /* Initialize tkmc_free_tcb */
  tkmc_init_list_head(&tkmc_free_tcb);

  /* Initialize tkmc_tcbs */
  for (int i = 0; i < sizeof(tkmc_tcbs) / sizeof(tkmc_tcbs[0]); ++i) {
    TCB *tcb = &tkmc_tcbs[i];
    *tcb = (TCB){
        .tskid = i + 1,
        .itskpri = 0,
        .state = NON_EXISTENT,
        .sp = NULL,
        .task = NULL,
    };
    tkmc_init_list_head(&tcb->head);

    tkmc_list_add_tail(&tcb->head,
                       &tkmc_free_tcb); /* Append to tkmc_free_tcb */
  }

  /* Initialize tkmc_ready_queue */
  for (int i = 0; i < sizeof(tkmc_ready_queue) / sizeof(tkmc_ready_queue[0]);
       ++i) {
    tkmc_init_list_head(&tkmc_ready_queue[i]);
  }
}

ID tkmc_create_task(void *sp, SZ stksz, PRI itskpri, FP fp) {
  UW *stack_begin = (UW *)sp;
  UW *stack_end = stack_begin + (stksz >> 2);

  ID new_id = E_LIMIT;
  TCB *new_tcb = NULL;
  if (tkmc_list_empty(&tkmc_free_tcb) == FALSE) {
    new_tcb = tkmc_list_first_entry(&tkmc_free_tcb, TCB, head);
    tkmc_list_del(&new_tcb->head);

    new_id = new_tcb->tskid;
  } else {
    new_id = E_LIMIT;
  }

  if (new_id >= 0) {
    new_tcb->state = DORMANT;
    stack_end += -13;
    for (int i = 0; i < 12; ++i) {
      stack_end[i] = 0xdeadbeef;
    }
    stack_end[12] = (UW)fp;
    new_tcb->sp = stack_end;
    new_tcb->task = fp;
    new_tcb->itskpri = itskpri;
  } else {
    new_id = (ID)E_LIMIT;
  }

  return new_id;
}

ER tkmc_start_task(ID tskid) {
  TCB *tcb = tkmc_tcbs + tskid - 1;
  tcb->state = READY;

  PRI itskpri = tcb->itskpri;
  tkmc_list_add_tail(&tcb->head, &tkmc_ready_queue[itskpri - 1]);
  return E_OK;
}

TCB *tkmc_get_highest_tcb(void) {

  for (int i = 0; i < sizeof(tkmc_ready_queue) / sizeof(tkmc_ready_queue[0]);
       ++i) {
    if (!tkmc_list_empty(&tkmc_ready_queue[i])) {
      return tkmc_list_first_entry(&tkmc_ready_queue[i], TCB, head);
    }
  }

  return NULL;
}