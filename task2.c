/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "putstring.h"
#include <tk/tkernel.h>

extern void tkmc_yield(void);

void task2(void) {
  putstring("FizzBuzz\n");
  while (1) {
    putstring("FizzBuzz.\n");
    tkmc_yield();
  }
}
