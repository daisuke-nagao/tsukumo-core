/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
#include <tk/tkernel.h>

#include "putstring.h"

extern ID get_tskid(unsigned int index);

// Define stacks for task3_a and task3_b
// aligned(16) is required for RISC-V
static char task3_a_stack[1024 * 4] __attribute__((aligned(16)));
static char task3_b_stack[1024 * 4] __attribute__((aligned(16)));

// Prototype declarations for task3_a and task3_b
static void task3_a(INT stacd, void *exinf);
static void task3_b(INT stacd, void *exinf);

static const char task3_a_exinf[] = "task3_a";
static const char task3_b_exinf[] = "task3_b";

/// Enum to identify each task by index
enum TASK_INDEX {
  TASK1 = 0,
  TASK2,
  TASK3,
  TASK_NBOF, // Number of tasks
};

ID s_flgid = 0; // Event flag ID

/// Entry point for TASK3
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
void task3(INT stacd, void *exinf) {
  putstring("task3 start\n");

  //! @todo create event flag that is used to wake me up on task3_a and task3_b
  //! exit
  // Create an event flag
  T_CFLG pk_cflg = {
      .exinf = NULL,      // No extended information
      .flgatr = TA_TFIFO, // FIFO order
      .iflgptn = 0,       // Initial pattern
  };
  ID flgid = tk_cre_flg(&pk_cflg);
  if (flgid < 0) {
    putstring("task3: failed to create event flag\n");
    tk_exd_tsk();
  }
  s_flgid = flgid;
  putstring("task3: created event flag\n");

  // Create a semaphore
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

  // Create and start task3_a
  ID task3_a_id = tk_cre_tsk(&(T_CTSK){.exinf = (void *)task3_a_exinf,
                                       .tskatr = TA_HLNG | TA_USERBUF,
                                       .task = task3_a,
                                       .itskpri = 2,
                                       .stksz = sizeof(task3_a_stack),
                                       .bufptr = task3_a_stack});
  if (task3_a_id < 0) {
    putstring("task3_a_id < 0\n");
    tk_exd_tsk();
  }

  ER ercd = tk_sta_tsk(task3_a_id, semid);
  if (ercd != E_OK) {
    putstring("tk_sta_tsk(task3_a_id, semid) != E_OK\n");
    tk_exd_tsk();
  }

  // Create and start task3_b
  ID task3_b_id = tk_cre_tsk(&(T_CTSK){.exinf = (void *)task3_b_exinf,
                                       .tskatr = TA_HLNG | TA_USERBUF,
                                       .task = task3_b,
                                       .itskpri = 3,
                                       .stksz = sizeof(task3_b_stack),
                                       .bufptr = task3_b_stack});
  if (task3_b_id < 0) {
    putstring("task3_b_id < 0\n");
    tk_exd_tsk();
  }

  ercd = tk_sta_tsk(task3_b_id, semid);
  if (ercd != E_OK) {
    putstring("tk_sta_tsk(task3_b_id, semid) != E_OK\n");
    tk_exd_tsk();
  }

  // Wait for task3_a and task3_b to finish
  // tk_slp_tsk(TMO_FEVR);
  // Wait for the event flag to be set by task3_a and task3_b by waiting until 2
  // bits are set in the event flag. This is a blocking wait.
  UINT flgptn;
  ercd = tk_wai_flg(flgid, 0x0003, TWF_ANDW | TWF_CLR, &flgptn, TMO_FEVR);
  if (ercd != E_OK) {
    putstring("tk_wai_flg(flgid, 0x0003, TWF_ANDW | TWF_CLR, &flgptn, "
              "TMO_FEVR) != E_OK\n");
    tk_exd_tsk();
  }
  putstring("task3: event flag set\n");

  //! @todo delete event flag and semaphore
  //! @todo deleting function is note implemented for event flag and semaphore

  // Terminate task3
  tk_exd_tsk();
}

/// Entry point for TASK3_A
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
static void task3_a(INT stacd, void *exinf) {
  putstring("task3_a start\n");

  ID semid = (ID)stacd;

  for (int i = 0; i < 5; ++i) {
    // Wait for the semaphore
    putstring("task3_a: trying to acquire semaphore\n");
    ER ercd = tk_wai_sem(semid, 1, TMO_FEVR);
    if (ercd == E_OK) {
      putstring("task3_a: acquired semaphore\n");
    } else {
      putstring("task3_a: failed to acquire semaphore\n");
    }

    // Simulate some work
    tk_dly_tsk(500);

    // Release the semaphore
    putstring("task3_a: trying to release semaphore\n");
    ercd = tk_sig_sem(semid, 1);
    if (ercd == E_OK) {
      putstring("task3_a: released semaphore\n");
    } else {
      putstring("task3_a: failed to release semaphore\n");
    }

    tk_dly_tsk(500);
  }

  // Set the event flag to indicate task3_a is done
  putstring("task3_a: setting event flag\n");
  tk_set_flg(s_flgid, 0x0001); // Set the first bit of the event flag
  putstring("task3_a: event flag set\n");

  // Exit task
  tk_exd_tsk();
}

/// Entry point for TASK3_B
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
static void task3_b(INT stacd, void *exinf) {
  putstring("task3_b start\n");

  ID semid = (ID)stacd;

  for (int i = 0; i < 5; ++i) {
    // Wait for the semaphore
    putstring("task3_b: trying to acquire semaphore\n");
    ER ercd = tk_wai_sem(semid, 1, TMO_FEVR);
    if (ercd == E_OK) {
      putstring("task3_b: acquired semaphore\n");
    } else {
      putstring("task3_b: failed to acquire semaphore\n");
    }

    // Simulate some work
    tk_dly_tsk(500);

    // Release the semaphore
    putstring("task3_b: trying to release semaphore\n");
    ercd = tk_sig_sem(semid, 1);
    if (ercd == E_OK) {
      putstring("task3_b: released semaphore\n");
    } else {
      putstring("task3_b: failed to release semaphore\n");
    }

    tk_dly_tsk(500);
  }

  // Set the event flag to indicate task3_b is done
  putstring("task3_b: setting event flag\n");
  tk_set_flg(s_flgid, 0x0002); // Set the second bit of the event flag
  putstring("task3_b: event flag set\n");

  // Exit task
  tk_exd_tsk();
}
