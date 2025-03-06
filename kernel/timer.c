// clang-format off
#include <tk/tkernel.h>
// clang-format on
#include "timer.h"
#include "asm/rv32/address.h"
#include "list.h"
#include "task.h"

/* Timer queue to manage tasks waiting for a timer event */
tkmc_list_head tkmc_timer_queue;

/* Memory-mapped addresses for the CLINT (Core Local Interruptor) */
#define CLINT_MSIP_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MSIP_OFFSET)
#define CLINT_MTIME_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MTIME_OFFSET)
#define CLINT_MTIMECMP_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MTIMECMP_OFFSET)

/* Static variable to store the next timer compare value */
static UW s_mtimecmp;

/*
 * Initialize the timer system.
 * - Sets up the timer queue.
 * - Configures the initial timer compare value.
 */
void tkmc_start_timer(void) {
  tkmc_init_list_head(&tkmc_timer_queue);
  s_mtimecmp = *(_UW *)(CLINT_MTIME_ADDRESS);
  s_mtimecmp += 100000; // Set the initial timer compare value
  *(_UW *)(CLINT_MTIMECMP_ADDRESS) = s_mtimecmp;
}

/*
 * Timer interrupt handler.
 * - Updates the timer compare value to schedule the next interrupt.
 * - Checks the timer queue and moves tasks to the ready queue if their wait
 * time has expired.
 */
void tkmc_timer_handler(void) {
  UW mtime = *(_UW *)(CLINT_MTIME_ADDRESS);
  UW mtimecmp = s_mtimecmp;

  /* Update the timer compare value to schedule the next interrupt */
  while (mtime >= mtimecmp) {
    mtimecmp += 100000;
    mtime = *(_UW *)(CLINT_MTIME_ADDRESS);
  }
  *(_UW *)(CLINT_MTIMECMP_ADDRESS) = mtimecmp;
  s_mtimecmp = mtimecmp;

  /* Check the timer queue for tasks whose wait time has expired */
  if (!tkmc_list_empty(&tkmc_timer_queue)) {
    TCB *tcb, *n;
    tkmc_list_for_each_entry_safe(tcb, n, &tkmc_timer_queue, head) {
      tcb->tick_count -= 1;
      if (tcb->tick_count == 0) {
        /* Move the task to the ready queue */
        tkmc_list_del(&tcb->head);
        tkmc_list_add_tail(&tcb->head, &tkmc_ready_queue[tcb->itskpri - 1]);
        tcb->state = READY;

        /* Update the next task to be scheduled */
        TCB *tkmc_get_highest_priority_task(void);
        next = tkmc_get_highest_priority_task();
        if (next != current) {
          out_w(CLINT_MSIP_ADDRESS, 1); // Trigger a machine software interrupt
        }
      }
    }
  }
}

/*
 * Move a task to the timer queue.
 * - Sets the task's state to WAIT.
 * - Adds the task to the timer queue with the specified tick count.
 *
 * Parameters:
 * - tcb: Pointer to the Task Control Block (TCB) of the task.
 * - tick_count: Number of ticks to wait before the task is moved to the ready
 * queue.
 */
void tkmc_move_to_timer_queue(TCB *tcb, UINT tick_count) {
  UINT intsts;
  DI(intsts);
  tcb->state = WAIT;
  tcb->tick_count = tick_count;
  tkmc_list_del(&tcb->head);
  tkmc_list_add_tail(&tcb->head, &tkmc_timer_queue);

  /* Update the next task to be scheduled */
  TCB *tkmc_get_highest_priority_task(void);
  next = tkmc_get_highest_priority_task();
  out_w(CLINT_MSIP_ADDRESS, 1); // Trigger a machine software interrupt
  EI(intsts);
}

/*
 * Delay the current task for a specified timeout.
 * - Moves the current task to the timer queue if the timeout is non-zero.
 * - Yields the CPU if the timeout is zero.
 *
 * Parameters:
 * - tmout: Timeout value in milliseconds.
 *
 * Returns:
 * - E_OK on success.
 */
ER tk_dly_tsk(TMO tmout) {
  if (tmout == 0) {
    tkmc_yield();
    return E_OK;
  }
  ID tskid = current->tskid;

  TCB *tcb = &tkmc_tcbs[tskid - 1];

  /* Move the task to the timer queue with the specified timeout */
  tkmc_move_to_timer_queue(tcb, ((tmout + 9) / 10));
  return E_OK;
}