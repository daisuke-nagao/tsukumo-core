/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_0195ECC8_7D68_710E_9F8F_8F0218E85157
#define UUID_0195ECC8_7D68_710E_9F8F_8F0218E85157

#include <tk/tkernel.h>

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct SEMCB {
  tkmc_list_head wait_queue; // Wait queue
  ID semid;                  // Semaphore ID
  void *exinf;               // Extended information
  ATR sematr;                // Semaphore attributes
  INT semcnt;                // Current semaphore count
  INT maxsem;                // Maximum semaphore count
} SEMCB;

extern void tkmc_init_semcb(void);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_0195ECC8_7D68_710E_9F8F_8F0218E85157 */
