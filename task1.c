/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "putstring.h"
#include <tk/tkernel.h>

extern void tkmc_yield(void);

void task1(INT stacd) {
  putstring("Hello, world\n");
  while (1) {
    if (stacd == 1) {
      putstring("Hello, world.\n");
    } else {
      putstring("hello, World.\n");
    }
    tkmc_yield();
  }
}
