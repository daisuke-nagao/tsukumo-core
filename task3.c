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

void task3(INT stacd, void *exinf) {
  T_CFLG pk_cflg = {
      .exinf = NULL,
      .flgatr = TA_TFIFO | TA_WSGL,
      .iflgptn = 0x00FFFFFF,

  };
  ID flgid = tk_cre_flg(&pk_cflg);
  UINT flgptn = 0;
  ER ercd;
  ercd = tk_wai_flg(flgid, 0x00a5a5a5, TWF_ANDW | TWF_BITCLR, &flgptn, TMO_POL);
  if (ercd == E_OK) {
    putstring("TASK3: E_OK\n");
  }
  //
  tk_ext_tsk();
}
