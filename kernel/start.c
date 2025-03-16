/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>

#include "ini_tsk.h"
#include "task.h"
#include "timer.h"

/*
 * Clears the .bss section by setting all memory within the section to zero.
 * This ensures that uninitialized global and static variables are set to their
 * default values.
 */
static void clear_bss(void);

/*
 * Stack for the initial task (ini_tsk).
 * Aligned to 16 bytes to meet the alignment requirements of the RISC-V
 * architecture.
 */
static UINT ini_tsk_stack[128] __attribute__((aligned(16)));
static UINT idl_tsk_stack[128] __attribute__((aligned(16)));

/*
 * Kernel entry point.
 * This function initializes the system, creates the initial task, and starts
 * the scheduler.
 *
 * Parameters:
 * - a0: Argument passed to the initial task.
 * - a1: Reserved for future use.
 */
void tkmc_start(int a0, int a1) {
  /* Clear the .bss section to initialize uninitialized variables to zero. */
  clear_bss();

  /* Initialize the Task Control Block (TCB) system. */
  tkmc_init_tcb();
  tkmc_init_timer();

  /* create tkmc_ini_tsk */
  {
    /* Define the attributes for the initial task (ini_tsk). */
    T_CTSK pk_ctsk = {
        .exinf = NULL,                  // No extended information
        .tskatr = TA_USERBUF,           // Task uses a user-provided buffer
        .task = (FP)tkmc_ini_tsk,       // Entry point of the initial task
        .itskpri = 1,                   // Highest priority
        .stksz = sizeof(ini_tsk_stack), // Stack size
        .bufptr = ini_tsk_stack,        // Pointer to the stack buffer
    };

    /* Create the initial task and retrieve its task ID. */
    ID ini_tsk_id = tk_cre_tsk(&pk_ctsk);

    /* Start the initial task with the argument a0. */
    tk_sta_tsk(ini_tsk_id, a0);
  }

  /* create tkmc_idl_tsk */
  {
    extern void tkmc_idl_tsk(INT, void *);
    /* Define the attributes for the initial task (ini_tsk). */
    T_CTSK pk_ctsk = {
        .exinf = NULL,                  // No extended information
        .tskatr = TA_USERBUF,           // Task uses a user-provided buffer
        .task = (FP)tkmc_idl_tsk,       // Entry point of the initial task
        .itskpri = CFN_MAX_PRI,         // Lowest priority
        .stksz = sizeof(idl_tsk_stack), // Stack size
        .bufptr = idl_tsk_stack,        // Pointer to the stack buffer
    };

    /* Create the initial task and retrieve its task ID. */
    ID idl_tsk_id = tk_cre_tsk(&pk_ctsk);

    /* Start the initial task with the argument a0. */
    tk_sta_tsk(idl_tsk_id, a0);
  }

  /* Retrieve the highest-priority task (should be the initial task). */
  TCB *tcb = tkmc_get_highest_priority_task();
  tcb->state = TTS_RUN; // Mark the task as running
  current = tcb;        // Set the current task pointer

  /* Start the system timer for task scheduling. */
  tkmc_start_timer();

  /* Enable interrupts globally. */
  EI(0);
  /* Launch the initial task by restoring its context. */
  __launch_task(&tcb->sp);

  return;
}

/*
 * Clears the .bss section by setting all memory within the section to zero.
 * This function is typically called during system initialization.
 */
static void clear_bss(void) {
  extern int *_bss_start; // Start address of the .bss section
  extern int *_bss_end;   // End address of the .bss section
  for (int *ptr = _bss_start; ptr < _bss_end; ++ptr) {
    *ptr = 0; // Set each word in the .bss section to zero
  }
}

/*
 * Scheduler function.
 * This function is called during a context switch to determine the next task to
 * run.
 *
 * Parameters:
 * - sp: Stack pointer of the currently running task.
 *
 * Returns:
 * - Pointer to the stack pointer of the next task to run.
 */
void **schedule(void *sp) {
  /* Save the stack pointer of the current task. */
  TCB *tmp = current;
  tmp->sp = sp;

  /* Update the state of the current task. */
  if (current->state == TTS_RUN) {
    current->state = TTS_RDY; // Move the current task back to the TTS_RDY state
  }
  next->state = TTS_RUN; // Mark the next task as running

  /* Update the current task pointer to the next task. */
  current = next;

  /* Return the stack pointer of the next task. */
  return &current->sp;
}
