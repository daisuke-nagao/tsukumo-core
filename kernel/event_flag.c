/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>

#include "event_flag.h"

FLGCB tkmc_flbcbs[CFN_MAX_FLGID];
static tkmc_list_head tkmc_free_flbcb;

void tkmc_init_flgcb(void) {
  tkmc_init_list_head(&tkmc_free_flbcb);

  for (int i = 0; i < sizeof(tkmc_flbcbs) / sizeof(tkmc_flbcbs[0]); ++i) {
    FLGCB *flgcb = &tkmc_flbcbs[i];
    *flgcb = (FLGCB){
        .flgid = i + 1,
        .exinf = NULL,
        .flgatr = 0,
        .flgptn = 0,
    };
    tkmc_init_list_head(&flgcb->wait_queue);

    tkmc_list_add_tail(&flgcb->wait_queue, &tkmc_free_flbcb);
  }
}

ID tk_cre_flg(CONST T_CFLG *pk_cflg) { return E_LIMIT; }
