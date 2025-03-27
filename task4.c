/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
#include <tk/tkernel.h>

#include "putstring.h"

extern ID get_tskid(unsigned int index);

/// Enum to identify each task by index
enum TASK_INDEX {
  TASK1 = 0,
  TASK2,
  TASK3,
  TASK4,
  TASK_NBOF, // Number of tasks
};

/// Entry point for TASK4
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
void task4(INT stacd, void *exinf) {
  ID flgid = (ID)stacd;
  UINT setptn = 0xFFFFFFFF;

  UINT flgptn;
  ER ercd = tk_wai_flg(flgid, 0xFFFFFFFF, TWF_ORW, &flgptn, TMO_FEVR);
  if (ercd == E_OBJ) {
    putstring("TASK4: E_OBJ\n");
  }
  tk_set_flg(flgid, setptn);

  // Exit task
  tk_ext_tsk();
}
