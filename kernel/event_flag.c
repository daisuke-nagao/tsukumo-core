/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>

#include "event_flag.h"

FLGCB tkmc_flgcbs[CFN_MAX_FLGID];
static tkmc_list_head tkmc_free_flbcb;

#define NOEXS_MASK 0x80000000u

void tkmc_init_flgcb(void) {
  tkmc_init_list_head(&tkmc_free_flbcb);

  for (int i = 0; i < sizeof(tkmc_flgcbs) / sizeof(tkmc_flgcbs[0]); ++i) {
    FLGCB *flgcb = &tkmc_flgcbs[i];
    *flgcb = (FLGCB){
        .flgid = (i + 1) | NOEXS_MASK,
        .exinf = NULL,
        .flgatr = 0,
        .flgptn = 0,
    };
    tkmc_init_list_head(&flgcb->wait_queue);

    tkmc_list_add_tail(&flgcb->wait_queue, &tkmc_free_flbcb);
  }
}

ID tk_cre_flg(CONST T_CFLG *pk_cflg) {
  const ATR flgatr = pk_cflg->flgatr;
  static const ATR VALID_FLGATR = TA_TFIFO | TA_TPRI | TA_WSGL | TA_WMUL;

  if ((flgatr & ~VALID_FLGATR) != 0) {
    return E_RSATR;
  }

  if (flgatr & TA_TPRI || flgatr & TA_WMUL) {
    return E_NOMEM; // temporary
  }

  UINT intsts = 0;
  FLGCB *new_flgcb = NULL;
  ID new_flgid = 0;
  DI(intsts);

  if (tkmc_list_empty(&tkmc_free_flbcb) == FALSE) {
    /* Allocate a TCB from the free list */
    new_flgcb = tkmc_list_first_entry(&tkmc_free_flbcb, FLGCB, wait_queue);
    tkmc_list_del(&new_flgcb->wait_queue);
    tkmc_init_list_head(&new_flgcb->wait_queue);
    new_flgid = new_flgcb->flgid & ~NOEXS_MASK;

    new_flgcb->flgid = new_flgid;
    new_flgcb->exinf = pk_cflg->exinf;
    new_flgcb->flgatr = pk_cflg->flgatr;
    new_flgcb->flgptn = pk_cflg->iflgptn;
  } else {
    new_flgid = (ID)E_LIMIT;
  }

  EI(intsts);

  return new_flgid;
}

ER tk_wai_flg(ID flgid, UINT waiptn, UINT wfmode, UINT *p_flgptn, TMO tmout) {
  return E_TMOUT;
}
