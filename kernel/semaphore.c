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
