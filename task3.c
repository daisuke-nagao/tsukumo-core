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

/// Entry point for TASK3
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
void task3(INT stacd, void *exinf) {
  putstring("task3 start\n");

  // Create a semaphore
  T_CSEM pk_csem = {
      .exinf = NULL,          // No extended information
      .sematr = TA_TFIFO,     // FIFO order
      .isemcnt = 1,           // Initial count
      .maxsem = 1,            // Maximum count
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
  tk_slp_tsk(TMO_FEVR);

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
    ER ercd = tk_wai_sem(semid, 1, TMO_FEVR);
    if (ercd == E_OK) {
      putstring("task3_a: acquired semaphore\n");
    } else {
      putstring("task3_a: failed to acquire semaphore\n");
    }

    // Simulate some work
    tk_dly_tsk(500);

    // Release the semaphore
    ercd = tk_sig_sem(semid, 1);
    if (ercd == E_OK) {
      putstring("task3_a: released semaphore\n");
    } else {
      putstring("task3_a: failed to release semaphore\n");
    }

    tk_dly_tsk(500);
  }

  // Wake up task3
  ID task3_id = get_tskid(TASK3);
  tk_wup_tsk(task3_id);

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
    ER ercd = tk_wai_sem(semid, 1, TMO_FEVR);
    if (ercd == E_OK) {
      putstring("task3_b: acquired semaphore\n");
    } else {
      putstring("task3_b: failed to acquire semaphore\n");
    }

    // Simulate some work
    tk_dly_tsk(500);

    // Release the semaphore
    ercd = tk_sig_sem(semid, 1);
    if (ercd == E_OK) {
      putstring("task3_b: released semaphore\n");
    } else {
      putstring("task3_b: failed to release semaphore\n");
    }

    tk_dly_tsk(500);
  }

  // Exit task
  tk_exd_tsk();
}
