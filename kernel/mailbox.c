/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "mailbox.h"
#include "list.h"

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

// memo:
// dummy_struct is a structure needed for sizeof(T_MSG), and in the
// implementation, tkmc_list_head is actually used. However, exposing this
// structure externally would excessively expose the internal implementation and
// increase the header files. Therefore, I will use dummy_struct.