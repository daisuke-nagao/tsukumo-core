/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
#include <tk/tkernel.h>

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
  if (flgid < 0) {
    putstring("task3: failed to create event flag\n");
    tk_exd_tsk();
  }
  s_flgid = flgid;
  putstring("task3: created event flag\n");

  // Create a semaphore to synchronize access between sem_tsk_hi and
  // sem_tsk_lo
  T_CSEM pk_csem = {
      .exinf = NULL,      // No extended information
      .sematr = TA_TFIFO, // FIFO order
      .isemcnt = 1,       // Initial count
      .maxsem = 1,        // Maximum count
  };

  ID semid = tk_cre_sem(&pk_csem);
  if (semid < 0) {
    putstring("task3: failed to create semaphore\n");
    tk_exd_tsk();
  }

  // Create and start sem_tsk_hi
  ID sem_tsk_hi_id = tk_cre_tsk(&(T_CTSK){.exinf = NULL,
                                          .tskatr = TA_HLNG | TA_USERBUF,
                                          .task = sem_tsk_hi,
                                          .itskpri = 2,
                                          .stksz = sizeof(g_stack1_1024B),
                                          .bufptr = g_stack1_1024B});
  if (sem_tsk_hi_id < 0) {
    putstring("sem_tsk_hi_id < 0\n");
    tk_exd_tsk();
  }

  ER ercd = tk_sta_tsk(sem_tsk_hi_id, semid);
  if (ercd != E_OK) {
    putstring("tk_sta_tsk(sem_tsk_hi_id, semid) != E_OK\n");
    tk_exd_tsk();
  }

  // Create and start sem_tsk_lo
  ID sem_tsk_lo_id = tk_cre_tsk(&(T_CTSK){.exinf = NULL,
                                          .tskatr = TA_HLNG | TA_USERBUF,
                                          .task = sem_tsk_lo,
                                          .itskpri = 3,
                                          .stksz = sizeof(g_stack2_1024B),
                                          .bufptr = g_stack2_1024B});
  if (sem_tsk_lo_id < 0) {
    putstring("sem_tsk_lo_id < 0\n");
    tk_exd_tsk();
  }

  ercd = tk_sta_tsk(sem_tsk_lo_id, semid);
  if (ercd != E_OK) {
    putstring("tk_sta_tsk(sem_tsk_lo_id, semid) != E_OK\n");
    tk_exd_tsk();
  }

  // Wait for sem_tsk_hi and sem_tsk_lo to signal
  // completion via the event flag
  UINT flgptn;
  ercd = tk_wai_flg(flgid, 0x0003, TWF_ANDW | TWF_CLR, &flgptn, TMO_FEVR);
  if (ercd != E_OK) {
    putstring("tk_wai_flg(flgid, 0x0003, TWF_ANDW | TWF_CLR, &flgptn, "
              "TMO_FEVR) != E_OK\n");
    tk_exd_tsk();
  }
  if (flgptn == 3) {
    putstring("task3: sem_tsk_hi and sem_tsk_lo "
              "completed successfully\n");
  } else {
    putstring("task3: sem_tsk_hi and/or sem_tsk_lo failed\n");
  }
  putstring("task3: event flag set\n");

  tk_dly_tsk(100); // Delay to allow other tasks to finish

  //! @todo delete event flag and semaphore
  //! @todo deleting function is note implemented for event flag and semaphore
  tk_del_sem(semid); // Delete the semaphore
  putstring("task3: deleted semaphore\n");

  tk_sta_tsk(get_tskid(TASK4), 0); // Start TASK4

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
    putstring("sem_tsk_hi: trying to acquire semaphore\n");
    ER ercd = tk_wai_sem(semid, 1, TMO_FEVR);
    if (ercd == E_OK) {
      putstring("sem_tsk_hi: acquired semaphore\n");
    } else {
      putstring("sem_tsk_hi: failed to acquire semaphore\n");
    }

    // Simulate some work
    tk_dly_tsk(50);

    // Release the semaphore
    putstring("sem_tsk_hi: trying to release semaphore\n");
    ercd = tk_sig_sem(semid, 1);
    if (ercd == E_OK) {
      putstring("sem_tsk_hi: released semaphore\n");
    } else {
      putstring("sem_tsk_hi: failed to release semaphore\n");
    }

    tk_dly_tsk(50);
  }

  // Signal completion by setting the event flag
  putstring("sem_tsk_hi: setting event flag\n");
  tk_set_flg(s_flgid, 0x0001); // Set the first bit of the event flag
  putstring("sem_tsk_hi: event flag set\n");

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
    putstring("sem_tsk_lo: trying to acquire semaphore\n");
    ER ercd = tk_wai_sem(semid, 1, TMO_FEVR);
    if (ercd == E_OK) {
      putstring("sem_tsk_lo: acquired semaphore\n");
    } else {
      putstring("sem_tsk_lo: failed to acquire semaphore\n");
    }

    // Simulate some work
    tk_dly_tsk(50);

    // Release the semaphore
    putstring("sem_tsk_lo: trying to release semaphore\n");
    ercd = tk_sig_sem(semid, 1);
    if (ercd == E_OK) {
      putstring("sem_tsk_lo: released semaphore\n");
    } else {
      putstring("sem_tsk_lo: failed to release semaphore\n");
    }

    tk_dly_tsk(50);
  }

  // Signal completion by setting the event flag
  putstring("sem_tsk_lo: setting event flag\n");
  tk_set_flg(s_flgid, 0x0002); // Set the second bit of the event flag
  putstring("sem_tsk_lo: event flag set\n");

  // Exit task
  tk_exd_tsk();
}
