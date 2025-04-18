/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "mailbox.h"
#include "list.h"

#include <tk/tkernel.h>

// Array of mailbox control blocks
MBXCB tkmc_mbxcbs[CFN_MAX_MBXID];
// Free list that holds mailbox control blocks that are not yet allocated
static tkmc_list_head tkmc_free_mbxcb;

// Bitmask used to mark control blocks as non-existent (i.e. not allocated)
#define NOEXS_MASK 0x80000000u

void tkmc_init_mbxcb(void) {
  tkmc_init_list_head(&tkmc_free_mbxcb);

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
    // Add the mailbox control block's wait queue to the free list.
    tkmc_list_add_tail(&mbxcb->wait_queue, &tkmc_free_mbxcb);
  }
}

ID tk_cre_mbx(CONST T_CMBX *pk_cmbx) { return E_LIMIT; }