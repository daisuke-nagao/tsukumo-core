/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "task.h"

TCB tkmc_tcbs[CFN_MAX_TSKID];
tkmc_list_head tkmc_free_tcb;
TCB *current = NULL;

void tkmc_init_tcb(void) {
  tkmc_init_list_head(&tkmc_free_tcb);

  for (int i = 0; i < sizeof(tkmc_tcbs) / sizeof(tkmc_tcbs[0]); ++i) {
    TCB *tcb = &tkmc_tcbs[i];
    *tcb = (TCB){
        .tskid = i + 1,
        .state = NON_EXISTENT,
        .sp = NULL,
        .task = NULL,
    };
    tkmc_init_list_head(&tcb->head);

    tkmc_list_add_tail(&tcb->head, &tkmc_free_tcb);
  }
}

ID tkmc_create_task(void *sp, SZ stksz, FP fp) {
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
  } else {
    new_id = (ID)E_LIMIT;
  }

  return new_id;
}
