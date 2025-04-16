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

// Function prototypes for sem_tsk_hi and sem_tsk_lo
static void sem_tsk_hi(INT stacd, void *exinf);
static void sem_tsk_lo(INT stacd, void *exinf);

static ID s_flgid = 0; // Global event flag ID

/// Entry point for TASK3
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
void task3(INT stacd, void *exinf) {
  putstring("task3 start\n");

  // Create an event flag to synchronize sem_tsk_hi and
  // sem_tsk_lo with task3
  T_CFLG pk_cflg = {
      .exinf = NULL,      // No extended information
      .flgatr = TA_TFIFO, // FIFO order
      .iflgptn = 0,       // Initial flag pattern
  };
  ID flgid = tk_cre_flg(&pk_cflg);
  TEST_ASSERT_GREATER_THAN(E_OK, flgid);
  s_flgid = flgid;

  // Create a semaphore to synchronize access between sem_tsk_hi and
  // sem_tsk_lo
  T_CSEM pk_csem = {
      .exinf = NULL,      // No extended information
      .sematr = TA_TFIFO, // FIFO order
      .isemcnt = 1,       // Initial count
      .maxsem = 1,        // Maximum count
  };

  ID semid = tk_cre_sem(&pk_csem);
  TEST_ASSERT_GREATER_THAN(E_OK, semid);

  // Create and start sem_tsk_hi
  ID sem_tsk_hi_id = tk_cre_tsk(&(T_CTSK){.exinf = NULL,
                                          .tskatr = TA_HLNG | TA_USERBUF,
                                          .task = sem_tsk_hi,
                                          .itskpri = 2,
                                          .stksz = sizeof(g_stack1_1024B),
                                          .bufptr = g_stack1_1024B});
  TEST_ASSERT_GREATER_THAN(E_OK, sem_tsk_hi_id);

  ER ercd = tk_sta_tsk(sem_tsk_hi_id, semid);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  // Create and start sem_tsk_lo
  ID sem_tsk_lo_id = tk_cre_tsk(&(T_CTSK){.exinf = NULL,
                                          .tskatr = TA_HLNG | TA_USERBUF,
                                          .task = sem_tsk_lo,
                                          .itskpri = 3,
                                          .stksz = sizeof(g_stack2_1024B),
                                          .bufptr = g_stack2_1024B});
  TEST_ASSERT_GREATER_THAN(E_OK, sem_tsk_lo_id);

  ercd = tk_sta_tsk(sem_tsk_lo_id, semid);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  // Wait for sem_tsk_hi and sem_tsk_lo to signal
  // completion via the event flag
  UINT flgptn;
  ercd = tk_wai_flg(flgid, 0x0003, TWF_ANDW | TWF_CLR, &flgptn, TMO_FEVR);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_EQUAL(0x0003, flgptn);

  tk_dly_tsk(100); // Delay to allow other tasks to finish

  //! @todo delete event flag
  //! @todo deleting function is note implemented for event flag
  ercd = tk_del_sem(semid); // Delete the semaphore
  TEST_ASSERT_EQUAL(E_OK, ercd);

  ID task4_id = get_tskid(stacd + 1);
  if (task4_id > 0) {
    tk_sta_tsk(task4_id, stacd + 1); // Start task4
  }

  putstring("task3 finish\n");
  // Terminate task3
  tk_exd_tsk();
}

/// Entry point for TASK3_A
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
static void sem_tsk_hi(INT stacd, void *exinf) {
  putstring("sem_tsk_hi start\n");

  ID semid = (ID)stacd;

  for (int i = 0; i < 5; ++i) {
    // Attempt to acquire the semaphore
    ER ercd;

    ercd = tk_wai_sem(semid, 1, 100);
    TEST_ASSERT_EQUAL(E_OK, ercd);

    // Simulate some work
    tk_dly_tsk(50);

    // Release the semaphore
    ercd = tk_sig_sem(semid, 1);
    TEST_ASSERT_EQUAL(E_OK, ercd);

    tk_dly_tsk(50);
  }

  // Signal completion by setting the event flag
  tk_set_flg(s_flgid, 0x0001); // Set the first bit of the event flag

  putstring("sem_tsk_hi finish\n");

  // Exit task
  tk_exd_tsk();
}

/// Entry point for TASK3_B
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
static void sem_tsk_lo(INT stacd, void *exinf) {
  putstring("sem_tsk_lo start\n");

  ID semid = (ID)stacd;

  for (int i = 0; i < 5; ++i) {
    // Attempt to acquire the semaphore
    ER ercd;

    ercd = tk_wai_sem(semid, 1, 100);
    TEST_ASSERT_EQUAL(E_OK, ercd);

    // Simulate some work
    tk_dly_tsk(50);

    // Release the semaphore
    ercd = tk_sig_sem(semid, 1);
    TEST_ASSERT_EQUAL(E_OK, ercd);

    tk_dly_tsk(50);
  }

  // Signal completion by setting the event flag
  tk_set_flg(s_flgid, 0x0002); // Set the second bit of the event flag

  putstring("sem_tsk_lo finish\n");

  // Exit task
  tk_exd_tsk();
}
