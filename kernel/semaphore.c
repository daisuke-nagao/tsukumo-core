/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "semaphore.h"

SEMCB tkmc_semcbs[CFN_MAX_SEMID];
static tkmc_list_head tkmc_free_semcb;

// Bitmask used to mark control blocks as non-existent
#define NOEXS_MASK 0x80000000u

void tkmc_init_semcb(void) {
  tkmc_init_list_head(&tkmc_free_semcb);

  for (int i = 0; i < CFN_MAX_SEMID; ++i) {
    SEMCB *semcb = &tkmc_semcbs[i];
    *semcb = (SEMCB){
        .semid = (i + 1) | NOEXS_MASK,
        .exinf = NULL,
        .sematr = 0,
        .semcnt = 0,
        .maxsem = 0,
    };
    tkmc_init_list_head(&semcb->wait_queue);
    tkmc_list_add_tail(&semcb->wait_queue, &tkmc_free_semcb);
  }
}

ID tk_cre_sem(CONST T_CSEM *pk_csem) {
  const ATR sematr = pk_csem->sematr;

  // Define valid attribute mask (combination of supported attributes)
  static const ATR VALID_SEMATR = TA_TFIFO | TA_TPRI | TA_CNT | TA_FIRST;

  // Check for invalid attribute bits
  if ((sematr & ~VALID_SEMATR) != 0) {
    return E_RSATR;
  }

  UINT intsts = 0;
  SEMCB *new_semcb = NULL;
  ID new_semid = 0;

  DI(intsts); // Disable interrupts

  if (!tkmc_list_empty(&tkmc_free_semcb)) {
    new_semcb = tkmc_list_first_entry(&tkmc_free_semcb, SEMCB, wait_queue);
    tkmc_list_del(&new_semcb->wait_queue);
    tkmc_init_list_head(&new_semcb->wait_queue);

    new_semid = new_semcb->semid & ~NOEXS_MASK;
    new_semcb->semid = new_semid;
    new_semcb->exinf = pk_csem->exinf;
    new_semcb->sematr = pk_csem->sematr;
    new_semcb->semcnt = pk_csem->isemcnt;
    new_semcb->maxsem = pk_csem->maxsem;
  } else {
    new_semid = (ID)E_LIMIT;
  }

  EI(intsts); // Enable interrupts

  return new_semid;
}
