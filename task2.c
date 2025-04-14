/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
#include <tk/tkernel.h>
#include <unity.h>

#include "putstring.h"
#include "tasks.h"
#include "userstack.h"

extern ID get_tskid(unsigned int index);

// Prototype declarations for task2_a and task2_b
static void task2_a(INT stacd, void *exinf);
static void task2_b(INT stacd, void *exinf);

/// Entry point for TASK2
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
void task2(INT stacd, void *exinf) {
  putstring("task2 start\n");

  // Create and start task2_a
  ID task2_a_id = tk_cre_tsk(&(T_CTSK){.exinf = NULL,
                                       .tskatr = TA_HLNG | TA_USERBUF,
                                       .task = task2_a,
                                       .itskpri = 1,
                                       .stksz = sizeof(g_stack1_1024B),
                                       .bufptr = g_stack1_1024B});
  TEST_ASSERT_GREATER_THAN(0, task2_a_id);
  if (task2_a_id < 0) {
    putstring("task2_a_id < 0\n");
    tk_exd_tsk();
  }

  ER ercd;

  ercd = tk_sta_tsk(task2_a_id, stacd);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  if (ercd != E_OK) {
    putstring("tk_sta_tsk(task2_a_id, stacd) != E_OK\n");
    tk_exd_tsk();
  }

  ercd = tk_slp_tsk(TMO_FEVR); // Sleep until woken up by task2_a
  TEST_ASSERT_EQUAL(E_OK, ercd);

  ID task3_id = get_tskid(stacd + 1);
  if (task3_id > 0) {
    tk_sta_tsk(task3_id, stacd + 1); // Start task3
  }

  putstring("task2 finish\n");
  // Terminate task2
  tk_exd_tsk();
}

/// Entry point for TASK2_A
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
static void task2_a(INT stacd, void *exinf) {
  putstring("task2_a start\n");

  // Create an event flag
  T_CFLG pk_cflg = {
      .exinf = NULL,                // No extended information
      .flgatr = TA_TFIFO | TA_WSGL, // FIFO order, single wait allowed
      .iflgptn = 0x00FFFFFF,        // Initial flag pattern (lower 24 bits set)
  };

  // Create an event flag object
  ID flgid = tk_cre_flg(&pk_cflg);
  TEST_ASSERT_GREATER_THAN(E_OK, flgid);
  if (flgid < 0) {
    putstring("task2_a: failed to create flag\n");
    tk_exd_tsk();
  }

  UINT flgptn = 0;
  ER ercd;

  // Attempt to wait for all bits 0x00a5a5a5 to be set and then cleared
  // (TWF_BITCLR) Expected: immediate success since all bits are initially set
  ercd = tk_wai_flg(flgid, 0x00a5a5a5, TWF_ANDW | TWF_BITCLR, &flgptn, TMO_POL);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  // Retry with same condition, expecting timeout because bits were cleared in
  // previous wait
  ercd = tk_wai_flg(flgid, 0x00a5a5a5, TWF_ANDW | TWF_BITCLR, &flgptn, TMO_POL);
  TEST_ASSERT_EQUAL(E_TMOUT, ercd);

  // Try with OR condition for a slightly different pattern
  // Expected: success, since lower bits still match at least part of 0x00a5a5a7
  ercd = tk_wai_flg(flgid, 0x00a5a5a7, TWF_ORW | TWF_CLR, &flgptn, TMO_POL);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  ercd = tk_wai_flg(flgid, 0xFFFFFFFF, TWF_ORW, &flgptn, TMO_POL);
  TEST_ASSERT_EQUAL(E_TMOUT, ercd);

  ercd = tk_set_flg(flgid, 0x00000008);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  ercd = tk_wai_flg(flgid, 0x00000008, TWF_ANDW | TWF_CLR, &flgptn, TMO_POL);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  // Create and start task2_b
  ID task2_b_id = tk_cre_tsk(&(T_CTSK){.exinf = NULL,
                                       .tskatr = TA_HLNG | TA_USERBUF,
                                       .task = task2_b,
                                       .itskpri = 2,
                                       .stksz = sizeof(g_stack2_1024B),
                                       .bufptr = g_stack2_1024B});
  TEST_ASSERT_GREATER_THAN(0, task2_b_id);

  ercd = tk_sta_tsk(task2_b_id, flgid);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  ercd = tk_wai_flg(flgid, 0x00000008, TWF_ANDW | TWF_CLR, &flgptn, 1000);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  ID task2_id = get_tskid(TASK2);
  tk_wup_tsk(task2_id); // Release wait for task2

  putstring("task2_a finish\n");
  // Exit task
  tk_exd_tsk();
}

/// Entry point for TASK2_B
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
static void task2_b(INT stacd, void *exinf) {
  putstring("task2_b start\n");

  // Operate on the event flag
  ID flgid = (ID)stacd;
  UINT setptn = 0xFFFFFFFF;

  UINT flgptn;
  ER ercd = tk_wai_flg(flgid, 0xFFFFFFFF, TWF_ORW, &flgptn, TMO_FEVR);
  TEST_ASSERT_EQUAL(E_OBJ, ercd);

  tk_set_flg(flgid, setptn);

  putstring("task2_b finish\n");
  // Exit task
  tk_exd_tsk();
}
