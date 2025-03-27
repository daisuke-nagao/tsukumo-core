/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "task.h"
#include "asm/rv32/address.h"
#include "dispatch.h"

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

static void reset_tcb(TCB *tcb) {
  // tcb->tskid = 0; // keep as it is
  tcb->itskpri = 0;
  tcb->tskstat = 0;
  tcb->tskwait = 0;
  tcb->sp = NULL;
  tcb->initial_sp = NULL;
  tcb->task = NULL;
  tcb->exinf = NULL;
  tcb->delay_ticks = 0;
  tcb->wupcause = E_OK;
  tcb->wupcnt = 0;
  tcb->winfo.flgptn = 0;
  tcb->winfo.wfmode = 0;

  tkmc_init_list_head(&tcb->head);
  tkmc_init_list_head(&tcb->winfo.wait_queue);

  return;
}

static void default_tcb(TCB *tcb) {
  reset_tcb(tcb);
  tcb->tskid = 0;

  return;
}

/// @brief Represents the full set of general-purpose registers to be saved
/// during a trap.
///
/// This structure corresponds to the 32 general-purpose registers in RISC-V,
/// including `mepc` (Machine Exception Program Counter), and is used to save
/// the context when entering a trap handler.
///
/// The padding ensures the structure is exactly 32 words (128 bytes),
/// matching the register save area expectations in the trap handling mechanism.
typedef union StashedRegisters {
  struct {
    UINT ra;                             ///< Return address (x1)
    UINT t0, t1, t2;                     ///< Temporary registers (x5–x7)
    UINT s0, s1;                         ///< Saved registers (x8–x9)
    UINT a0, a1, a2, a3, a4, a5, a6, a7; ///< Argument registers (x10–x17)
    UINT s2, s3, s4, s5, s6, s7, s8, s9, s10,
        s11;             ///< Saved registers (x18–x27)
    UINT t3, t4, t5, t6; ///< Temporary registers (x28–x31)
    UINT mepc;           ///< Machine Exception Program Counter

    UINT padding[3];
  } __attribute__((
      packed)) registers; ///< Padding to ensure the structure is 32 words
  UINT array[32];
} StashedRegisters;

// Ensure that the structure occupies exactly 32 words (128 bytes),
// matching the size of a full register context save
_Static_assert(sizeof(StashedRegisters) == sizeof(UINT[32]),
               "StashedRegisters must be exactly 32 UINTs in size (128 bytes)");

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

    default_tcb(tcb);
    tcb->tskid = i + 1; // Assign a unique task ID

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
  UB *stack_begin = (UB *)pk_ctsk->bufptr;
  UB *stack_end = stack_begin + pk_ctsk->stksz;

  ID new_id = E_LIMIT; // Default to error code
  TCB *new_tcb = NULL;
  UINT intsts = 0;

  /* Disable interrupts to ensure atomic access to the free list */
  DI(intsts);
  if (tkmc_list_empty(&tkmc_free_tcb) == FALSE) {
    /* Allocate a TCB from the free list */
    new_tcb = tkmc_list_first_entry(&tkmc_free_tcb, TCB, head);
    tkmc_list_del(&new_tcb->head);
    tkmc_init_list_head(&new_tcb->head);

    new_id = new_tcb->tskid; // Assign the task ID
  } else {
    new_id = E_LIMIT; // No free TCBs available
  }

  if (new_id >= 0) {
    reset_tcb(new_tcb);

    /* Initialize the TCB for the new task */
    static const UINT SZ = sizeof(StashedRegisters);
    stack_end -= SZ; // Reserve space for the initial context
    StashedRegisters *regs = (StashedRegisters *)stack_end;
    for (int i = 0; i < sizeof(regs->array) / sizeof(regs->array[0]); ++i) {
      regs->array[i] = 0xdeadbeef; // Fill stack with a known pattern
    }
    regs->registers.ra = (UINT)tk_ext_tsk;      // Set return address (ra)
    regs->registers.mepc = (UINT)pk_ctsk->task; // Set task entry point (mepc)

    new_tcb->tskstat = TTS_DMT;          // Set initial task state
    new_tcb->sp = stack_end;             // Set stack pointer
    new_tcb->initial_sp = stack_end;     // Store initial stack pointer
    new_tcb->task = pk_ctsk->task;       // Assign task function
    new_tcb->itskpri = pk_ctsk->itskpri; // Assign task priority
    new_tcb->exinf = pk_ctsk->exinf; // Store user-defined extended information
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
 * - Moves the task from the TTS_DMT state to the TTS_RDY state.
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
  if (tskid > CFN_MAX_TSKID) {
    return E_ID; // Invalid task ID
  }
  TCB *tcb = tkmc_tcbs + tskid - 1;
  UINT intsts = 0;

  /* Disable interrupts to ensure atomic state changes */
  DI(intsts);
  if (tcb->tskstat == TTS_NOEXS) {
    EI(intsts);
    return E_NOEXS; // Task does not exist
  }
  if (tcb->tskstat != TTS_DMT) {
    EI(intsts);
    return E_OBJ; // Task is not in the TTS_DMT state
  }

  /* Reset stack pointer to initial value */
  tcb->sp = tcb->initial_sp;

  /* Transition the task to the TTS_RDY state */
  tcb->tskstat = TTS_RDY;
  tcb->tskwait = 0;

  /* Set up the task's initial context */
  PRI itskpri = tcb->itskpri;
  StashedRegisters *regs = (StashedRegisters *)tcb->sp;
  regs->registers.a0 = (UINT)stacd;      // Set a0 register
  regs->registers.a1 = (UINT)tcb->exinf; // Set a1 register
  tkmc_list_add_tail(&tcb->head, &tkmc_ready_queue[itskpri - 1]);

  /* Update the next task to be scheduled */
  if (current != NULL) {
    next = tkmc_get_highest_priority_task();
    if (next != current) {
      dispatch();
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
    dispatch();
  }
  EI(intsts);
}

/*
 * Terminate the current task.
 * - Moves the task to the TTS_DMT state.
 * - Triggers a context switch to the next ready task.
 */
void tk_ext_tsk(void) {
  TCB *tmp = current;
  UINT intsts = 0;

  /* Disable interrupts to ensure atomic operations */
  DI(intsts);
  tkmc_list_del(&tmp->head);
  tmp->tskstat = TTS_DMT;
  tmp->tskwait = 0;

  /* Update the next task to be scheduled */
  next = tkmc_get_highest_priority_task();
  dispatch();
  EI(intsts);
}

/*
 * Release a waiting task.
 * - Moves the specified task from the TTS_WAI state to the TTS_RDY state.
 * - Removes the task from the waiting queue and adds it to the appropriate
 *   ready queue.
 * - Triggers a context switch if a higher-priority task is ready.
 *
 * Parameters:
 * - tskid: Task ID of the task to be released.
 *
 * Returns:
 * - E_OK on success.
 * - E_ID if the task ID is invalid.
 * - E_NOEXS if the task does not exist.
 * - E_OBJ if the task is not in the TTS_WAI state.
 */
ER tk_rel_wai(ID tskid) {
  // Check if the task ID is out of range
  if (tskid <= 0 || tskid > CFN_MAX_TSKID) {
    return E_ID;
  }
  TCB *tcb = &tkmc_tcbs[tskid - 1];

  ER ercd = E_OK;
  UINT intsts = 0;

  // Disable interrupts to prevent race conditions
  DI(intsts);

  if (ercd == E_OK) {
    // Validate the state of the task before proceeding
    if (tcb->tskstat == TTS_NOEXS) {
      ercd = E_NOEXS; // Task does not exist
    } else if (tcb == current) {
      ercd = E_OBJ; // Cannot release the current task
    } else if (tcb->tskstat != TTS_WAI) {
      ercd = E_OBJ; // Task is not in a waiting state
    }
  }

  if (ercd == E_OK) {
    // Transition the task to the ready state
    tcb->tskstat = TTS_RDY;
    tcb->tskwait = 0;
    tcb->wupcause = E_RLWAI; // Reason for wake-up is a release request
    tcb->wupcnt = 0;

    // Remove the task from the waiting queue if it is present
    if (!tkmc_list_empty(&tcb->head)) {
      tkmc_list_del(&tcb->head);
    }

    // Add the task to the appropriate ready queue based on its priority
    tkmc_list_add_tail(&tcb->head, &tkmc_ready_queue[tcb->itskpri - 1]);

    // Check if a higher-priority task needs to run
    next = tkmc_get_highest_priority_task();
    if (current != next) {
      dispatch(); // Perform a context switch
    }
  }

  // Re-enable interrupts after making changes
  EI(intsts);

  return ercd;
}

/*
 * Wake up a task.
 * - Transitions a sleeping or waiting task to the ready state.
 * - If the task is already ready or running, increments its wakeup count.
 *
 * Parameters:
 * - tskid: ID of the task to wake up.
 *
 * Returns:
 * - E_OK on success.
 * - E_ID if the task ID is invalid.
 * - E_NOEXS if the task does not exist.
 * - E_OBJ if the task is not in a suitable state to be woken up.
 * - E_QOVR if the wakeup count exceeds its maximum value.
 */
ER tk_wup_tsk(ID tskid) {
  // Validate the task ID
  if (tskid <= 0 || tskid > CFN_MAX_TSKID) {
    return E_ID; // Invalid task ID
  }

  // Get the TCB for the specified task ID
  TCB *tcb = &tkmc_tcbs[tskid - 1];

  ER ercd = E_OK;
  UINT intsts = 0;
  const UINT WUPCNT_MAX = (~(UINT)(0)) - 1;

  // Disable interrupts to ensure atomic operations
  DI(intsts);

  // Check the task state
  UINT tskstat = tcb->tskstat;
  if (tskstat == TTS_WAI) {
    UINT tskwait = tcb->tskwait;
    if (tskwait == TTW_SLP) {
      // If the task is waiting to sleep, make it ready
      tcb->tskstat = TTS_RDY;
      tcb->tskwait = 0;
      tcb->wupcause = E_OK; // Normal wake-up
      tcb->wupcnt = 0;

      // Remove the task from any queues and add it to the ready queue
      if (!tkmc_list_empty(&tcb->head)) {
        tkmc_list_del(&tcb->head);
      }
      tkmc_list_add_tail(&tcb->head, &tkmc_ready_queue[tcb->itskpri - 1]);

      // Check if a higher-priority task needs to run
      next = tkmc_get_highest_priority_task();
      if (current != next) {
        dispatch(); // Perform a context switch
      }
    } else {
      // If the task is waiting on something else, increment the wakeup count
      if (tcb->wupcnt <= WUPCNT_MAX) {
        tcb->wupcnt += 1;
        ercd = E_OK;
      } else {
        ercd = E_QOVR; // Wakeup count overflow
      }
    }
  } else if (tskstat == TTS_RDY || tskstat == TTS_RUN) {
    // If the task is already ready or running, increment the wakeup count
    if (tcb->wupcnt <= WUPCNT_MAX) {
      tcb->wupcnt += 1;
      ercd = E_OK;
    } else {
      ercd = E_QOVR; // Wakeup count overflow
    }
  } else if (tskstat == TTS_NOEXS) {
    ercd = E_NOEXS; // Task does not exist
  } else {
    ercd = E_OBJ; // Invalid state for waking up
  }

  // Re-enable interrupts
  EI(intsts);

  return ercd;
}
