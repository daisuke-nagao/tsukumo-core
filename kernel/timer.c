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

void tkmc_init_timer(void) { tkmc_init_list_head(&tkmc_timer_queue); }

/*
 * Initialize the timer system.
 * - Sets up the timer queue.
 * - Configures the initial timer compare value.
 */
void tkmc_start_timer(void) {
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
      tcb->delay_ticks -= 1;
      if (tcb->delay_ticks == 0) {
        /* Move the task to the ready queue */
        tkmc_list_del(&tcb->head);
        tkmc_list_add_tail(&tcb->head, &tkmc_ready_queue[tcb->itskpri - 1]);
        tcb->tskstat = TTS_RDY;
        tcb->wupcause = E_TMOUT;

        /* Update the next task to be scheduled */
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
 * - Sets the task's state to TTS_WAI.
 * - Adds the task to the timer queue with the specified tick count.
 *
 * Parameters:
 * - tcb: Pointer to the Task Control Block (TCB) of the task.
 * - delay_ticks: Number of ticks to wait before the task is moved to the ready
 * queue.
 */
static void schedule_timer(TCB *tcb, UINT delay_ticks, enum TaskWait tskwait) {
  tcb->tskstat = TTS_WAI;
  tcb->tskwait = tskwait;
  tcb->delay_ticks = delay_ticks;
  tkmc_list_del(&tcb->head);
  tkmc_list_add_tail(&tcb->head, &tkmc_timer_queue);

  /* Update the next task to be scheduled */
  next = tkmc_get_highest_priority_task();
  out_w(CLINT_MSIP_ADDRESS, 1); // Trigger a machine software interrupt
}

/*
 * Delay the current task for a specified timeout.
 * - Moves the current task to the timer queue if the timeout is non-zero.
 * - Yields the CPU if the timeout is zero.
 *
 * Parameters:
 * - dlytm: Timeout value in milliseconds.
 *
 * Returns:
 * - E_OK on success.
 */
ER tk_dly_tsk(TMO dlytm) {
  if (dlytm == 0) {
    tkmc_yield();
    return E_OK;
  } else if (dlytm < 0) {
    return E_PAR;
  }

  /* Move the task to the timer queue with the specified timeout */
  UINT intsts;
  DI(intsts);
  schedule_timer(current, ((dlytm + 9) / 10) + 1, TTW_DLY);
  EI(intsts);
  // wait to be awaken
  DI(intsts);
  ER ercd = ((volatile TCB *)current)->wupcause;
  current->wupcause = E_OK;
  EI(intsts);
  if (ercd == E_TMOUT) {
    ercd = E_OK;
  }
  return ercd;
}

ER tk_slp_tsk(TMO tmout) {
  if (tmout < TMO_FEVR) {
    return E_PAR;
  }

  return E_OK;
}
