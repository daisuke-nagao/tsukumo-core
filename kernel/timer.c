/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// clang-format off
#include <tk/tkernel.h>
// clang-format on
#include "timer.h"
#include "asm/rv32/address.h"
#include "dispatch.h"
#include "list.h"
#include "task.h"

/* Timer queue to manage tasks waiting for a timer event */
tkmc_list_head tkmc_timer_queue;

/* Memory-mapped addresses for the CLINT (Core Local Interruptor) */
#define CLINT_MSIP_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MSIP_OFFSET)
#define CLINT_MTIME_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MTIME_OFFSET)
#define CLINT_MTIMECMP_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MTIMECMP_OFFSET)

/* Static variable to store the next timer compare value */
static UD s_mtimecmp;

static UD read_mtime(void) {
  UW high, low, next_high;
  do {
    high = in_w(CLINT_MTIME_ADDRESS + 4);
    low = in_w(CLINT_MTIME_ADDRESS);
    next_high = in_w(CLINT_MTIME_ADDRESS + 4);
  } while (high != next_high);
  return ((UD)high << 32) | low;
}

static void write_mtimecmp(UD value) {
  // Keep the intermediate RV32 comparand from falling below either endpoint.
  out_w(CLINT_MTIMECMP_ADDRESS, 0xffffffffu);
  out_w(CLINT_MTIMECMP_ADDRESS + 4, (UW)(value >> 32));
  out_w(CLINT_MTIMECMP_ADDRESS, (UW)value);
}

void tkmc_init_timer(void) { tkmc_init_list_head(&tkmc_timer_queue); }

/*
 * Initialize the timer system.
 * - Sets up the timer queue.
 * - Configures the initial timer compare value.
 */
void tkmc_start_timer(void) {
  UD mtimecmp = read_mtime();
  mtimecmp += 100000; // Set the initial timer compare value
  s_mtimecmp = mtimecmp;
  tkmc_compiler_barrier();
  write_mtimecmp(mtimecmp);
}

/*
 * Timer interrupt handler.
 * - Updates the timer compare value to schedule the next interrupt.
 * - Checks the timer queue and moves tasks to the ready queue if their wait
 * time has expired.
 */
void tkmc_timer_handler(void) {
  UD mtime = read_mtime();
  UD mtimecmp = s_mtimecmp;

  /* Update the timer compare value to schedule the next interrupt */
  while (mtime >= mtimecmp) {
    mtimecmp += 100000;
    mtime = read_mtime();
  }
  s_mtimecmp = mtimecmp;
  tkmc_compiler_barrier();
  write_mtimecmp(mtimecmp);

  /* Check the timer queue for tasks whose wait time has expired */
  if (!tkmc_list_empty(&tkmc_timer_queue)) {
    TCB *tcb, *n;
    tkmc_list_for_each_entry_safe(tcb, n, &tkmc_timer_queue, head) {
      tcb->delay_ticks -= 1;
      if (tcb->delay_ticks == 0) {
        /* The wait is over: out of the timer queue and, if the task waited
         * for an object, out of its wait queue too. */
        tkmc_complete_wait(tcb, E_TMOUT);

        /* Update the next task to be scheduled */
        next = tkmc_get_highest_priority_task();
        if (next != current) {
          dispatch();
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
void tkmc_schedule_timer(TCB *tcb, UINT delay_ticks, UINT tskwait) {
  tcb->tskstat = TTS_WAI;
  tcb->tskwait = tskwait;
  tcb->delay_ticks = delay_ticks;
  tkmc_list_del(&tcb->head);
  tkmc_list_add_tail(&tcb->head, &tkmc_timer_queue);

  /* Update the next task to be scheduled */
  next = tkmc_get_highest_priority_task();
  dispatch();
}

/*
 * Delay the current task for a specified timeout.
 * - Moves the current task to the timer queue if the timeout is non-zero.
 * - Yields the CPU if the timeout is zero.
 *
 * Parameters:
 * - dlytm: 0, or a delay of 1 to INT_MAX ms, counted as in tk_slp_tsk().
 *
 * Returns:
 * - E_OK on success.
 * - E_PAR if dlytm is negative.
 * - E_CTX if interrupts are disabled (in an interrupt handler or between DI()
 *   and EI()), even for dlytm == 0. Nothing is changed. See tk_slp_tsk().
 */
ER tk_dly_tsk(TMO dlytm) {
  if (dlytm < 0) {
    return E_PAR;
  }

  UINT intsts;
  DI(intsts);
  if (isDI(intsts)) {
    EI(intsts);
    return E_CTX;
  }
  if (dlytm == 0) {
    EI(intsts);
    tkmc_yield();
    return E_OK;
  }

  /* Move the task to the timer queue with the specified timeout */
  tkmc_schedule_timer(current, tkmc_ms_to_ticks(dlytm), TTW_DLY);
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

/*
 * Put the current task to sleep until it is woken up or tmout runs out.
 *
 * Parameters:
 * - tmout: TMO_POL, TMO_FEVR, or a time limit of 1 to INT_MAX ms (about 24.9
 *   days). A time limit is counted in 10 ms ticks: rounded up, plus one tick,
 *   since the first tick comes at any time after the sleep begins. The other
 *   wait calls count theirs the same way.
 *
 * Returns:
 * - E_OK if the task was woken up or a wakeup request was queued.
 * - E_TMOUT if tmout ran out, or at once for TMO_POL without a queued wakeup.
 * - E_PAR if tmout is below TMO_FEVR.
 * - E_CTX if interrupts are disabled, even when a wakeup is queued. Nothing is
 *   changed. The task is switched out when interrupts come back on, so with
 *   them off it would stay in the waiting state while it runs. Checking MIE
 *   does not see a handler that enables interrupts again; that needs a counter
 *   of handler nesting, which the kernel does not keep.
 */
ER tk_slp_tsk(TMO tmout) {
  if (tmout < TMO_FEVR) {
    return E_PAR;
  }

  UINT intsts;
  ER ercd = E_OK;
  DI(intsts);
  if (isDI(intsts)) {
    EI(intsts);
    return E_CTX;
  }
  if (current->wupcnt > 0) {
    current->wupcnt -= 1;
    ercd = E_OK;
    EI(intsts);
    return ercd;
  } else {
    if (tmout > 0) {
      tkmc_schedule_timer(current, tkmc_ms_to_ticks(tmout), TTW_SLP);
    } else if (tmout == TMO_POL) {
      EI(intsts);
      return E_TMOUT;
    } else {
      current->tskstat = TTS_WAI;
      current->tskwait = TTW_SLP;
      current->delay_ticks = 0;
      tkmc_list_del(&current->head);
      tkmc_init_list_head(&current->head);
      next = tkmc_get_highest_priority_task();
      dispatch();
    }
    EI(intsts);
    // wait to be awaken
    DI(intsts);
    ercd = ((volatile TCB *)current)->wupcause;
    current->wupcause = E_OK;
    EI(intsts);
  }

  return ercd;
}
