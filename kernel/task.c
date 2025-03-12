/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "task.h"
#include "asm/rv32/address.h"

/* Memory-mapped addresses for the CLINT (Core Local Interruptor) */
#define CLINT_MSIP_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MSIP_OFFSET)
#define CLINT_MTIME_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MTIME_OFFSET)
#define CLINT_MTIMECMP_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MTIMECMP_OFFSET)

/* Array of Task Control Blocks (TCBs) for managing tasks */
TCB tkmc_tcbs[CFN_MAX_TSKID];

/* List of free TCBs available for allocation */
tkmc_list_head tkmc_free_tcb;

/* Array of ready queues, one for each priority level */
tkmc_list_head tkmc_ready_queue[CFN_MAX_PRI];

/* Pointer to the currently running task */
TCB *current = NULL;

/* Pointer to the next task to be scheduled */
TCB *next = NULL;

/*
 * Initialize the Task Control Block (TCB) system.
 * - Sets up the free TCB list and ready queues.
 * - Prepares each TCB with default values and adds them to the free list.
 */
void tkmc_init_tcb(void) {
  /* Initialize the free TCB list */
  tkmc_init_list_head(&tkmc_free_tcb);

  /* Initialize each TCB and add it to the free list */
  for (int i = 0; i < sizeof(tkmc_tcbs) / sizeof(tkmc_tcbs[0]); ++i) {
    TCB *tcb = &tkmc_tcbs[i];
    *tcb = (TCB){
        .tskid = i + 1,        // Assign a unique task ID
        .itskpri = 0,          // Default priority
        .state = NON_EXISTENT, // Initial state
        .sp = NULL,            // Stack pointer (set when task starts)
        .initial_sp = NULL,    // Initial stack pointer (set at task creation)
        .task = NULL,          // No task function assigned
        .exinf = NULL,
        .tick_count = 0,
        .wupcause = E_OK,
    };
    tkmc_init_list_head(&tcb->head);

    /* Add the TCB to the free list */
    tkmc_list_add_tail(&tcb->head, &tkmc_free_tcb);
  }

  /* Initialize the ready queues for each priority level */
  for (int i = 0; i < sizeof(tkmc_ready_queue) / sizeof(tkmc_ready_queue[0]);
       ++i) {
    tkmc_init_list_head(&tkmc_ready_queue[i]);
  }
}

/*
 * Create a new task.
 * - Allocates a TCB from the free list.
 * - Configures the TCB with the provided task attributes.
 * - Prepares the task's stack and initial context.
 *
 * Parameters:
 * - pk_ctsk: Pointer to the task creation structure containing task attributes.
 *
 * Returns:
 * - Task ID of the newly created task, or an error code if creation fails.
 */
ID tk_cre_tsk(CONST T_CTSK *pk_ctsk) {
  const ATR tskatr = pk_ctsk->tskatr;
  static const ATR VALID_TSKATR =
      TA_ASM | TA_HLNG | TA_USERBUF | TA_RNG0 | TA_RNG1 | TA_RNG2 | TA_RNG3;

  /* Validate task attributes */
  if ((tskatr & ~VALID_TSKATR) != 0) {
    return E_RSATR; // Invalid attribute
  }

  if ((tskatr & TA_USERBUF) == 0) {
    return E_RSATR; // User buffer not specified
  }

  /* Calculate stack boundaries */
  UW *stack_begin = (UW *)pk_ctsk->bufptr;
  UW *stack_end = stack_begin + (pk_ctsk->stksz >> 2);

  ID new_id = E_LIMIT; // Default to error code
  TCB *new_tcb = NULL;
  UINT intsts = 0;

  /* Disable interrupts to ensure atomic access to the free list */
  DI(intsts);
  if (tkmc_list_empty(&tkmc_free_tcb) == FALSE) {
    /* Allocate a TCB from the free list */
    new_tcb = tkmc_list_first_entry(&tkmc_free_tcb, TCB, head);
    tkmc_list_del(&new_tcb->head);

    new_id = new_tcb->tskid; // Assign the task ID
  } else {
    new_id = E_LIMIT; // No free TCBs available
  }

  if (new_id >= 0) {
    /* Initialize the TCB for the new task */
    new_tcb->state = DORMANT;
    stack_end += -32; // Reserve space for the initial context
    for (int i = 0; i < 32; ++i) {
      stack_end[i] = 0xdeadbeef; // Fill stack with a known pattern
    }
    stack_end[0] = (UW)tk_ext_tsk;       // Set return address (ra)
    stack_end[28] = (UW)pk_ctsk->task;   // Set task entry point (mepc)
    new_tcb->sp = stack_end;             // Set stack pointer
    new_tcb->initial_sp = stack_end;     // Store initial stack pointer
    new_tcb->task = pk_ctsk->task;       // Set task function
    new_tcb->itskpri = pk_ctsk->itskpri; // Set task priority
    new_tcb->exinf = pk_ctsk->exinf;     // Set extended information
    new_tcb->tick_count = 0;
    new_tcb->wupcause = E_OK;
  } else {
    new_id = (ID)E_LIMIT; // Task creation failed
  }

  /* Re-enable interrupts */
  EI(intsts);
  return new_id;
}

/*
 * Retrieve the highest-priority task from the ready queues.
 * - Searches the ready queues in order of priority.
 * - Returns the first task found in a non-empty queue.
 *
 * Returns:
 * - Pointer to the highest-priority TCB, or NULL if no tasks are ready.
 */
TCB *tkmc_get_highest_priority_task(void) {
  for (int i = 0; i < sizeof(tkmc_ready_queue) / sizeof(tkmc_ready_queue[0]);
       ++i) {
    if (!tkmc_list_empty(&tkmc_ready_queue[i])) {
      return tkmc_list_first_entry(&tkmc_ready_queue[i], TCB, head);
    }
  }
  return NULL; // No tasks are ready
}

/*
 * Start a task.
 * - Moves the task from the DORMANT state to the READY state.
 * - Adds the task to the appropriate ready queue based on its priority.
 * - Resets the stack pointer to the initial value before execution.
 *
 * Parameters:
 * - tskid: Task ID of the task to start.
 * - stacd: Start code passed to the task.
 *
 * Returns:
 * - E_OK on success, or an error code if the task cannot be started.
 */
ER tk_sta_tsk(ID tskid, INT stacd) {
  if (tskid >= CFN_MAX_TSKID) {
    return E_ID; // Invalid task ID
  }
  TCB *tcb = tkmc_tcbs + tskid - 1;
  UINT intsts = 0;

  /* Disable interrupts to ensure atomic state changes */
  DI(intsts);
  if (tcb->state == NON_EXISTENT) {
    EI(intsts);
    return E_NOEXS; // Task does not exist
  }
  if (tcb->state != DORMANT) {
    EI(intsts);
    return E_OBJ; // Task is not in the DORMANT state
  }

  /* Reset stack pointer to initial value */
  tcb->sp = tcb->initial_sp;

  /* Transition the task to the READY state */
  tcb->state = READY;

  /* Set up the task's initial context */
  PRI itskpri = tcb->itskpri;
  INT *sp = (INT *)tcb->sp;
  sp[6] = stacd;           // Set a0 register
  sp[7] = (INT)tcb->exinf; // Set a1 register
  tkmc_list_add_tail(&tcb->head, &tkmc_ready_queue[itskpri - 1]);

  /* Update the next task to be scheduled */
  if (current != NULL) {
    next = tkmc_get_highest_priority_task();
    if (next != current) {
      out_w(CLINT_MSIP_ADDRESS, 1); // Trigger a machine software interrupt
    }
  }
  EI(intsts);
  return E_OK;
}

/*
 * Yield the CPU to allow other tasks to run.
 * - Moves the current task to the end of its ready queue.
 * - Triggers a context switch if a higher-priority task is ready.
 */
void tkmc_yield(void) {
  TCB *tmp = current;
  PRI itskpri = tmp->itskpri;
  UINT intsts = 0;

  /* Disable interrupts to ensure atomic operations */
  DI(intsts);
  tkmc_list_del(&tmp->head);
  tkmc_list_add_tail(&tmp->head, &tkmc_ready_queue[itskpri - 1]);

  /* Update the next task to be scheduled */
  next = tkmc_get_highest_priority_task();
  if (tmp != next) {
    out_w(CLINT_MSIP_ADDRESS, 1); // Trigger a machine software interrupt
  }
  EI(intsts);
}

/*
 * Terminate the current task.
 * - Moves the task to the DORMANT state.
 * - Triggers a context switch to the next ready task.
 */
void tk_ext_tsk(void) {
  TCB *tmp = current;
  UINT intsts = 0;

  /* Disable interrupts to ensure atomic operations */
  DI(intsts);
  tkmc_list_del(&tmp->head);
  tmp->state = DORMANT;

  /* Update the next task to be scheduled */
  next = tkmc_get_highest_priority_task();
  out_w(CLINT_MSIP_ADDRESS, 1); // Trigger a machine software interrupt
  EI(intsts);
}
