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
  TASK_NBOF, // Number of tasks
};

/// Entry point for TASK3
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
void task3(INT stacd, void *exinf) {
  // Define flag creation parameters
  T_CFLG pk_cflg = {
      .exinf = NULL,                // No extended information
      .flgatr = TA_TFIFO | TA_WSGL, // FIFO order, single wait allowed
      .iflgptn = 0x00FFFFFF,        // Initial flag pattern (lower 24 bits set)
  };

  // Create an event flag object
  ID flgid = tk_cre_flg(&pk_cflg);
  UINT flgptn = 0;
  ER ercd;

  // Attempt to wait for all bits 0x00a5a5a5 to be set and then cleared
  // (TWF_BITCLR) Expected: immediate success since all bits are initially set
  ercd = tk_wai_flg(flgid, 0x00a5a5a5, TWF_ANDW | TWF_BITCLR, &flgptn, TMO_POL);
  if (ercd == E_OK) { // expect true
    putstring("TASK3: E_OK\n");
  }

  // Retry with same condition, expecting timeout because bits were cleared in
  // previous wait
  ercd = tk_wai_flg(flgid, 0x00a5a5a5, TWF_ANDW | TWF_BITCLR, &flgptn, TMO_POL);
  if (ercd == E_TMOUT) { // expect true
    putstring("TASK3: E_TMOUT\n");
  }

  // Try with OR condition for a slightly different pattern
  // Expected: success, since lower bits still match at least part of 0x00a5a5a7
  ercd = tk_wai_flg(flgid, 0x00a5a5a7, TWF_ORW | TWF_CLR, &flgptn, TMO_POL);
  if (ercd == E_OK) { // expect true
    putstring("TASK3: E_OK\n");
  }

  ercd = tk_wai_flg(flgid, 0xFFFFFFFF, TWF_ORW, &flgptn, TMO_POL);
  if (ercd == E_TMOUT) { // expect true
    putstring("TASK3: E_TMOUT\n");
  }

  // Exit task
  tk_ext_tsk();
}
