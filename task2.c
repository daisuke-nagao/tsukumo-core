/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "putstring.h"
#include <typedef.h>

extern void tkmc_context_switch(ID tskid);

void task2(void) {
  putstring("FizzBuzz\n");
  while (1) {
    putstring("FizzBuzz.\n");
    tkmc_context_switch(0);
  }
}
