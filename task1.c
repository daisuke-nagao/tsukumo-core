/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
#include <tk/tkernel.h>

#include "putstring.h"

extern ID get_tskid(unsigned int index);
enum TASK_INDEX {
  TASK1 = 0,
  TASK2 = 1,
  TASK_NBOF,
};
void task1(INT stacd, void *exinf) {
  putstring("Hello, world\n");

  ID task2_id = get_tskid(TASK2);

  const char *msg = (const char *)exinf;
  INT c = 0;
  while (1) {
    ER ercd = E_OK;
    ercd = tk_dly_tsk(990);
    putstring(msg);

    ++c;
    c %= 2;

    if (c) {
      tk_rel_wai(task2_id);
      tk_rel_wai(task2_id);
    } else {
      tk_wup_tsk(task2_id);
      tk_wup_tsk(task2_id);
      tk_wup_tsk(task2_id);
    }
    putstring(" TASK1\n");
  }
}
