/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "putstring.h"
#include <tk/tkernel.h>

extern void tkmc_yield(void);

void task2(INT stacd) {
  putstring("FizzBuzz\n");
  while (1) {
    if (stacd == 2) {
      putstring("FizzBuzz.\n");
    } else {
      putstring("fizzbuzz.\n");
    }
    tkmc_yield();
  }
}
