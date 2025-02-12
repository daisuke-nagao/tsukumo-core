/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "putstring.h"
#include <tk/tkernel.h>

extern void tkmc_yield(void);

void task1(INT stacd, void *exinf) {
  putstring("Hello, world\n");

  const char *msg = (const char *)exinf;
  while (1) {
    putstring(msg);
    if (stacd == 1) {
      putstring(" 1\n");
    } else {
      putstring(" 2\n");
    }
    tkmc_yield();
  }
}
