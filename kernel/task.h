/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01948452_C50C_7B25_A131_C2733EF5E067
#define UUID_01948452_C50C_7B25_A131_C2733EF5E067

#include <tk/tkernel.h>

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct EventFlagInfo {
  UINT waiptn;
  UINT wfmode;
  UINT flgptn;
} EventFlagInfo;

struct SEMCB;
typedef struct SemaphoreInfo {
  INT semcnt;
  struct SEMCB *semcb; // The semaphore waited for; a waiter that leaves it
                       // unserved must hand its place on to the next one.
} SemaphoreInfo;

struct T_MSG;
typedef struct MailboxInfo {
  struct T_MSG *msg;
} MailboxInfo;

typedef struct WaitInfo {
  tkmc_list_head wait_queue;
  union {
    EventFlagInfo event_flag;
    SemaphoreInfo semaphore;
    MailboxInfo mailbox;
  };
} WaitInfo;

/* Task Control Block */
typedef struct TCB {
  tkmc_list_head head;
  ID tskid;
  PRI itskpri;
  UINT tskstat;
  UW tskwait;
  void *sp;
  void *initial_sp;
  FP task;
  void *exinf;
  UINT delay_ticks;
  ER wupcause;
  UINT wupcnt;
  WaitInfo winfo;
} TCB;

extern TCB *current;

extern void tkmc_init_tcb(void);

extern void __launch_task(void **sp_end);
extern TCB *tkmc_get_highest_priority_task(void);

// Ends the wait of a task and makes it ready, with the given wake-up cause. It
// takes the task off the timer queue and off the wait queue of the object it
// waited for, clears what it waited for and its time limit, and puts it on its
// ready queue once. The caller owns the rest: what the wait was for (units of a
// semaphore, a flag pattern, a message), and the scheduling. One exception: a
// semaphore waiter that leaves with E_TMOUT or E_RLWAI took no units, so the
// waiters behind it are served here (tkmc_sem_serve_waiters()), and they may
// become ready too. Interrupts must be disabled.
extern void tkmc_complete_wait(TCB *tcb, ER cause);
extern void tkmc_yield(void);

extern TCB *current;
extern TCB *next;
extern tkmc_list_head tkmc_ready_queue[CFN_MAX_PRI];
extern TCB *current;
extern TCB *next;
extern TCB tkmc_tcbs[CFN_MAX_TSKID];

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01948452_C50C_7B25_A131_C2733EF5E067 */
