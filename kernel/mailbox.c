/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "mailbox.h"
#include "dispatch.h"
#include "list.h"
#include "task.h"
#include "timer.h"

#include <tk/tkernel.h>

_Static_assert(sizeof(tkmc_list_head) == sizeof(struct dummy_struct),
               "Size of dummy_struct must be equal to size of tkmc_list_head");

// Array of mailbox control blocks
MBXCB tkmc_mbxcbs[CFN_MAX_MBXID];
// Free list that holds mailbox control blocks that are not yet allocated
static tkmc_list_head tkmc_free_mbxcb;

// Bitmask used to mark control blocks as non-existent (i.e. not allocated)
#define NOEXS_MASK 0x80000000u

static tkmc_list_head *msg_link(T_MSG *msg) {
  return (tkmc_list_head *)&msg->list;
}

static T_MSG *msg_from_link(tkmc_list_head *link) {
  return tkmc_list_entry(link, T_MSG, list);
}

static void insert_msg_fifo(MBXCB *mbxcb, T_MSG *msg) {
  tkmc_list_add_tail(msg_link(msg), &mbxcb->mbx_queue);
}

static void insert_msg_pri(MBXCB *mbxcb, T_MSG_PRI *msg) {
  tkmc_list_head *new_link = msg_link(&msg->msgque);
  tkmc_list_head *pos = mbxcb->mbx_queue.next;

  while (pos != &mbxcb->mbx_queue) {
    T_MSG_PRI *queued = (T_MSG_PRI *)msg_from_link(pos);

    if (msg->msgpri < queued->msgpri) {
      tkmc_list_add(new_link, pos->prev);
      return;
    }
    pos = pos->next;
  }

  tkmc_list_add_tail(new_link, &mbxcb->mbx_queue);
}

static void insert_waiter(MBXCB *mbxcb, TCB *tcb) {
  tkmc_list_head *wait_link = &tcb->winfo.wait_queue;

  if ((mbxcb->mbxatr & TA_TPRI) != 0) {
    tkmc_list_head *pos = mbxcb->wait_queue.next;

    while (pos != &mbxcb->wait_queue) {
      TCB *queued = tkmc_list_entry(pos, TCB, winfo.wait_queue);

      if (tcb->itskpri < queued->itskpri) {
        tkmc_list_add(wait_link, pos->prev);
        return;
      }
      pos = pos->next;
    }
  }

  tkmc_list_add_tail(wait_link, &mbxcb->wait_queue);
}

static TCB *pop_waiting_receiver(MBXCB *mbxcb) {
  while (!tkmc_list_empty(&mbxcb->wait_queue)) {
    TCB *tcb = tkmc_list_first_entry(&mbxcb->wait_queue, TCB, winfo.wait_queue);

    tkmc_list_del(&tcb->winfo.wait_queue);
    tkmc_init_list_head(&tcb->winfo.wait_queue);

    if (tcb->tskstat == TTS_WAI && tcb->tskwait == TTW_MBX) {
      return tcb;
    }
  }

  return NULL;
}

static void make_waiter_ready(TCB *tcb, ER cause, T_MSG *msg) {
  if (tcb->delay_ticks > 0) {
    tkmc_list_del(&tcb->head);
    tkmc_init_list_head(&tcb->head);
    tcb->delay_ticks = 0;
  }

  tcb->winfo.mailbox.msg = msg;
  tcb->wupcause = cause;
  tcb->tskstat = TTS_RDY;
  tcb->tskwait = 0;
  tkmc_list_add_tail(&tcb->head, &tkmc_ready_queue[tcb->itskpri - 1]);
}

static void dispatch_if_needed(void) {
  if (current == NULL) {
    return;
  }

  next = tkmc_get_highest_priority_task();
  if (next != current) {
    dispatch();
  }
}

/**
 * @brief Initialize the mailbox control block (MBXCB) system.
 *
 * This function sets up the free list of mailbox control blocks (MBXCBs)
 * and initializes each control block with default values.
 *
 * @pre CFN_MAX_MBXID > 0
 * @post All MBXCBs are added to the free list and marked as non-existent.
 * @invariant The free list must always contain unallocated MBXCBs.
 */
void tkmc_init_mbxcb(void) {
  // Initialize the free list head.
  tkmc_init_list_head(&tkmc_free_mbxcb);

  // Iterate through all mailbox control blocks and initialize them.
  for (int i = 0; i < CFN_MAX_MBXID; ++i) {
    MBXCB *mbxcb = &tkmc_mbxcbs[i];
    *mbxcb = (MBXCB){
        // Set mbxid to (i+1) and mark as non-existent initially.
        .mbxid = (i + 1) | NOEXS_MASK,
        .exinf = NULL, // Extended information (user data), initially NULL.
        .mbxatr = 0,   // Mailbox attributes; will be set on creation.
    };

    // Initialize the wait queue list head for each mailbox.
    tkmc_init_list_head(&mbxcb->wait_queue);

    // Initialize the mailbox queue list head for each mailbox.
    tkmc_init_list_head(&mbxcb->mbx_queue);

    // Add the mailbox control block's wait queue to the free list.
    tkmc_list_add_tail(&mbxcb->wait_queue, &tkmc_free_mbxcb);
  }
}

/**
 * @brief Create a mailbox object.
 *
 * This function allocates a mailbox control block (MBXCB) from the free list
 * and initializes it with the specified attributes and extended information.
 *
 * @param[in] pk_cmbx Pointer to the mailbox creation parameter structure.
 * @return The ID of the created mailbox, or an error code if creation fails.
 *
 * @pre pk_cmbx != NULL
 * @pre The attribute `mbxatr` in `pk_cmbx` must only contain valid bits.
 * @post If successful, a new mailbox is created and removed from the free list.
 * @invariant The free list must always contain unallocated MBXCBs.
 */
ID tk_cre_mbx(CONST T_CMBX *pk_cmbx) {
  if (pk_cmbx == NULL) {
    return E_PAR;
  }

  const ATR mbxatr = pk_cmbx->mbxatr;
  static const ATR VALID_MBXATR =
      TA_TFIFO | TA_TPRI | TA_MFIFO | TA_MPRI | TA_DSNAME | TA_NODISWAI;

  // Check if attribute contains any invalid bits.
  // VALID_MBXATR defines the set of valid attribute bits for a mailbox.
  // Any bit in mbxatr that is not part of VALID_MBXATR is considered invalid.
  // This check ensures that only supported attributes are used, preventing
  // undefined behavior or misuse of the mailbox creation function.
  if ((mbxatr & ~VALID_MBXATR) != 0) {
    return E_RSATR; // Return error for invalid attributes.
  }

  ID new_mbxid = 0; // Initialize mailbox ID to 0.

  UINT intsts = 0; // Variable to store interrupt status.
  MBXCB *new_mbxcb = NULL;

  DI(intsts); // Disable interrupts to protect shared data.

  // Check if the free list is empty.
  if (tkmc_list_empty(&tkmc_free_mbxcb) == FALSE) {
    // Allocate a MBXCB from the free list.
    new_mbxcb = tkmc_list_first_entry(&tkmc_free_mbxcb, MBXCB, wait_queue);

    // Remove the allocated MBXCB from the free list.
    tkmc_list_del(&new_mbxcb->wait_queue);

    // Reinitialize the wait queue for the allocated MBXCB.
    tkmc_init_list_head(&new_mbxcb->wait_queue);
    tkmc_init_list_head(&new_mbxcb->mbx_queue);

    // Update mailbox ID to mark it as valid.
    new_mbxid = new_mbxcb->mbxid & ~NOEXS_MASK;

    // Initialize the allocated MBXCB with the provided parameters.
    new_mbxcb->mbxid = new_mbxid;
    new_mbxcb->exinf = pk_cmbx->exinf;
    new_mbxcb->mbxatr = pk_cmbx->mbxatr;
  } else {
    // No available MBXCBs in the free list.
    new_mbxid = (ID)E_LIMIT; // Return error for resource limit.
  }

  EI(intsts); // Restore interrupts.

  return new_mbxid; // Return the new mailbox ID or error code.
}

/**
 * @brief Delete a mailbox object.
 */
ER tk_del_mbx(ID mbxid) {
  if (mbxid <= 0 || mbxid > CFN_MAX_MBXID) {
    return E_ID;
  }

  MBXCB *mbxcb = &tkmc_mbxcbs[mbxid - 1];
  UINT intsts = 0;

  DI(intsts);

  if ((mbxcb->mbxid & NOEXS_MASK) != 0) {
    EI(intsts);
    return E_NOEXS;
  }

  TCB *tcb = NULL;
  while ((tcb = pop_waiting_receiver(mbxcb)) != NULL) {
    make_waiter_ready(tcb, E_DLT, NULL);
  }

  while (!tkmc_list_empty(&mbxcb->mbx_queue)) {
    tkmc_list_head *link = mbxcb->mbx_queue.next;
    tkmc_list_del(link);
    tkmc_init_list_head(link);
  }

  mbxcb->mbxid = mbxid | NOEXS_MASK;
  mbxcb->exinf = NULL;
  mbxcb->mbxatr = 0;
  tkmc_init_list_head(&mbxcb->wait_queue);
  tkmc_init_list_head(&mbxcb->mbx_queue);
  tkmc_list_add_tail(&mbxcb->wait_queue, &tkmc_free_mbxcb);

  dispatch_if_needed();

  EI(intsts);
  return E_OK;
}

/**
 * @brief Send a message to a mailbox.
 *
 * This function sends a message to the specified mailbox. If tasks are waiting
 * for messages in the mailbox, the message is delivered to the highest-priority
 * waiting task. Otherwise, the message is added to the mailbox's message queue.
 *
 * @param[in] mbxid Mailbox ID.
 * @param[in] pk_msg Pointer to the message to be sent.
 * @return E_OK on success, or an error code such as E_ID, E_PAR, or E_NOEXS.
 *
 * @pre mbxid > 0 && mbxid <= CFN_MAX_MBXID
 * @pre pk_msg != NULL
 * @post The message is either delivered to a waiting task or added to the
 *       mailbox's message queue.
 * @invariant The mailbox's message queue and wait queue must remain consistent.
 */
ER tk_snd_mbx(ID mbxid, T_MSG *pk_msg) {
  // Validate the mailbox ID.
  if (mbxid <= 0 || mbxid > CFN_MAX_MBXID) {
    return E_ID; // Invalid mailbox ID.
  }

  // Validate the message pointer.
  if (pk_msg == NULL) {
    return E_PAR; // Invalid parameter.
  }

  // Retrieve the mailbox control block.
  MBXCB *mbxcb = &tkmc_mbxcbs[mbxid - 1];

  UINT intsts = 0;
  DI(intsts); // Disable interrupts to protect shared data.

  // Check if the mailbox exists.
  if ((mbxcb->mbxid & NOEXS_MASK) != 0) {
    EI(intsts);
    return E_NOEXS; // Mailbox does not exist.
  }

  // Handle priority-based message validation for TA_MPRI.
  if (mbxcb->mbxatr & TA_MPRI) {
    T_MSG_PRI *msg_pri = (T_MSG_PRI *)pk_msg;
    if (msg_pri->msgpri <= 0) {
      EI(intsts);
      return E_PAR; // Invalid message priority.
    }
  }

  // Check if there are tasks waiting for messages in the mailbox.
  TCB *tcb = pop_waiting_receiver(mbxcb);
  if (tcb != NULL) {
    make_waiter_ready(tcb, E_OK, pk_msg);
    dispatch_if_needed();
  } else {
    // No tasks are waiting; add the message to the mailbox queue.
    if (mbxcb->mbxatr & TA_MPRI) {
      insert_msg_pri(mbxcb, (T_MSG_PRI *)pk_msg);
    } else {
      insert_msg_fifo(mbxcb, pk_msg);
    }
  }

  EI(intsts);  // Restore interrupts.
  return E_OK; // Success.
}

/**
 * @brief Receive a message from a mailbox.
 */
ER tk_rcv_mbx(ID mbxid, T_MSG **ppk_msg, TMO tmout) {
  if (mbxid <= 0 || mbxid > CFN_MAX_MBXID) {
    return E_ID;
  }
  if (ppk_msg == NULL || tmout < TMO_FEVR) {
    return E_PAR;
  }
  if (current == NULL) {
    return E_CTX;
  }

  MBXCB *mbxcb = &tkmc_mbxcbs[mbxid - 1];
  UINT intsts = 0;

  DI(intsts);

  if ((mbxcb->mbxid & NOEXS_MASK) != 0) {
    EI(intsts);
    return E_NOEXS;
  }

  if (!tkmc_list_empty(&mbxcb->mbx_queue)) {
    tkmc_list_head *link = mbxcb->mbx_queue.next;
    T_MSG *msg = msg_from_link(link);

    tkmc_list_del(link);
    tkmc_init_list_head(link);

    *ppk_msg = msg;
    EI(intsts);
    return E_OK;
  }

  if (tmout == TMO_POL) {
    EI(intsts);
    return E_TMOUT;
  }

  TCB *tcb = current;
  tcb->winfo.mailbox.msg = NULL;
  tkmc_init_list_head(&tcb->winfo.wait_queue);
  insert_waiter(mbxcb, tcb);

  if (tmout > 0) {
    tkmc_schedule_timer(tcb, ((tmout + 9) / 10) + 1, TTW_MBX);
  } else {
    tcb->tskstat = TTS_WAI;
    tcb->tskwait = TTW_MBX;
    tcb->delay_ticks = 0;
    tkmc_list_del(&tcb->head);
    tkmc_init_list_head(&tcb->head);
    next = tkmc_get_highest_priority_task();
    dispatch();
  }

  EI(intsts);

  DI(intsts);

  ER ercd = ((volatile TCB *)current)->wupcause;

  if (!tkmc_list_empty(&current->winfo.wait_queue)) {
    tkmc_list_del(&current->winfo.wait_queue);
    tkmc_init_list_head(&current->winfo.wait_queue);
  }

  if (ercd == E_OK) {
    *ppk_msg = current->winfo.mailbox.msg;
  }

  current->winfo.mailbox.msg = NULL;
  current->wupcause = E_OK;
  current->delay_ticks = 0;
  current->tskwait = 0;

  EI(intsts);

  return ercd;
}
// memo:
// dummy_struct is a structure needed for sizeof(T_MSG), and in the
// implementation, tkmc_list_head is actually used. However, exposing this
// structure externally would excessively expose the internal implementation and
// increase the header files. Therefore, I will use dummy_struct.
