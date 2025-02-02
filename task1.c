/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "putstring.h"
#include <tk/tkernel.h>

extern void tkmc_yield(void);

void task1(void) {
  putstring("Hello, world\n");
  while (1) {
    putstring("Hello, world.\n");
    tkmc_yield();
  }
}
