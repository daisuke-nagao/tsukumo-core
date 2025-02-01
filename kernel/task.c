/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "task.h"

TCB tkmc_tcbs[CFN_MAX_TSKID];
TCB tkmc_free_tcb;
TCB *current = NULL;

void tkmc_init_tcb(void) {
  tkmc_init_list_head(&tkmc_free_tcb.head);

  for (int i = 0; i < sizeof(tkmc_tcbs) / sizeof(tkmc_tcbs[0]); ++i) {
    TCB *tcb = &tkmc_tcbs[i];
    *tcb = (TCB){
        .tskid = i + 1,
        .state = NON_EXISTENT,
        .sp = NULL,
        .task = NULL,
    };
    tkmc_init_list_head(&tcb->head);

    tkmc_free_tcb.head.prev->next = &tcb->head;
    tcb->head.prev = tkmc_free_tcb.head.prev;
    tcb->head.next = &tkmc_free_tcb.head;
    tkmc_free_tcb.head.prev = &tcb->head;
  }
}
