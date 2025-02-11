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
  asm volatile("li a0, 0x2000000;"
               "li a1, 1;"
               "sw a1, 0(a0);" ::
                   : "a0", "a1", "memory");
  while (1) {
    putstring("Hello, world.\n");
    tkmc_yield();
  }
}
