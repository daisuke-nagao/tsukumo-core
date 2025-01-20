/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "task.h"

TCB tkmc_tcbs[2] = {
    {0, NON_EXISTENT, NULL, NULL},
    {0, NON_EXISTENT, NULL, NULL},
};

void tkmc_init_tcb(void) {}
