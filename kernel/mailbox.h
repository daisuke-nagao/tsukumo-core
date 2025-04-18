/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01964715_58B1_791F_8654_6C9F0D281D36
#define UUID_01964715_58B1_791F_8654_6C9F0D281D36

#include <tk/tkernel.h>

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct MBXCB {
  tkmc_list_head wait_queue; // Wait queue
  ID mbxid;                  // Mailbox ID
  void *exinf;               // Extended information
  ATR mbxatr;                // Mailbox attributes
} MBXCB;

extern void tkmc_init_mbxcb(void);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01964715_58B1_791F_8654_6C9F0D281D36 */
