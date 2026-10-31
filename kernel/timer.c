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
#include "context.h"
#include "dispatch.h"
#include "list.h"
#include "task.h"

/* Timer queue to manage tasks waiting for a timer event */
tkmc_list_head tkmc_timer_queue;

/* Memory-mapped addresses for the CLINT (Core Local Interruptor) */
#define CLINT_MSIP_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MSIP_OFFSET)
#define CLINT_MTIME_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MTIME_OFFSET)
#define CLINT_MTIMECMP_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MTIMECMP_OFFSET)

/* One kernel tick (10 ms) in machine timer counts (10 MHz) */
enum { TICK_COUNTS = 100000 };

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

/*
 * Count the tick edges from *mtimecmp up to the machine time, and move
 * *mtimecmp to the first edge still ahead. Counted one by one: a 64 bit
 * division needs a library the kernel does not link, and the count is
 * normally 1.
 */
static UINT pass_elapsed_periods(UD *mtimecmp) {
  UINT periods = 0;
  while (read_mtime() >= *mtimecmp) {
    *mtimecmp += TICK_COUNTS;
    periods += 1;
  }
  return periods;
}

void tkmc_init_timer(void) { tkmc_init_list_head(&tkmc_timer_queue); }

/*
 * Initialize the timer system.
 * - Sets up the timer queue.
 * - Configures the initial timer compare value.
 */
void tkmc_start_timer(void) {
  UD mtimecmp = read_mtime();
  mtimecmp += TICK_COUNTS; // Set the initial timer compare value
  s_mtimecmp = mtimecmp;
  tkmc_compiler_barrier();
  write_mtimecmp(mtimecmp);
}

/* The earliest deadline, in queue order for ties, or NULL. */
static TCB *first_deadline(void) {
  TCB *first = NULL;
  TCB *tcb;
  tkmc_list_for_each_entry(tcb, &tkmc_timer_queue, head) {
    if (first == NULL || tcb->delay_ticks < first->delay_ticks) {
      first = tcb;
    }
  }
  return first;
}

/*
 * Timer interrupt handler.
 * - Counts the tick edges that have passed since the last compare value, and
 *   sets the compare value to the next edge ahead. An interrupt that came late
 *   counts every edge it missed.
 * - Takes that count off the wait of every task on the timer queue, and makes
 *   ready each task whose wait it reaches.
 * - Does nothing if no edge has passed, as for the extra interrupt that can
 *   come when MTIP is cleared late after a compare write.
 */
void tkmc_timer_handler(void) {
  UD mtimecmp = s_mtimecmp;
  UINT periods = pass_elapsed_periods(&mtimecmp);
  if (periods == 0) {
    return;
  }
  s_mtimecmp = mtimecmp;
  tkmc_compiler_barrier();
  write_mtimecmp(mtimecmp);

  /* Advance only to the next deadline before completing a wait: an earlier
   * timeout can hand semaphore units to a waiter with a later deadline.
   * Re-scan after each completion, which can remove other timer entries too.
   * shortcut: these scans are O(n^2), bounded by CFN_MAX_TSKID (32); revisit
   * if the measured interrupt latency requires it. */
  BOOL ended = FALSE;
  TCB *first;
  while ((first = first_deadline()) != NULL) {
    const UINT ticks = first->delay_ticks;
    const UINT step = ticks < periods ? ticks : periods;
    TCB *tcb;
    tkmc_list_for_each_entry(tcb, &tkmc_timer_queue, head) {
      tcb->delay_ticks -= step;
    }
    periods -= step;
    if (ticks > step) {
      break;
    }
    tkmc_complete_wait(first, E_TMOUT);
    ended = TRUE;
  }
  if (ended) {
    next = tkmc_get_highest_priority_task();
    if (next != current) {
      dispatch();
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
 *
 * Interrupts must be disabled. Edges that have passed while the timer interrupt
 * is pending are added to the count: the handler takes them off every waiting
 * task when it runs, and they came before this wait began.
 */
void tkmc_schedule_timer(TCB *tcb, UINT delay_ticks, UINT tskwait) {
  UD mtimecmp = s_mtimecmp;
  tcb->tskstat = TTS_WAI;
  tcb->tskwait = tskwait;
  tcb->delay_ticks = delay_ticks + pass_elapsed_periods(&mtimecmp);
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
 * - dlytm: 0, or a delay of 1 to UINT_MAX ms (about 49.7 days), counted as in
 *   tk_slp_tsk(). RELTIM is unsigned, so there is no invalid value.
 *
 * Returns:
 * - E_OK when the delay has passed.
 * - E_RLWAI if tk_rel_wai() ended the delay early.
 * - E_CTX if interrupts are disabled, no current task exists, or execution is
 *   in an ISR, even for dlytm == 0. Nothing is changed. See tk_slp_tsk().
 */
ER tk_dly_tsk(RELTIM dlytm) {
  UINT intsts;
  DI(intsts);
  if (!tkmc_can_block(intsts)) {
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
 * A queued wakeup request, if any, is used up instead of sleeping, one per
 * call; nothing else in the kernel uses one up.
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
 * - E_CTX if interrupts are disabled, no current task exists, or execution is
 *   in an ISR, even when a wakeup is queued. Nothing is changed. ISR state is
 *   recorded by the trap entry independently of MIE. Only single-level ISR
 *   execution with interrupts kept disabled is supported.
 */
ER tk_slp_tsk(TMO tmout) {
  if (tmout < TMO_FEVR) {
    return E_PAR;
  }

  UINT intsts;
  ER ercd = E_OK;
  DI(intsts);
  if (!tkmc_can_block(intsts)) {
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
