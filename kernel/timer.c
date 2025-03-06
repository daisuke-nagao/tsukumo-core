// clang-format off
#include "timer.h"
#include <tk/tkernel.h>
// clang-format on
#include "../putstring.h"
#include "asm/rv32/address.h"
#include "list.h"
#include "task.h"

tkmc_list_head tkmc_timer_queue;

#define CLINT_MSIP_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MSIP_OFFSET)
#define CLINT_MTIME_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MTIME_OFFSET)
#define CLINT_MTIMECMP_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MTIMECMP_OFFSET)

static UW s_mtimecmp;
void tkmc_start_timer(void) {
  tkmc_init_list_head(&tkmc_timer_queue);
  s_mtimecmp = *(_UW *)(CLINT_MTIME_ADDRESS);
  s_mtimecmp += 100000;
  *(_UW *)(CLINT_MTIMECMP_ADDRESS) = s_mtimecmp;
}

void tkmc_timer_handler(void) {
  UW mtime = *(_UW *)(CLINT_MTIME_ADDRESS);
  UW mtimecmp = s_mtimecmp;
  while (mtime >= mtimecmp) {
    mtimecmp += 100000;
    mtime = *(_UW *)(CLINT_MTIME_ADDRESS);
  }
  *(_UW *)(CLINT_MTIMECMP_ADDRESS) = mtimecmp;
  s_mtimecmp = mtimecmp;

  if (!tkmc_list_empty(&tkmc_timer_queue)) {
    TCB *tcb, *n;
    tkmc_list_for_each_entry_safe(tcb, n, &tkmc_timer_queue, head) {
      tcb->tick_count -= 1;
      if (tcb->tick_count == 0) {
        tkmc_list_del(&tcb->head);
        tkmc_list_add_tail(&tcb->head, &tkmc_ready_queue[tcb->itskpri - 1]);
        tcb->state = READY;

        TCB *tkmc_get_highest_priority_task(void);
        next = tkmc_get_highest_priority_task();
        if (next != current) {
          out_w(CLINT_MSIP_ADDRESS, 1);
        }
      }
    }
  }
}

void memset(void *ptr, int c, typeof(sizeof(int)) sz) {}

void tkmc_move_to_timer_queue(TCB *tcb, UINT tick_count) {
  UINT intsts;
  DI(intsts);
  tcb->state = WAIT;
  tcb->tick_count = tick_count;
  tkmc_list_del(&tcb->head);
  tkmc_list_add_tail(&tcb->head, &tkmc_timer_queue);

  TCB *tkmc_get_highest_priority_task(void);
  next = tkmc_get_highest_priority_task();
  out_w(CLINT_MSIP_ADDRESS, 1);
  EI(intsts);
}

ER tkmc_dly_tsk(TMO tmout) {
  if (tmout == 0) {
    tkmc_yield();
    return E_OK;
  }
  ID tskid = current->tskid;

  TCB *tcb = &tkmc_tcbs[tskid - 1];

  tkmc_move_to_timer_queue(tcb, ((tmout + 9) / 10));
  return E_OK;
}
