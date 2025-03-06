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
    extern ER tkmc_dly_tsk(TMO tmout);
    if (msg[0] == 'H') {
      tkmc_dly_tsk(10);
    } else {
      tkmc_dly_tsk(0);
    }
  }
}
