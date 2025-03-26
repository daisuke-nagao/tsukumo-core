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

static BOOL check_ptn(UINT flgptn, UINT waiptn, UINT wfmode) {
  BOOL cond;
  if ((wfmode & TWF_ANDW) != 0) {
    cond = (flgptn & waiptn) == waiptn;
  } else {
    cond = (flgptn & waiptn) != 0;
  }
  return cond ? TRUE : FALSE;
}

ER tk_wai_flg(ID flgid, UINT waiptn, UINT wfmode, UINT *p_flgptn, TMO tmout) {
  if (flgid > CFN_MAX_FLGID) {
    return E_ID;
  }
  if (waiptn == 0) {
    return E_PAR;
  }
  static const UINT VALID_WFMODE = TWF_ANDW | TWF_ORW | TWF_CLR | TWF_BITCLR;
  if ((wfmode & ~VALID_WFMODE) != 0) {
    return E_PAR;
  }
  if (tmout < TMO_FEVR) {
    return E_PAR;
  }

  ER ercd = E_OK;
  FLGCB *flgcb = &tkmc_flgcbs[flgid - 1];

  UINT intsts = 0;
  DI(intsts);
  if ((flgcb->flgid & NOEXS_MASK) != 0) {
    ercd = E_NOEXS;
  }

  if (ercd == E_OK) {
    UINT flgptn = flgcb->flgptn;

    if (check_ptn(flgptn, waiptn, wfmode) != FALSE) {
      *p_flgptn = flgptn;
      if ((wfmode & TWF_CLR) != 0) {
        flgcb->flgptn = 0;
      } else if ((wfmode & TWF_BITCLR) != 0) {
        flgcb->flgptn &= ~waiptn;
      }
      ercd = E_OK;
    } else {
      if (tmout == TMO_POL) {
        ercd = E_TMOUT;
      } else if (tmout > 0) {
        ercd = E_TMOUT; // temporary
      } else if (tmout == TMO_FEVR) {
        ercd = E_TMOUT; // temporary
      } else {
        ercd = E_TMOUT; // temporary
      }
    }
  }
  EI(intsts);

  return ercd;
}

ER tk_set_flg(ID flgid, UINT setptn) {
  if (flgid > CFN_MAX_FLGID) {
    return E_ID;
  }
  
  ER ercd = E_OK;
  FLGCB *flgcb = &tkmc_flgcbs[flgid - 1];

  UINT intsts = 0;
  DI(intsts);
  if ((flgcb->flgid & NOEXS_MASK) != 0) {
    ercd = E_NOEXS;
  }

  if (ercd == E_OK) {
    flgcb->flgptn |= setptn;
  }

  EI(intsts);

  return E_OK;
}