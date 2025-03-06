/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
#include <tk/tkernel.h>

#include "putstring.h"

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
    if (msg[0] == 'H') {
      tk_dly_tsk(10);
    } else {
      tk_dly_tsk(0);
    }
  }
}
