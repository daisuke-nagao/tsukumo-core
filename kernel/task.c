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
  tkmc_free_tcb.next = tkmc_free_tcb.prev = &tkmc_free_tcb;

  for (int i = 0; i < sizeof(tkmc_tcbs) / sizeof(tkmc_tcbs[0]); ++i) {
    TCB *tcb = &tkmc_tcbs[i];
    *tcb = (TCB){
        .next = tcb,
        .prev = tcb,
        .tskid = i + 1,
        .state = NON_EXISTENT,
        .sp = NULL,
        .task = NULL,
    };

    tkmc_free_tcb.prev->next = tcb;
    tcb->prev = tkmc_free_tcb.prev;
    tcb->next = &tkmc_free_tcb;
    tkmc_free_tcb.prev = tcb;
  }
}
