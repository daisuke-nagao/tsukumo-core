/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>

#include "dispatch.h"
#include "event_flag.h"
#include "task.h"
#include "timer.h"

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
    EI(intsts);
    return E_NOEXS;
  }

  UINT flgptn = flgcb->flgptn;

  if (check_ptn(flgptn, waiptn, wfmode) != FALSE) {
    *p_flgptn = flgptn;
    if ((wfmode & TWF_CLR) != 0) {
      flgcb->flgptn = 0;
    } else if ((wfmode & TWF_BITCLR) != 0) {
      flgcb->flgptn &= ~waiptn;
    }
    EI(intsts);
    return E_OK;
  }

  if (tmout == TMO_POL) {
    EI(intsts);
    return E_TMOUT;
  }

  // === Timeout specified or infinite wait ===
  TCB *tcb = current;

  // WSGL allows only one task to wait; if already waiting, return error
  if ((flgcb->flgatr & TA_WMUL) == 0) {
    if (!tkmc_list_empty(&flgcb->wait_queue)) {
      EI(intsts);
      return E_OBJ;
    }
  }

  // Set wait information
  tcb->winfo.waiptn = waiptn;
  tcb->winfo.wfmode = wfmode;
  tcb->winfo.flgptn = 0;

  tkmc_init_list_head(&tcb->winfo.wait_queue); // Initialize for safety
  tkmc_list_add_tail(&tcb->winfo.wait_queue, &flgcb->wait_queue);

  if (tmout > 0) {
    tkmc_schedule_timer(tcb, ((tmout + 9) / 10) + 1,
                        TTW_FLG); // Convert to ticks
  } else {
    // Infinite wait
    tcb->tskstat = TTS_WAI;
    tcb->tskwait = TTW_FLG;
    tcb->delay_ticks = 0;
    tkmc_list_del(&tcb->head);
    tkmc_init_list_head(&tcb->head);
    next = tkmc_get_highest_priority_task();
    dispatch();
  }

  EI(intsts);

  // Block until the wait is released (resumed by an interrupt)
  DI(intsts);
  ercd = ((volatile TCB *)current)->wupcause;
  if (ercd == E_OK) {
    *p_flgptn = current->winfo.flgptn;
  }
  current->wupcause = E_OK;
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
    EI(intsts);
    return E_NOEXS;
  }

  // Set the pattern bits
  flgcb->flgptn |= setptn;

  if (!tkmc_list_empty(&flgcb->wait_queue)) {
    TCB *tcb, *n;
    tkmc_list_for_each_entry_safe(tcb, n, &flgcb->wait_queue,
                                  winfo.wait_queue) {
      if (check_ptn(flgcb->flgptn, tcb->winfo.waiptn, tcb->winfo.wfmode)) {
        tkmc_list_del(&tcb->winfo.wait_queue);

        // Remove from timer queue if present
        if (tcb->delay_ticks > 0) {
          tkmc_list_del(&tcb->head); // Remove from timer queue
          tcb->delay_ticks = 0;
        }

        // Set result
        tcb->winfo.flgptn = flgcb->flgptn;
        tcb->wupcause = E_OK;

        // Clear pattern
        if (tcb->winfo.wfmode & TWF_CLR) {
          flgcb->flgptn = 0;
        } else if (tcb->winfo.wfmode & TWF_BITCLR) {
          flgcb->flgptn &= ~(tcb->winfo.waiptn);
        }

        // State transition
        tcb->tskstat = TTS_RDY;
        tcb->tskwait = 0;
        tkmc_list_add_tail(&tcb->head, &tkmc_ready_queue[tcb->itskpri - 1]);

        if ((flgcb->flgatr & TA_WSGL) != 0) {
          break;
        }
      }
    }

    // Context switch if a higher-priority task is ready
    next = tkmc_get_highest_priority_task();
    if (next != current) {
      dispatch();
    }
  }

  EI(intsts);

  return E_OK;
}
