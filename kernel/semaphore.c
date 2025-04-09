/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "semaphore.h"

#include "dispatch.h"
#include "task.h"
#include "timer.h"

// Array of semaphore control blocks
SEMCB tkmc_semcbs[CFN_MAX_SEMID];
// Free list that holds semaphore control blocks that are not yet allocated
static tkmc_list_head tkmc_free_semcb;

// Bitmask used to mark control blocks as non-existent (i.e. not allocated)
#define NOEXS_MASK 0x80000000u

/**
 * @brief Initializes the semaphore control block table.
 *
 * This function sets up all semaphore control blocks (SEMCB) by:
 * - Initializing each SEMCB with a unique semid marked as non-existent
 *   (using NOEXS_MASK).
 * - Initializing the wait queue for each SEMCB.
 * - Adding each SEMCB to the free list for later allocation.
 */
void tkmc_init_semcb(void) {
  tkmc_init_list_head(&tkmc_free_semcb);

  for (int i = 0; i < CFN_MAX_SEMID; ++i) {
    SEMCB *semcb = &tkmc_semcbs[i];
    *semcb = (SEMCB){
        // Set semid to (i+1) and mark as non-existent initially.
        .semid = (i + 1) | NOEXS_MASK,
        .exinf = NULL, // Extended information (user data), initially NULL.
        .sematr = 0,   // Semaphore attributes; will be set on creation.
        .semcnt = 0,   // Current resource count, initially 0.
        .maxsem = 0,   // Maximum resource count, initially 0.
    };
    // Initialize the wait queue list head for each semaphore.
    tkmc_init_list_head(&semcb->wait_queue);
    // Add the semaphore control block's wait queue to the free list.
    tkmc_list_add_tail(&semcb->wait_queue, &tkmc_free_semcb);
  }
}

/**
 * @brief Creates a semaphore object.
 *
 * This function allocates a semaphore control block (SEMCB) from the free list,
 * validates the given semaphore attributes, and initializes the SEMCB with the
 * provided creation parameters. The semaphore is marked as allocated by
 * removing the NOEXS_MASK from its semid.
 *
 * @param pk_csem Pointer to a creation parameter structure containing:
 *                - sematr  : Semaphore attribute flags.
 *                - isemcnt : Initial resource count.
 *                - maxsem  : Maximum allowable resource count.
 *                - exinf   : Extended user information.
 * @return Semaphore identifier on success, or an error code such as E_RSATR or
 * E_LIMIT.
 */
ID tk_cre_sem(CONST T_CSEM *pk_csem) {
  const ATR sematr = pk_csem->sematr;

  // Define valid attribute mask (combination of supported attributes)
  static const ATR VALID_SEMATR = TA_TFIFO | TA_TPRI | TA_CNT | TA_FIRST;

  // Check for invalid attribute bits
  if ((sematr & ~VALID_SEMATR) != 0) {
    return E_RSATR; // Return error if invalid attribute bits are set.
  }

  UINT intsts = 0;
  SEMCB *new_semcb = NULL;
  ID new_semid = 0;

  DI(intsts); // Disable interrupts to protect shared data.

  if (!tkmc_list_empty(&tkmc_free_semcb)) {
    // Allocate a new SEMCB from the free list.
    new_semcb = tkmc_list_first_entry(&tkmc_free_semcb, SEMCB, wait_queue);
    tkmc_list_del(&new_semcb->wait_queue);
    tkmc_init_list_head(&new_semcb->wait_queue);

    // Remove the NOEXS_MASK to mark the semaphore as allocated.
    new_semid = new_semcb->semid & ~NOEXS_MASK;
    new_semcb->semid = new_semid;

    // Initialize semaphore control block with parameters provided.
    new_semcb->exinf = pk_csem->exinf;
    new_semcb->sematr = pk_csem->sematr;
    new_semcb->semcnt = pk_csem->isemcnt;
    new_semcb->maxsem = pk_csem->maxsem;
  } else {
    new_semid = (ID)E_LIMIT; // No available SEMCB found.
  }

  EI(intsts); // Restore interrupts.

  return new_semid;
}

/**
 * @brief Wait (pend) on a semaphore.
 *
 * This function causes the current task to wait for the specified number of
 * semaphore resources. It performs parameter validation, checks for immediate
 * availability, and if necessary, enqueues the task in the semaphore's wait
 * queue. For polling (non-blocking) requests, it returns immediately if
 * resources are insufficient. When resources become available or on timeout,
 * the task is resumed.
 *
 * @param semid Semaphore identifier.
 * @param cnt Number of resources requested.
 * @param tmout Timeout period (TMO_POL for polling; TMO_FEVR for infinite wait
 * or >0 for finite timeout).
 * @return E_OK upon success, or an error code like E_ID, E_PAR, E_TMOUT, or
 * E_NOEXS.
 */
ER tk_wai_sem(ID semid, INT cnt, TMO tmout) {
  // Validate parameters: semid must be within valid range, cnt positive,
  // timeout valid.
  if (semid <= 0 || semid > CFN_MAX_SEMID) {
    return E_ID;
  }
  if (cnt <= 0 || tmout < TMO_FEVR) {
    return E_PAR;
  }

  // Retrieve semaphore control block corresponding to semid.
  SEMCB *semcb = &tkmc_semcbs[semid - 1];

  UINT intsts = 0;
  DI(intsts);

  // Check if the semaphore object exists by verifying NOEXS_MASK.
  if ((semcb->semid & NOEXS_MASK) != 0) {
    EI(intsts);
    return E_NOEXS;
  }

  // Immediate check: if sufficient resources are available.
  if (semcb->semcnt >= cnt) {
    semcb->semcnt -= cnt;
    EI(intsts);
    return E_OK;
  }

  // If polling is requested (non-blocking), return timeout immediately.
  if (tmout == TMO_POL) {
    EI(intsts);
    return E_TMOUT;
  }

  TCB *tcb = current;

  // Configure the waiting parameters for the task.
  tcb->winfo.semaphore.semcnt = cnt;

  // Reset the task's wait queue link for safety.
  tkmc_init_list_head(&tcb->winfo.wait_queue); // safety

  // Insert the task into the semaphore's wait queue.
  // Use priority-based insertion if the semaphore attribute TA_TPRI is set;
  // otherwise, use FIFO.
  if (semcb->sematr & TA_TPRI) {
    if (tkmc_list_empty(&semcb->wait_queue) != FALSE) {
      // Queue is empty: simply add to the tail.
      tkmc_list_add_tail(&tcb->winfo.wait_queue, &semcb->wait_queue);
    } else {
      BOOL inserted = FALSE;
      TCB *pos, *n;
      // Traverse the wait queue safely to find the correct insertion point.
      tkmc_list_for_each_entry_safe(pos, n, &semcb->wait_queue,
                                    winfo.wait_queue) {
        // Insert before the first task with a lower priority.
        if (tcb->itskpri < pos->itskpri) {
          tkmc_list_add(&tcb->winfo.wait_queue, pos->winfo.wait_queue.prev);
          inserted = TRUE;
          break;
        }
      }
      // If no higher-priority task is found, add to tail.
      if (inserted == FALSE) {
        tkmc_list_add_tail(&tcb->winfo.wait_queue, &semcb->wait_queue);
      }
    }
  } else {
    // FIFO insertion if priority scheduling is not required.
    tkmc_list_add_tail(&tcb->winfo.wait_queue, &semcb->wait_queue);
  }

  if (tmout > 0) {
    // For a finite timeout, schedule a timer that will trigger wait
    // termination.
    tkmc_schedule_timer(tcb, ((tmout + 9) / 10) + 1, TTW_SEM);
  } else {
    // For infinite wait: update task state and perform a context switch.
    tcb->tskstat = TTS_WAI;
    tcb->tskwait = TTW_SEM;
    tcb->delay_ticks = 0;
    // Remove task from its current ready queue list.
    tkmc_list_del(&tcb->head);
    tkmc_init_list_head(&tcb->head);
    // Determine the next highest priority task to run.
    next = tkmc_get_highest_priority_task();
    dispatch(); // Switch context to the selected task.
  }

  EI(intsts);

  // After the wait, re-disable interrupts and fetch the wakeup cause.
  DI(intsts);
  ER ercd = ((volatile TCB *)current)->wupcause;
  // Reset wakeup cause for future wait operations.
  current->wupcause = E_OK;
  EI(intsts);

  return ercd;
}

/**
 * @brief Signals a semaphore to release resources.
 *
 * This function releases (returns) a given number of resources back to the
 * semaphore. It first validates the input and ensures that releasing the
 * requested count does not exceed the semaphore's maximum limit. Then, it
 * iterates over the wait queue, checking if any waiting tasks can have their
 * resource requests satisfied with the newly available resources. Each task
 * whose request is met is:
 *   - Removed from the wait queue.
 *   - Removed from the timer queue if a timeout was pending.
 *   - Marked as ready (wakeup cause set to E_OK) and added to the ready queue.
 *   - Its resource requirement is subtracted from the semaphore count.
 * Finally, if a higher-priority task is ready, a dispatch is requested.
 *
 * @param semid Semaphore identifier.
 * @param cnt Number of resources to release.
 * @return E_OK on success, or an error code such as E_ID, E_PAR, E_NOEXS, or
 * E_QOVR.
 */
ER tk_sig_sem(ID semid, INT cnt) {
  // Validate input parameters.
  if (semid <= 0 || semid > CFN_MAX_SEMID) {
    return E_ID;
  }
  if (cnt <= 0) {
    return E_PAR;
  }

  ER ercd = E_OK;
  SEMCB *semcb = &tkmc_semcbs[semid - 1];

  UINT intsts = 0;
  DI(intsts);

  // Confirm that the semaphore object exists (i.e. is allocated).
  if ((semcb->semid & NOEXS_MASK) != 0) {
    EI(intsts);
    return E_NOEXS;
  }

  // Check that releasing cnt resources does not exceed the maximum resource
  // limit.
  if (semcb->maxsem >= cnt && semcb->semcnt > semcb->maxsem - cnt) {
    EI(intsts);
    return E_QOVR; // Overflow error
  }

  // Increase the semaphore's resource count.
  semcb->semcnt += cnt;

  TCB *pos, *n;
  // Traverse the wait queue safely; tasks are examined in FIFO order.
  tkmc_list_for_each_entry_safe(pos, n, &semcb->wait_queue, winfo.wait_queue) {
    INT req = pos->winfo.semaphore.semcnt;
    // If resources are insufficient for the waiting task...
    if (semcb->semcnt < req) {
      // If the semaphore attribute TA_CNT is not set, break out of the loop.
      if ((semcb->sematr & TA_CNT) == 0) {
        break;
      } else {
        // Otherwise, continue checking the next waiting task.
        continue;
      }
    }

    TCB *tcb = pos;
    // Remove the task from the wait queue as its request is satisfied.
    tkmc_list_del(&tcb->winfo.wait_queue);
    tkmc_init_list_head(&tcb->winfo.wait_queue);
    // If the task has a pending timer, remove it from the timer queue.
    if (tcb->delay_ticks > 0) {
      tkmc_list_del(&tcb->head);
      tcb->delay_ticks = 0;
    }

    // Wake up the task by setting its wakeup cause and marking it ready.
    tcb->wupcause = E_OK;
    tcb->tskstat = TTS_RDY;
    tcb->tskwait = 0;
    // Add the task to the ready queue corresponding to its priority.
    tkmc_list_add_tail(&tcb->head, &tkmc_ready_queue[tcb->itskpri - 1]);

    // Subtract the assigned resources from the semaphore count.
    semcb->semcnt -= req;

    // If resources have been depleted, exit the loop.
    if (semcb->semcnt <= 0) {
      break;
    }
  }

  // Check if a higher-priority task is now ready for execution.
  next = tkmc_get_highest_priority_task();
  if (next != current) {
    dispatch(); // Request a context switch if necessary.
  }
  EI(intsts);

  return ercd;
}

/**
 * @brief Deletes a semaphore object.
 * 
 * This function deallocates a semaphore control block (SEMCB) and marks it as
 * non-existent. It clears the semaphore attributes and counts, and adds the
 * SEMCB back to the free list for future allocation. The function also
 * validates the semaphore ID and checks if the semaphore object exists before
 * proceeding with the deletion.
 */
ER tk_del_sem(ID semid) {
  // Validate the semaphore ID.
  if (semid <= 0 || semid > CFN_MAX_SEMID) {
    return E_ID;
  }

  SEMCB *semcb = &tkmc_semcbs[semid - 1];
  UINT intsts = 0;
  DI(intsts);

  // Check if the semaphore object exists (i.e. is allocated).
  if ((semcb->semid & NOEXS_MASK) != 0) {
    EI(intsts);
    return E_NOEXS;
  }

  // Mark the semaphore as non-existent by setting the NOEXS_MASK.
  semcb->semid |= NOEXS_MASK;

  // Clear the semaphore attributes and counts.
  semcb->exinf = NULL;
  semcb->sematr = 0;
  semcb->semcnt = 0;
  semcb->maxsem = 0;

  // Add the semaphore control block back to the free list.
  tkmc_list_add_tail(&semcb->wait_queue, &tkmc_free_semcb);

  EI(intsts);
  return E_OK;
}