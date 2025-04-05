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

static void task2_a(INT, void *);

/// Entry point for TASK2
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
void task2(INT stacd, void *exinf) {
  putstring("task2 start\n");

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
    putstring("task2: E_OK\n");
  }

  // Retry with same condition, expecting timeout because bits were cleared in
  // previous wait
  ercd = tk_wai_flg(flgid, 0x00a5a5a5, TWF_ANDW | TWF_BITCLR, &flgptn, TMO_POL);
  if (ercd == E_TMOUT) { // expect true
    putstring("task2: E_TMOUT\n");
  }

  // Try with OR condition for a slightly different pattern
  // Expected: success, since lower bits still match at least part of 0x00a5a5a7
  ercd = tk_wai_flg(flgid, 0x00a5a5a7, TWF_ORW | TWF_CLR, &flgptn, TMO_POL);
  if (ercd == E_OK) { // expect true
    putstring("task2: E_OK\n");
  }

  ercd = tk_wai_flg(flgid, 0xFFFFFFFF, TWF_ORW, &flgptn, TMO_POL);
  if (ercd == E_TMOUT) { // expect true
    putstring("task2: E_TMOUT\n");
  }

  ercd = tk_set_flg(flgid, 0x00000008);
  if (ercd == E_OK) {
    putstring("task2: E_OK\n");
  }

  ercd = tk_wai_flg(flgid, 0x00000008, TWF_ANDW | TWF_CLR, &flgptn, TMO_POL);
  if (ercd == E_OK) { // expect true
    putstring("task2: E_OK\n");
  }

  {
    static UW task4_stack[256] __attribute__((aligned(16)));
    T_CTSK pk_ctsk4 = {
        .exinf = NULL,
        .tskatr = TA_USERBUF,
        .task = (FP)task2_a,
        .itskpri = 4,
        .stksz = sizeof(task4_stack),
        .bufptr = task4_stack,
    };
    ID task4_id = tk_cre_tsk(&pk_ctsk4);
    tk_sta_tsk(task4_id, flgid);
  }

  ercd = tk_wai_flg(flgid, 0x00000008, TWF_ANDW | TWF_CLR, &flgptn, 1000);
  if (ercd == E_OK) { // expect true
    putstring("task2: E_OK. awaken by task\n");
  }

  ID task3_id = get_tskid(TASK2);
  tk_sta_tsk(task3_id, 0); // Start task3

  // Exit task
  tk_exd_tsk();
}

/// Entry point for TASK4
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
void task2_a(INT stacd, void *exinf) {
  ID flgid = (ID)stacd;
  UINT setptn = 0xFFFFFFFF;

  UINT flgptn;
  ER ercd = tk_wai_flg(flgid, 0xFFFFFFFF, TWF_ORW, &flgptn, TMO_FEVR);
  if (ercd == E_OBJ) {
    putstring("task2_a: E_OBJ\n");
  }
  tk_set_flg(flgid, setptn);

  // Exit task
  tk_exd_tsk();
}
