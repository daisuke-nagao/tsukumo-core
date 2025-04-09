/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
#include <tk/tkernel.h>

#include "putstring.h"

extern ID get_tskid(unsigned int index);

// Define stacks for sem_tsk_1 and sem_tsk_2
// Each stack is aligned to 16 bytes, which is required for RISC-V
static char sem_tsk_1_stack[1024 * 4] __attribute__((aligned(16)));
static char sem_tsk_2_stack[1024 * 4] __attribute__((aligned(16)));

// Function prototypes for sem_tsk_1 and sem_tsk_2
static void sem_tsk_1(INT stacd, void *exinf);
static void sem_tsk_2(INT stacd, void *exinf);

// Extended information for sem_tsk_1 and sem_tsk_2
static const char sem_tsk_1_exinf[] = "sem_tsk_1";
static const char sem_tsk_2_exinf[] = "sem_tsk_2";

/// Enum to identify each task by index
enum TASK_INDEX {
  TASK1 = 0,
  TASK2,
  TASK3,
  TASK_NBOF, // Number of tasks
};

static ID s_flgid = 0; // Global event flag ID

/// Entry point for TASK3
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
void task4(INT stacd, void *exinf) {
  putstring("task4 start\n");

  // Create an event flag to synchronize sem_tsk_1 and
  // sem_tsk_2 with task4
  T_CFLG pk_cflg = {
      .exinf = NULL,      // No extended information
      .flgatr = TA_TFIFO, // FIFO order
      .iflgptn = 0,       // Initial flag pattern
  };
  ID flgid = tk_cre_flg(&pk_cflg);
  if (flgid < 0) {
    putstring("task4: failed to create event flag\n");
    tk_exd_tsk();
  }
  s_flgid = flgid;
  putstring("task4: created event flag\n");

  // Create a semaphore to synchronize access between sem_tsk_1 and
  // sem_tsk_2
  T_CSEM pk_csem = {
      .exinf = NULL,      // No extended information
      .sematr = TA_TFIFO, // FIFO order
      .isemcnt = 3,       // Initial count
      .maxsem = 3,        // Maximum count
  };

  ID semid = tk_cre_sem(&pk_csem);
  if (semid < 0) {
    putstring("task4: failed to create semaphore\n");
    tk_exd_tsk();
  }

  // Create and start sem_tsk_1
  ID sem_tsk_1_id = tk_cre_tsk(&(T_CTSK){.exinf = (void *)sem_tsk_1_exinf,
                                         .tskatr = TA_HLNG | TA_USERBUF,
                                         .task = sem_tsk_1,
                                         .itskpri = 3,
                                         .stksz = sizeof(sem_tsk_1_stack),
                                         .bufptr = sem_tsk_1_stack});
  if (sem_tsk_1_id < 0) {
    putstring("sem_tsk_1_id < 0\n");
    tk_exd_tsk();
  }

  ER ercd = tk_sta_tsk(sem_tsk_1_id, semid);
  if (ercd != E_OK) {
    putstring("tk_sta_tsk(sem_tsk_1_id, semid) != E_OK\n");
    tk_exd_tsk();
  }

  // Create and start sem_tsk_2
  ID sem_tsk_2_id = tk_cre_tsk(&(T_CTSK){.exinf = (void *)sem_tsk_2_exinf,
                                         .tskatr = TA_HLNG | TA_USERBUF,
                                         .task = sem_tsk_2,
                                         .itskpri = 3,
                                         .stksz = sizeof(sem_tsk_2_stack),
                                         .bufptr = sem_tsk_2_stack});
  if (sem_tsk_2_id < 0) {
    putstring("sem_tsk_2_id < 0\n");
    tk_exd_tsk();
  }

  ercd = tk_sta_tsk(sem_tsk_2_id, semid);
  if (ercd != E_OK) {
    putstring("tk_sta_tsk(sem_tsk_2_id, semid) != E_OK\n");
    tk_exd_tsk();
  }

  // Wait for sem_tsk_1 and sem_tsk_2 to signal
  // completion via the event flag
  UINT flgptn;
  ercd = tk_wai_flg(flgid, 0x0003, TWF_ANDW | TWF_CLR, &flgptn, TMO_FEVR);
  if (ercd != E_OK) {
    putstring("tk_wai_flg(flgid, 0x0003, TWF_ANDW | TWF_CLR, &flgptn, "
              "TMO_FEVR) != E_OK\n");
    tk_exd_tsk();
  }
  if (flgptn == 3) {
    putstring("task4: sem_tsk_1 and sem_tsk_2 "
              "completed successfully\n");
  } else {
    putstring("task4: sem_tsk_1 and/or sem_tsk_2 failed\n");
  }
  putstring("task4: event flag set\n");

  tk_dly_tsk(100); // Delay to allow other tasks to finish

  //! @todo delete event flag and semaphore
  //! @todo deleting function is note implemented for event flag and semaphore
  tk_del_sem(semid); // Delete the semaphore
  putstring("task4: deleted semaphore\n");

  // Terminate task4
  tk_exd_tsk();
}

/// Entry point for TASK3_A
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
static void sem_tsk_1(INT stacd, void *exinf) {
  putstring("sem_tsk_1 start\n");

  ID semid = (ID)stacd;

  for (int i = 0; i < 5; ++i) {
    // Attempt to acquire the semaphore
    putstring("sem_tsk_1: trying to acquire semaphore\n");
    ER ercd = tk_wai_sem(semid, 3, TMO_FEVR);
    if (ercd == E_OK) {
      putstring("sem_tsk_1: acquired semaphore\n");
    } else {
      putstring("sem_tsk_1: failed to acquire semaphore\n");
    }

    // Simulate some work
    tk_dly_tsk(50);

    // Release the semaphore
    putstring("sem_tsk_1: trying to release semaphore\n");
    ercd = tk_sig_sem(semid, 1);
    ercd = tk_sig_sem(semid, 1);
    ercd = tk_sig_sem(semid, 1);
    if (ercd == E_OK) {
      putstring("sem_tsk_1: released semaphore\n");
    } else {
      putstring("sem_tsk_1: failed to release semaphore\n");
    }

    tk_dly_tsk(50);
  }

  // Signal completion by setting the event flag
  putstring("sem_tsk_1: setting event flag\n");
  tk_set_flg(s_flgid, 0x0001); // Set the first bit of the event flag
  putstring("sem_tsk_1: event flag set\n");

  // Exit task
  tk_exd_tsk();
}

/// Entry point for TASK3_B
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
static void sem_tsk_2(INT stacd, void *exinf) {
  putstring("sem_tsk_2 start\n");

  ID semid = (ID)stacd;

  for (int i = 0; i < 5; ++i) {
    // Attempt to acquire the semaphore
    putstring("sem_tsk_2: trying to acquire semaphore\n");
    ER ercd = tk_wai_sem(semid, 3, TMO_FEVR);
    if (ercd == E_OK) {
      putstring("sem_tsk_2: acquired semaphore\n");
    } else {
      putstring("sem_tsk_2: failed to acquire semaphore\n");
    }

    // Simulate some work
    tk_dly_tsk(50);

    // Release the semaphore
    putstring("sem_tsk_2: trying to release semaphore\n");
    ercd = tk_sig_sem(semid, 1);
    ercd = tk_sig_sem(semid, 1);
    ercd = tk_sig_sem(semid, 1);
    if (ercd == E_OK) {
      putstring("sem_tsk_2: released semaphore\n");
    } else {
      putstring("sem_tsk_2: failed to release semaphore\n");
    }

    tk_dly_tsk(50);
  }

  // Signal completion by setting the event flag
  putstring("sem_tsk_2: setting event flag\n");
  tk_set_flg(s_flgid, 0x0002); // Set the second bit of the event flag
  putstring("sem_tsk_2: event flag set\n");

  // Exit task
  tk_exd_tsk();
}
