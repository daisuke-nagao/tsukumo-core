/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
#include <tk/tkernel.h>

#include "putstring.h"

extern ID get_tskid(unsigned int index);

enum TASK_INDEX {
  TASK1 = 0,
  TASK2,
  TASK3,
  TASK_NBOF,
};

void task2(INT stacd, void *exinf) {
  ID task3_id = get_tskid(TASK3);
  tk_sta_tsk(task3_id, stacd);
#if 0
  putstring("Hello, world\n");

  const char *msg = (const char *)exinf;
  while (1) {
    ER ercd = tk_slp_tsk(300);
    putstring(msg);
    if (ercd == E_OK) {
      putstring(" TASK2 E_OK\n");
    } else if (ercd == E_RLWAI) {
      putstring(" TASK2 E_RLWAI\n");
    } else if (ercd == E_TMOUT) {
      putstring(" TASK2 E_TMOUT\n");
    } else {
      putstring(" TASK2 !\n");
    }
  }
#endif
  tk_exd_tsk();
}
