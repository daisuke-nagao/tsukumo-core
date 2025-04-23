/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "mailbox.h"
#include "list.h"
#include "task.h"

#include <tk/tkernel.h>

_Static_assert(sizeof(tkmc_list_head) == sizeof(struct dummy_struct),
               "Size of dummy_struct must be equal to size of tkmc_list_head");

// Array of mailbox control blocks
MBXCB tkmc_mbxcbs[CFN_MAX_MBXID];
// Free list that holds mailbox control blocks that are not yet allocated
static tkmc_list_head tkmc_free_mbxcb;

// Bitmask used to mark control blocks as non-existent (i.e. not allocated)
#define NOEXS_MASK 0x80000000u

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

  // Check if the free list is empty.
  if (tkmc_list_empty(&tkmc_free_mbxcb) == FALSE) {
    // Allocate a MBXCB from the free list.
    new_mbxcb = tkmc_list_first_entry(&tkmc_free_mbxcb, MBXCB, wait_queue);

    // Remove the allocated MBXCB from the free list.
    tkmc_list_del(&new_mbxcb->wait_queue);

    // Reinitialize the wait queue for the allocated MBXCB.
    tkmc_init_list_head(&new_mbxcb->wait_queue);

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

ER tk_snd_mbx(ID mbxid, T_MSG *pk_msg) {
  // Check if the mailbox ID is valid.
  // The mailbox ID must be greater than 0 and less than or equal to
  // CFN_MAX_MBXID.
  if (mbxid <= 0 || mbxid > CFN_MAX_MBXID) {
    return E_ID; // Return error for invalid ID.
  }

  // Get the mailbox control block (MBXCB) for the specified mailbox ID.
  MBXCB *mbxcb = &tkmc_mbxcbs[mbxid - 1];

  // Check if the mailbox exists and is not marked as non-existent.
  // A mailbox is considered non-existent if its mbxid has the NOEXS_MASK bit
  // set.
  if ((mbxcb->mbxid & NOEXS_MASK) != 0) {
    return E_NOEXS; // Return error for non-existent mailbox.
  }

  // Check if the message pointer is NULL.
  // A NULL message pointer is considered an invalid parameter.
  if (pk_msg == NULL) {
    return E_PAR; // Return error for invalid parameter.
  }

  // If the mailbox has the TA_MPRI attribute, validate the message priority.
  // The message priority must be greater than 0 for priority-based mailboxes.
  if (mbxcb->mbxatr & TA_MPRI) {
    T_MSG_PRI *msg_pri = (T_MSG_PRI *)pk_msg;
    if (msg_pri->msgpri <= 0) {
      return E_PAR; // Return error for invalid parameters.
    }
  }

  // Check if there are any tasks waiting for messages in the mailbox.
  if (tkmc_list_empty(&mbxcb->wait_queue) == FALSE) {
    // There are tasks waiting for messages in the mailbox.

    // Remove the first task from the wait queue.
    TCB *wait_tcb = tkmc_list_first_entry(&mbxcb->wait_queue, TCB, head);
    tkmc_list_del(&wait_tcb->head); // Remove the task from the wait queue.
    tkmc_init_list_head(&wait_tcb->head); // Reinitialize the task's list head.

    // Set the return value of the waiting task to the message pointer.
    //! @todo add msg to wait_tcb->winfo.msg
    // wait_tcb->winfo.msg = pk_msg;

    //! @todo Unblock the waiting task and set its state to ready.
  } else {
    // No tasks are waiting for messages in the mailbox.

    // Add the message to the mailbox queue.
    // The message is added to the tail of the mailbox's message queue.
    tkmc_list_head *list = (tkmc_list_head *)&pk_msg->list;
    tkmc_list_add_tail(list, &mbxcb->mbx_queue);
  }

  return E_OK; // Return success.
}
// memo:
// dummy_struct is a structure needed for sizeof(T_MSG), and in the
// implementation, tkmc_list_head is actually used. However, exposing this
// structure externally would excessively expose the internal implementation and
// increase the header files. Therefore, I will use dummy_struct.