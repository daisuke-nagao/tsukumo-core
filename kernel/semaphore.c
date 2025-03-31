/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "semaphore.h"

SEMCB tkmc_semcbs[CFN_MAX_SEMID];
static tkmc_list_head tkmc_free_semcb;

void tkmc_init_semcb(void) {
}
