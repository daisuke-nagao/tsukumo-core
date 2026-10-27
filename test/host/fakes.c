/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "fakes.h"

#include <unity.h>

#include "asm/rv32/address.h"
#include "event_flag.h"
#include "mailbox.h"
#include "semaphore.h"
#include "task.h"
#include "timer.h"

host_access_t host_log[HOST_LOG_MAX];
UINT host_log_count;
UD host_mtime;
UD host_mtime_advance_after_low_read;

jmp_buf host_wait_jmp;
volatile int host_wait_armed;
volatile int host_waited;

static int s_interrupts_enabled;
static UINT s_stacks_used;
static UD s_msip;
static UD s_mtimecmp_low;
static UD s_mtimecmp_high;

// The only addresses the kernel has a reason to touch. Anything else is a wild
// access, and fails the running test.
enum {
  MSIP = CLINT_BASE_ADDRESS + CLINT_MSIP_OFFSET,
  MTIMECMP_LOW = CLINT_BASE_ADDRESS + CLINT_MTIMECMP_OFFSET,
  MTIMECMP_HIGH = MTIMECMP_LOW + 4,
  MTIME_LOW = CLINT_BASE_ADDRESS + CLINT_MTIME_OFFSET,
  MTIME_HIGH = MTIME_LOW + 4,
};

static void record(INT port, UINT width, UD value, BOOL is_write) {
  TEST_ASSERT_LESS_THAN_UINT_MESSAGE(HOST_LOG_MAX, host_log_count,
                                     "the access log is full");
  host_log[host_log_count++] = (host_access_t){port, width, value, is_write};
}

void host_out(INT port, UINT width, UD data) {
  record(port, width, data, TRUE);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(4, width, "device registers are word wide");
  switch (port) {
  case MSIP:
    s_msip = data;
    break;
  case MTIMECMP_LOW:
    s_mtimecmp_low = data;
    break;
  case MTIMECMP_HIGH:
    s_mtimecmp_high = data;
    break;
  default:
    TEST_FAIL_MESSAGE("the kernel wrote to an address the fake machine does "
                      "not have");
  }
}

UD host_in(INT port, UINT width) {
  UD value = 0;
  switch (port) {
  case MSIP:
    value = s_msip;
    break;
  case MTIMECMP_LOW:
    value = s_mtimecmp_low;
    break;
  case MTIMECMP_HIGH:
    value = s_mtimecmp_high;
    break;
  case MTIME_LOW:
    value = (UW)host_mtime;
    host_mtime += host_mtime_advance_after_low_read;
    host_mtime_advance_after_low_read = 0;
    break;
  case MTIME_HIGH:
    value = (UW)(host_mtime >> 32);
    break;
  default:
    TEST_FAIL_MESSAGE("the kernel read from an address the fake machine does "
                      "not have");
  }
  record(port, width, value, FALSE);
  return value;
}

// Same contract as the DI()/EI() of the real header: DI returns a value that
// EI uses to put the previous state back, and EI never disables interrupts.
UINT host_di(void) {
  UINT before = s_interrupts_enabled ? MSTATUS_MIE : 0;
  s_interrupts_enabled = 0;
  return before | ~MSTATUS_MIE;
}

void host_ei(UINT intsts) {
  if (intsts == 0 || (intsts & MSTATUS_MIE) != 0) {
    s_interrupts_enabled = 1;
  }
  // On the target, the task that has just put itself to sleep is switched out
  // right here: it asked for a switch (the software interrupt is pending) and
  // interrupts come back on. Abandon the call instead, and take the interrupt
  // as the trap would.
  if (host_wait_armed && s_interrupts_enabled && s_msip != 0 &&
      current != NULL && current->tskstat == TTS_WAI) {
    host_wait_armed = 0;
    s_msip = 0;
    longjmp(host_wait_jmp, 1);
  }
}

BOOL host_interrupts_enabled(void) { return s_interrupts_enabled != 0; }

// How many times the node is on the ring that starts at head.
static UINT count_in_ring(const tkmc_list_head *head,
                          const tkmc_list_head *node) {
  UINT count = 0;
  for (const tkmc_list_head *pos = head->next; pos != head; pos = pos->next) {
    if (pos == node) {
      ++count;
    }
  }
  return count;
}

// The ring is well formed when every link is matched by the link back, and the
// walk comes round to the head within the number of tasks there can be.
static void check_ring(const tkmc_list_head *head) {
  UINT length = 0;
  for (const tkmc_list_head *pos = head->next; pos != head; pos = pos->next) {
    TEST_ASSERT_EQUAL_PTR_MESSAGE(pos, pos->next->prev, "broken prev link");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(pos, pos->prev->next, "broken next link");
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
        CFN_MAX_TSKID, ++length, "a queue never gets back to its head");
  }
}

extern SEMCB tkmc_semcbs[CFN_MAX_SEMID];
extern FLGCB tkmc_flgcbs[CFN_MAX_FLGID];
extern MBXCB tkmc_mbxcbs[CFN_MAX_MBXID];

// Same as the kernel's NOEXS_MASK: the top bit of an object's ID says it does
// not exist, and then its wait_queue is a link of the free list, not a queue.
enum { OBJECT_NOEXS = 0x80000000u };

// Everyone on the wait queue of an object is waiting for that kind of object.
static void check_wait_queue(const tkmc_list_head *queue, UW kind) {
  check_ring(queue);
  for (const tkmc_list_head *pos = queue->next; pos != queue; pos = pos->next) {
    const TCB *tcb =
        tkmc_list_entry((tkmc_list_head *)pos, TCB, winfo.wait_queue);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_WAI, tcb->tskstat,
                                   "a task on a wait queue is not waiting");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        kind, tcb->tskwait,
        "a task waits on a queue of another kind of object");
  }
}

void host_check_kernel_queues(void) {
  for (UINT prio = 0; prio < CFN_MAX_PRI; ++prio) {
    check_ring(&tkmc_ready_queue[prio]);
  }
  check_ring(&tkmc_timer_queue);
  for (UINT i = 0; i < CFN_MAX_SEMID; ++i) {
    if ((tkmc_semcbs[i].semid & OBJECT_NOEXS) == 0) {
      check_wait_queue(&tkmc_semcbs[i].wait_queue, TTW_SEM);
    }
  }
  for (UINT i = 0; i < CFN_MAX_FLGID; ++i) {
    if ((tkmc_flgcbs[i].flgid & OBJECT_NOEXS) == 0) {
      check_wait_queue(&tkmc_flgcbs[i].wait_queue, TTW_FLG);
    }
  }
  for (UINT i = 0; i < CFN_MAX_MBXID; ++i) {
    if ((tkmc_mbxcbs[i].mbxid & OBJECT_NOEXS) == 0) {
      check_wait_queue(&tkmc_mbxcbs[i].wait_queue, TTW_MBX);
    }
  }

  for (UINT i = 0; i < CFN_MAX_TSKID; ++i) {
    const TCB *tcb = &tkmc_tcbs[i];
    UINT on_ready = 0;
    for (UINT prio = 0; prio < CFN_MAX_PRI; ++prio) {
      UINT count = count_in_ring(&tkmc_ready_queue[prio], &tcb->head);
      on_ready += count;
      TEST_ASSERT_TRUE_MESSAGE(
          count == 0 || (UINT)tcb->itskpri == prio + 1,
          "a task is on the ready queue of another priority");
    }
    UINT on_timer = count_in_ring(&tkmc_timer_queue, &tcb->head);
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(1, on_ready + on_timer,
                                           "a task is on more than one queue");

    if (tcb->tskstat == TTS_RDY || tcb->tskstat == TTS_RUN) {
      TEST_ASSERT_EQUAL_UINT_MESSAGE(1, on_ready,
                                     "a ready task is not on a ready queue");
      TEST_ASSERT_EQUAL_UINT_MESSAGE(
          0, tcb->delay_ticks + tcb->tskwait,
          "a ready task still has a time limit or something it waits for");
    } else if (tcb->tskstat == TTS_WAI) {
      TEST_ASSERT_EQUAL_UINT_MESSAGE(0, on_ready,
                                     "a waiting task is on a ready queue");
      TEST_ASSERT_EQUAL_UINT_MESSAGE(
          tcb->delay_ticks > 0, on_timer,
          "a waiting task is on the timer queue only with a time limit");
    } else {
      TEST_ASSERT_EQUAL_UINT_MESSAGE(0, on_ready + on_timer,
                                     "a task that is neither ready nor "
                                     "waiting is on a queue");
    }
  }
}

void host_reset(void) {
  host_log_count = 0;
  s_interrupts_enabled = 1;
  s_msip = s_mtimecmp_low = s_mtimecmp_high = 0;
  host_mtime = 0;
  host_mtime_advance_after_low_read = 0;
  host_wait_armed = 0;
  host_waited = 0;
  s_stacks_used =
      0; // The kernel tables are reset below, so every task is gone.

  tkmc_init_tcb();
  tkmc_init_flgcb();
  tkmc_init_semcb();
  tkmc_init_mbxcb();
  tkmc_init_timer();
  tkmc_start_timer(); // Sets the first timer compare value, as the kernel does
                      // at boot.
  current = NULL;
  next = NULL;

  // The log starts empty after the kernel has been set up.
  host_log_count = 0;
}

enum { STACK_WORDS = 256, MAX_TASKS = 32 };

static UW s_stacks[MAX_TASKS][STACK_WORDS] __attribute__((aligned(16)));

static void never_run(INT stacd, void *exinf) {
  (void)stacd;
  (void)exinf;
}

ID host_make_task(PRI prio) {
  TEST_ASSERT_LESS_THAN_UINT(MAX_TASKS, s_stacks_used);
  ID tskid = tk_cre_tsk(&(T_CTSK){.exinf = 0,
                                  .tskatr = TA_HLNG | TA_USERBUF,
                                  .task = (FP)never_run,
                                  .itskpri = prio,
                                  .stksz = sizeof(s_stacks[0]),
                                  .bufptr = s_stacks[s_stacks_used++]});
  TEST_ASSERT_GREATER_THAN(0, tskid);
  TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(tskid, 0));
  return tskid;
}

extern void tkmc_timer_handler(void);

void host_tick(UINT ticks) {
  for (UINT i = 0; i < ticks; ++i) {
    host_mtime += HOST_TICK;
    tkmc_timer_handler();
  }
}

void host_set_current(ID tskid) {
  current = &tkmc_tcbs[tskid - 1];
  current->tskstat = TTS_RUN;
  next = current;
}

UINT host_write_count(INT port) {
  UINT count = 0;
  for (UINT i = 0; i < host_log_count; ++i) {
    if (host_log[i].is_write && host_log[i].port == port) {
      ++count;
    }
  }
  return count;
}

UD host_last_write(INT port) {
  for (UINT i = host_log_count; i > 0; --i) {
    if (host_log[i - 1].is_write && host_log[i - 1].port == port) {
      return host_log[i - 1].value;
    }
  }
  TEST_FAIL_MESSAGE("no write to the port in the log");
  return 0;
}
