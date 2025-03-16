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
    ER ercd = E_OK;
    if (msg[0] == 'H') {
      ercd = tk_dly_tsk(990);
    } else {
      ercd = tk_slp_tsk(300);
    }
    putstring(msg);
    if (stacd == 3) {
      tk_rel_wai(4);
      putstring(" 1\n");
    } else {
      if (ercd == E_OK) {
        putstring(" 2\n");
      } else if (ercd == E_RLWAI) {
        putstring(" 3\n");
      } else if (ercd == E_TMOUT) {
        putstring(" 4\n");
      } else {
        putstring(" !\n");
      }
    }
  }
}
