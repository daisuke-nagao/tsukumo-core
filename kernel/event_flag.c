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

// Event flag control block table
FLGCB tkmc_flgcbs[CFN_MAX_FLGID];

// Free list of unused event flag control blocks
static tkmc_list_head tkmc_free_flbcb;

// Bitmask used to mark control blocks as non-existent
#define NOEXS_MASK 0x80000000u

/**
 * @brief Initialize the event flag control block table.
 *
 * Set up all FLGCBs as unallocated and add them to the free list.
 */
void tkmc_init_flgcb(void) {
  tkmc_init_list_head(&tkmc_free_flbcb);

  for (int i = 0; i < sizeof(tkmc_flgcbs) / sizeof(tkmc_flgcbs[0]); ++i) {
    FLGCB *flgcb = &tkmc_flgcbs[i];

    // Initialize all fields with default values and mark as non-existent
    *flgcb = (FLGCB){
        .flgid = (i + 1) | NOEXS_MASK,
        .exinf = NULL,
        .flgatr = 0,
        .flgptn = 0,
    };
    tkmc_init_list_head(&flgcb->wait_queue);

    // Add to free list
    tkmc_list_add_tail(&flgcb->wait_queue, &tkmc_free_flbcb);
  }
}

/**
 * @brief Create an event flag object.
 *
 * @param pk_cflg Pointer to the creation parameter structure
 * @return ID of the created flag, or error code
 */
ID tk_cre_flg(CONST T_CFLG *pk_cflg) {
  const ATR flgatr = pk_cflg->flgatr;
  static const ATR VALID_FLGATR = TA_TFIFO | TA_TPRI | TA_WSGL | TA_WMUL;

  // Check if attribute contains any invalid bits
  if ((flgatr & ~VALID_FLGATR) != 0) {
    return E_RSATR;
  }

  UINT intsts = 0;
  FLGCB *new_flgcb = NULL;
  ID new_flgid = 0;
  DI(intsts); // Disable interrupts to access shared structures

  if (tkmc_list_empty(&tkmc_free_flbcb) == FALSE) {
    // Allocate a FLGCB from the free list
    new_flgcb = tkmc_list_first_entry(&tkmc_free_flbcb, FLGCB, wait_queue);
    tkmc_list_del(&new_flgcb->wait_queue);
    tkmc_init_list_head(&new_flgcb->wait_queue);

    // Update flag ID to mark as valid
    new_flgid = new_flgcb->flgid & ~NOEXS_MASK;

    new_flgcb->flgid = new_flgid;
    new_flgcb->exinf = pk_cflg->exinf;
    new_flgcb->flgatr = pk_cflg->flgatr;
    new_flgcb->flgptn = pk_cflg->iflgptn;
  } else {
    // No available FLGCBs
    new_flgid = (ID)E_LIMIT;
  }

  EI(intsts); // Restore interrupts

  return new_flgid;
}

/**
 * @brief Check if the flag pattern matches the waiting pattern and mode.
 *
 * @param flgptn Current flag pattern
 * @param waiptn Waiting pattern
 * @param wfmode Waiting mode (AND/OR)
 * @return TRUE if condition is met
 */
static BOOL check_ptn(UINT flgptn, UINT waiptn, UINT wfmode) {
  BOOL cond;
  if ((wfmode & TWF_ANDW) != 0) {
    cond = (flgptn & waiptn) == waiptn;
  } else {
    cond = (flgptn & waiptn) != 0;
  }
  return cond ? TRUE : FALSE;
}

/**
 * @brief Wait for event flag.
 */
ER tk_wai_flg(ID flgid, UINT waiptn, UINT wfmode, UINT *p_flgptn, TMO tmout) {
  // Basic validation
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

  // Check existence
  if ((flgcb->flgid & NOEXS_MASK) != 0) {
    EI(intsts);
    return E_NOEXS;
  }

  UINT flgptn = flgcb->flgptn;

  // Immediate condition check
  if (check_ptn(flgptn, waiptn, wfmode) != FALSE) {
    *p_flgptn = flgptn;

    // Clear pattern if requested
    if ((wfmode & TWF_CLR) != 0) {
      flgcb->flgptn = 0;
    } else if ((wfmode & TWF_BITCLR) != 0) {
      flgcb->flgptn &= ~waiptn;
    }

    EI(intsts);
    return E_OK;
  }

  // No wait requested (polling)
  if (tmout == TMO_POL) {
    EI(intsts);
    return E_TMOUT;
  }

  TCB *tcb = current;

  // Check for single-wait restriction
  if ((flgcb->flgatr & TA_WMUL) == 0 && !tkmc_list_empty(&flgcb->wait_queue)) {
    EI(intsts);
    return E_OBJ;
  }

  // Setup wait parameters
  tcb->winfo.event_flag.waiptn = waiptn;
  tcb->winfo.event_flag.wfmode = wfmode;
  tcb->winfo.event_flag.flgptn = 0;

  tkmc_init_list_head(&tcb->winfo.wait_queue); // For safety

  // Insert into wait queue based on priority or FIFO
  if (flgcb->flgatr & TA_TPRI) {
    if (tkmc_list_empty(&flgcb->wait_queue) != FALSE) {
      tkmc_list_add_tail(&tcb->winfo.wait_queue, &flgcb->wait_queue);
    } else {
      BOOL inserted = FALSE;
      TCB *pos;
      tkmc_list_for_each_entry(pos, &flgcb->wait_queue, winfo.wait_queue) {
        if (tcb->itskpri < pos->itskpri) {
          tkmc_list_add(&tcb->winfo.wait_queue, pos->winfo.wait_queue.prev);
          inserted = TRUE;
          break;
        }
      }
      if (inserted == FALSE) {
        tkmc_list_add_tail(&tcb->winfo.wait_queue, &flgcb->wait_queue);
      }
    }
  } else {
    tkmc_list_add_tail(&tcb->winfo.wait_queue, &flgcb->wait_queue);
  }

  if (tmout > 0) {
    // Start timer if timeout is specified
    tkmc_schedule_timer(tcb, ((tmout + 9) / 10) + 1, TTW_FLG);
  } else {
    // Infinite wait
    tcb->tskstat = TTS_WAI;
    tcb->tskwait = TTW_FLG;
    tcb->delay_ticks = 0;
    tkmc_list_del(&tcb->head);
    tkmc_init_list_head(&tcb->head);
    next = tkmc_get_highest_priority_task();
    dispatch(); // Perform task switch
  }

  EI(intsts);

  // Wait here until resumed by event or timeout
  DI(intsts);
  ercd = ((volatile TCB *)current)->wupcause;
  if (ercd == E_OK) {
    *p_flgptn = current->winfo.event_flag.flgptn;
  }
  current->wupcause = E_OK;
  EI(intsts);

  return ercd;
}

/**
 * @brief Set bits in the event flag and wake up waiting tasks if matched.
 */
ER tk_set_flg(ID flgid, UINT setptn) {
  if (flgid > CFN_MAX_FLGID) {
    return E_ID;
  }

  FLGCB *flgcb = &tkmc_flgcbs[flgid - 1];
  UINT intsts = 0;
  DI(intsts);

  if ((flgcb->flgid & NOEXS_MASK) != 0) {
    EI(intsts);
    return E_NOEXS;
  }

  // Update flag bits
  flgcb->flgptn |= setptn;

  if (!tkmc_list_empty(&flgcb->wait_queue)) {
    TCB *tcb, *n;

    // Check each waiting task and wake up if condition met
    tkmc_list_for_each_entry_safe(tcb, n, &flgcb->wait_queue,
                                  winfo.wait_queue) {
      if (check_ptn(flgcb->flgptn, tcb->winfo.event_flag.waiptn,
                    tcb->winfo.event_flag.wfmode)) {
        tkmc_list_del(&tcb->winfo.wait_queue);
        tkmc_init_list_head(&tcb->winfo.wait_queue);

        // Remove from timer queue if registered
        if (tcb->delay_ticks > 0) {
          tkmc_list_del(&tcb->head);
          tcb->delay_ticks = 0;
        }

        tcb->winfo.event_flag.flgptn = flgcb->flgptn;
        tcb->wupcause = E_OK;

        // Optional clearing of flags
        if (tcb->winfo.event_flag.wfmode & TWF_CLR) {
          flgcb->flgptn = 0;
        } else if (tcb->winfo.event_flag.wfmode & TWF_BITCLR) {
          flgcb->flgptn &= ~(tcb->winfo.event_flag.waiptn);
        }

        // Move task to ready state
        tcb->tskstat = TTS_RDY;
        tcb->tskwait = 0;
        tkmc_list_add_tail(&tcb->head, &tkmc_ready_queue[tcb->itskpri - 1]);

        // If only one task should be woken (TA_WSGL), stop here
        if ((flgcb->flgatr & TA_WSGL) != 0) {
          break;
        }
      }
    }

    // Trigger context switch if a higher-priority task became ready
    next = tkmc_get_highest_priority_task();
    if (next != current) {
      dispatch();
    }
  }

  EI(intsts);

  return E_OK;
}

/**
 * @brief Clear specific bits in the event flag.
 */
ER tk_clr_flg(ID flgid, UINT clrptn) {
  if (flgid <= 0 || flgid > CFN_MAX_FLGID) {
    return E_ID;
  }

  FLGCB *flgcb = &tkmc_flgcbs[flgid - 1];
  UINT intsts = 0;
  DI(intsts);

  if ((flgcb->flgid & NOEXS_MASK) != 0) {
    EI(intsts);
    return E_NOEXS;
  }

  // Clear specified bits from the flag pattern
  flgcb->flgptn &= ~clrptn;

  EI(intsts);
  return E_OK;
}
