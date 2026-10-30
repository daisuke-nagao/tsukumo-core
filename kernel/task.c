/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "task.h"
#include "asm/rv32/address.h"
#include "dispatch.h"
#include "memset.h"
#include "object_id.h"
#include "semaphore.h"

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
  memset(&tcb->winfo, 0, sizeof(tcb->winfo));

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
    UINT t0, t1, t2;                     ///< Temporary registers (x5-x7)
    UINT s0, s1;                         ///< Saved registers (x8-x9)
    UINT a0, a1, a2, a3, a4, a5, a6, a7; ///< Argument registers (x10-x17)
    UINT s2, s3, s4, s5, s6, s7, s8, s9, s10,
        s11;             ///< Saved registers (x18-x27)
    UINT t3, t4, t5, t6; ///< Temporary registers (x28-x31)
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

// The RISC-V ABI keeps sp 16 byte aligned. tk_cre_tsk() refuses a stack whose
// end is not, and does not round the end down instead: the stack the caller
// gets would be smaller than the size it asked for, with no word of it.
#define STACK_ALIGN 16

// <stdint.h> is out of reach (-nostdinc), and UINT is 32 bits on the PC tests
// too, where the overflow of a stack end has to be checked at the width of the
// pointers.
typedef __UINTPTR_TYPE__ tkmc_uintptr_t;
#define TKMC_UINTPTR_MAX (~(tkmc_uintptr_t)0)

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
 * - Checks the stack the caller gives, and reserves the room for the initial
 *   context in it (the context itself is built by tk_sta_tsk).
 *
 * Parameters:
 * - pk_ctsk: Pointer to the task creation structure containing task attributes.
 *
 * Returns:
 * - Task ID of the newly created task, or an error code if creation fails:
 *   E_PAR for a NULL packet, entry or buffer, a priority outside
 *   1..CFN_MAX_PRI, a stack of at most the 128 byte saved frame, a stack that
 *   runs past the end of the address space, or one that does not end on a 16
 *   byte boundary; E_RSATR for an attribute the kernel does not support, which
 *   is checked before the parameters; E_LIMIT when no TCB is free. A refused
 *   call changes nothing.
 *
 * The size check only tells that the frame fits. How deep the task can go
 * below it is up to the caller, who must also leave room for the kernel: an
 * interrupt saves a frame of sizeof(StashedRegisters) bytes on the stack of
 * the task it interrupts and runs its handler there, so every stack needs that
 * frame plus the handler's own frames (see the -fstack-usage output) below the
 * task's deepest point.
 */
ID tk_cre_tsk(CONST T_CTSK *pk_ctsk) {
  if (pk_ctsk == NULL) {
    return E_PAR;
  }
  // Each field is read once, so a packet that changes during the call cannot
  // get a value past the checks.
  const ATR tskatr = pk_ctsk->tskatr;
  const FP task = pk_ctsk->task;
  void *const bufptr = pk_ctsk->bufptr;
  const PRI itskpri = pk_ctsk->itskpri;
  const SZ stksz = pk_ctsk->stksz;
  void *const exinf = pk_ctsk->exinf;
  static const ATR VALID_TSKATR =
      TA_ASM | TA_HLNG | TA_USERBUF | TA_RNG0 | TA_RNG1 | TA_RNG2 | TA_RNG3;

  /* Validate task attributes */
  if ((tskatr & ~VALID_TSKATR) != 0) {
    return E_RSATR; // Invalid attribute
  }

  if ((tskatr & TA_USERBUF) == 0) {
    return E_RSATR; // User buffer not specified
  }

  if (task == NULL || bufptr == NULL) {
    return E_PAR;
  }
  if (itskpri < 1 || itskpri > CFN_MAX_PRI) {
    return E_PAR; // tkmc_ready_queue[itskpri - 1] would be out of range
  }
  if (stksz <= (SZ)sizeof(StashedRegisters)) {
    // The frame must fit with room left. This also rules out a negative size.
    return E_PAR;
  }
  const tkmc_uintptr_t stack_begin = (tkmc_uintptr_t)bufptr;
  const tkmc_uintptr_t stack_size = (tkmc_uintptr_t)stksz;
  if (stack_size > TKMC_UINTPTR_MAX - stack_begin) {
    return E_PAR; // The end would wrap around the address space
  }
  if ((stack_begin + stack_size) % STACK_ALIGN != 0) {
    return E_PAR;
  }

  /* Calculate stack boundaries */
  UB *stack_end = (UB *)(stack_begin + stack_size);

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
    stack_end -= sizeof(StashedRegisters); // Reserve the initial context

    new_tcb->tskstat = TTS_DMT;      // Set initial task state
    new_tcb->sp = stack_end;         // Set stack pointer
    new_tcb->initial_sp = stack_end; // Store initial stack pointer
    new_tcb->task = task;            // Assign task function
    new_tcb->itskpri = itskpri;      // Assign task priority
    new_tcb->exinf = exinf;          // Store user-defined extended information
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
 * End the wait of a task and make it ready (see task.h).
 *
 * Every way a wait ends goes through here: the time limit, a signal or a set,
 * a delete, tk_rel_wai(), tk_wup_tsk(). The task must leave every queue the
 * wait put it on at the moment it becomes ready. If it stayed on the wait queue
 * of an object, a signal that comes before the task runs again would find it
 * there and make it ready a second time.
 *
 * Whether the task is on the timer queue is told by its head being on a queue,
 * not by delay_ticks: a wait for ever has none, and the timer handler sets the
 * count of a wait that is over to 0 before it ends the wait.
 */
void tkmc_complete_wait(TCB *tcb, ER cause) {
  const UINT tskwait = tcb->tskwait;
  if (!tkmc_list_empty(&tcb->head)) {
    tkmc_list_del(&tcb->head);
  }
  if (!tkmc_list_empty(&tcb->winfo.wait_queue)) {
    tkmc_list_del(&tcb->winfo.wait_queue);
    tkmc_init_list_head(&tcb->winfo.wait_queue);
  }

  tcb->delay_ticks = 0;
  tcb->tskwait = 0;
  tcb->wupcause = cause;
  tcb->tskstat = TTS_RDY;
  tkmc_list_add_tail(&tcb->head, &tkmc_ready_queue[tcb->itskpri - 1]);

  // A semaphore waiter that leaves without its units may have held back the
  // waiters behind it (TA_FIRST); they get the units now. Not after E_OK: the
  // task took its units, and the signal that served it serves the rest. Not
  // after E_DLT: the semaphore is being deleted with every waiter.
  if (tskwait == TTW_SEM && (cause == E_TMOUT || cause == E_RLWAI)) {
    tkmc_sem_serve_waiters(tcb->winfo.semaphore.semcb);
  }
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
  if (!tkmc_id_in_range(tskid, CFN_MAX_TSKID)) {
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

  tcb->tskwait = 0;
  tcb->delay_ticks = 0;
  tcb->wupcnt = 0;
  tcb->wupcause = E_OK;
  memset(&tcb->winfo, 0, sizeof(tcb->winfo));
  tkmc_init_list_head(&tcb->winfo.wait_queue);

  /* The previous run used this frame as ordinary stack space. */
  PRI itskpri = tcb->itskpri;
  StashedRegisters *regs = (StashedRegisters *)tcb->sp;
  for (UINT i = 0; i < sizeof(regs->array) / sizeof(regs->array[0]); ++i) {
    regs->array[i] = 0xdeadbeef;
  }
  regs->registers.ra = (UINT)tk_ext_tsk;
  regs->registers.mepc = (UINT)tcb->task;
  regs->registers.a0 = (UINT)stacd;      // Set a0 register
  regs->registers.a1 = (UINT)tcb->exinf; // Set a1 register
  tcb->tskstat = TTS_RDY;
  tkmc_init_list_head(&tcb->head);
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
 * - This function does not return, provided interrupts are enabled: the switch
 *   is taken when they come back on at the end. Called with them disabled, it
 *   returns, and the caller keeps running as a dormant task whose list link
 *   is no longer valid. There is no return value to report this with; the
 *   specification does not allow the call from such a context.
 */
void tk_ext_tsk(void) {
  TCB *tmp = current;
  UINT intsts = 0;

  /* Disable interrupts to ensure atomic operations */
  DI(intsts);

  /* Remove the current task from the ready queue */
  tkmc_list_del(&tmp->head);

  /* Set the task state to "dormant" */
  tmp->tskstat = TTS_DMT;
  tmp->tskwait = 0;

  /* Schedule the next task to be executed */
  next = tkmc_get_highest_priority_task();
  dispatch();

  /* Re-enable interrupts */
  EI(intsts);
}

/*
 * Terminate and delete the current task.
 * - Moves the task to the TTS_NOEXS state.
 * - Returns the task to the free list for reuse.
 * - Triggers a context switch to the next ready task.
 * - This function does not return, with the same condition as tk_ext_tsk():
 *   called with interrupts disabled, it returns, and the caller keeps running
 *   on a task that is on the free list.
 */
void tk_exd_tsk(void) {
  TCB *tmp = current;
  UINT intsts = 0;

  /* Disable interrupts to ensure atomic operations */
  DI(intsts);

  /* Remove the current task from the ready queue */
  tkmc_list_del(&tmp->head);

  /* Set the task state to "non-existent" */
  tmp->tskstat = TTS_NOEXS;

  /* Return the task to the free list */
  tkmc_list_add_tail(&tmp->head, &tkmc_free_tcb);

  /* Schedule the next task to be executed */
  next = tkmc_get_highest_priority_task();
  dispatch();

  /* Re-enable interrupts */
  EI(intsts);
}

/*
 * Release a waiting task.
 * - Moves the specified task from the TTS_WAI state to the TTS_RDY state.
 * - Removes the task from the waiting queue and adds it to the appropriate
 *   ready queue.
 * - Triggers a context switch if a higher-priority task is ready.
 * - Leaves the task's queued wakeup requests for its next tk_slp_tsk().
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
  if (!tkmc_id_in_range(tskid, CFN_MAX_TSKID)) {
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
    tkmc_complete_wait(tcb, E_RLWAI);

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
 * Wake up another task.
 * - Makes a task waiting in tk_slp_tsk ready without queuing a request.
 * - Otherwise queues one request for a ready, running or waiting task, up to
 *   TK_WAKEUP_MAXCNT. Other kinds of wait are not released.
 * - Queued requests are cleared only when the task is created or started, and
 *   used up one at a time by tk_slp_tsk(); there is no tk_can_wup().
 *
 * Parameters:
 * - tskid: ID of the task to wake up. TSK_SELF is not supported.
 *
 * Returns:
 * - E_OK on success.
 * - E_ID if the task ID is out of range, 0 included.
 * - E_NOEXS if the task does not exist.
 * - E_OBJ if the task is dormant, or is the running task and interrupts are
 *   enabled. Nothing changes. With interrupts disabled, as in a handler, the
 *   running task gets a request queued. Like the E_CTX check of tk_slp_tsk(),
 *   this cannot tell a handler from a task that called DI(), so such a task
 *   can still queue a request for itself.
 * - E_QOVR if the queued count is already TK_WAKEUP_MAXCNT. Nothing changes.
 */
ER tk_wup_tsk(ID tskid) {
  // Validate the task ID
  if (!tkmc_id_in_range(tskid, CFN_MAX_TSKID)) {
    return E_ID; // Invalid task ID
  }

  // Get the TCB for the specified task ID
  TCB *tcb = &tkmc_tcbs[tskid - 1];

  ER ercd = E_OK;
  UINT intsts = 0;

  // Disable interrupts to ensure atomic operations
  DI(intsts);

  // Check the task state
  UINT tskstat = tcb->tskstat;
  if (tcb == current && !isDI(intsts)) {
    ercd = E_OBJ; // A task cannot wake itself
  } else if (tskstat == TTS_WAI) {
    UINT tskwait = tcb->tskwait;
    if (tskwait == TTW_SLP) {
      // If the task is waiting to sleep, make it ready (normal wake-up)
      tkmc_complete_wait(tcb, E_OK);

      // Check if a higher-priority task needs to run
      next = tkmc_get_highest_priority_task();
      if (current != next) {
        dispatch(); // Perform a context switch
      }
    } else {
      // If the task is waiting on something else, increment the wakeup count
      if (tcb->wupcnt < TK_WAKEUP_MAXCNT) {
        tcb->wupcnt += 1;
        ercd = E_OK;
      } else {
        ercd = E_QOVR; // Wakeup count overflow
      }
    }
  } else if (tskstat == TTS_RDY || tskstat == TTS_RUN) {
    // If the task is already ready or running, increment the wakeup count
    if (tcb->wupcnt < TK_WAKEUP_MAXCNT) {
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
