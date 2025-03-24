/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_0195C76B_33EA_715C_A3A4_72FC38355057
#define UUID_0195C76B_33EA_715C_A3A4_72FC38355057

#include <tk/tkernel.h>

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct FLGCB {
  tkmc_list_head wait_queue;
  ID flgid;
  void *exinf;
  ATR attr;
  UINT flgptn;
} FLGCB;

extern void tkmc_init_flgcb(void);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_0195C76B_33EA_715C_A3A4_72FC38355057 */
