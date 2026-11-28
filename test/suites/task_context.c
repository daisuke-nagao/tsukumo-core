/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "context.h"
#include "dispatch.h"
#include "event_flag.h"
#include "finish.h"
#include "mailbox.h"
#include "panic.h"
#include "putstring.h"
#include "qemu_virt.h"
#include "runner.h"
#include "semaphore.h"

// The modes, by the name CMakeLists.txt passes as CONTEXT_TEST_MODE. Macros,
// not an enum, so that #if can test them. The order matters: the checks below
// compare against RET_TASK and RET_NO_CURRENT, and an odd mode ends with
// tk_exd_tsk().
#define LIVE_ISR 0
#define NO_CURRENT_WAIT 1
#define DI_EXT 2
#define DI_EXD 3
#define ISR_EXT 4
#define ISR_EXD 5
#define NO_CURRENT_EXT 6
#define NO_CURRENT_EXD 7
#define RET_TASK 8
#define RET_TASK_DI 9
#define RET_HLNG 10
#define RET_TIMER 11
#define RET_SOFTWARE 12
#define RET_ASM_TASK_DI 13
#define RET_NO_CURRENT 14
_Static_assert(CONTEXT_TEST_MODE >= LIVE_ISR &&
                   CONTEXT_TEST_MODE <= RET_NO_CURRENT,
               "unknown context fixture");

extern SEMCB tkmc_semcbs[CFN_MAX_SEMID];
extern FLGCB tkmc_flgcbs[CFN_MAX_FLGID];
extern MBXCB tkmc_mbxcbs[CFN_MAX_MBXID];
extern tkmc_list_head tkmc_timer_queue;
extern ID __real_tk_cre_tsk(CONST T_CTSK *packet);
extern void __real_tkmc_timer_handler(void);
__typeof__(schedule) __real_schedule, __wrap_schedule;
#if CONTEXT_TEST_MODE == RET_ASM_TASK_DI
extern void asm_task_di_ret_entry(void);
#endif

enum { MSIP = CLINT_BASE_ADDRESS + CLINT_MSIP_OFFSET, SENTINEL = 0x12345678 };
// The longest a case spins for an event that a trap or another task brings.
enum { SPIN_MS = 2000 };
enum {
  // The child of the ISR case runs as soon as it starts.
  CHILD_PRIORITY = 2,
  // The task that the ISR probe creates and never starts.
  PROBE_PRIORITY = 3,
  // The TA_ASM task of the RET_ASM_TASK_DI run, which usermain starts; it
  // ranks below the initial task, of priority 1, so it runs once usermain
  // sleeps.
  ASM_TASK_PRIORITY = 2,
};
_Static_assert(TEST_RUNNER_PRIORITY < CHILD_PRIORITY &&
                   CHILD_PRIORITY < TEST_CASE_PRIORITY,
               "the child must outrank the case and not the runner");
static struct {
  TCB tasks[CFN_MAX_TSKID];
  tkmc_list_head ready[CFN_MAX_PRI], timer;
  SEMCB sems[CFN_MAX_SEMID];
  FLGCB flags[CFN_MAX_FLGID];
  MBXCB mailboxes[CFN_MAX_MBXID];
  TCB *running, *scheduled;
  UINT isr;
  UW msip;
} snapshot;

static ID semid, flgid, mbxid, gate;
static T_MSG message, sentinel;
static volatile ID probe_task, probe_sem, probe_flag, probe_mbx,
    probe_deleted_mbx;
static T_MSG probe_message;
static volatile BOOL armed, observed, child_done, child_in_isr;
static volatile ER child_result, notified, flagged, probe_sem_deleted,
    probe_mbx_deleted, probe_flag_cleared, probe_message_sent;
static volatile UINT dispatches;
static volatile BOOL quiet_armed, quiet_seen;
static volatile UINT quiet_dispatches;
static TCB *expected_current;
static BOOL startup_probed;

static __attribute__((__noreturn__)) void fail(const char *reason) {
  test_fail("TASK CONTEXT :FAIL: ", reason);
}

static void capture(void) {
  test_copy_bytes(snapshot.tasks, tkmc_tcbs, sizeof(snapshot.tasks));
  test_copy_bytes(snapshot.ready, tkmc_ready_queue, sizeof(snapshot.ready));
  test_copy_bytes(&snapshot.timer, &tkmc_timer_queue, sizeof(snapshot.timer));
  test_copy_bytes(snapshot.sems, tkmc_semcbs, sizeof(snapshot.sems));
  test_copy_bytes(snapshot.flags, tkmc_flgcbs, sizeof(snapshot.flags));
  test_copy_bytes(snapshot.mailboxes, tkmc_mbxcbs, sizeof(snapshot.mailboxes));
  snapshot.running = current;
  snapshot.scheduled = next;
  snapshot.isr = tkmc_isr_depth;
  snapshot.msip = in_w(MSIP);
}

static BOOL unchanged(void) {
  return test_equal_bytes(snapshot.tasks, tkmc_tcbs, sizeof(snapshot.tasks)) &&
         test_equal_bytes(snapshot.ready, tkmc_ready_queue,
                          sizeof(snapshot.ready)) &&
         test_equal_bytes(&snapshot.timer, &tkmc_timer_queue,
                          sizeof(snapshot.timer)) &&
         test_equal_bytes(snapshot.sems, tkmc_semcbs, sizeof(snapshot.sems)) &&
         test_equal_bytes(snapshot.flags, tkmc_flgcbs,
                          sizeof(snapshot.flags)) &&
         test_equal_bytes(snapshot.mailboxes, tkmc_mbxcbs,
                          sizeof(snapshot.mailboxes)) &&
         snapshot.running == current && snapshot.scheduled == next &&
         snapshot.isr == tkmc_isr_depth && snapshot.msip == in_w(MSIP);
}

static void make_objects(void) {
  semid = tk_cre_sem(&(T_CSEM){.maxsem = 1, .isemcnt = 1});
  flgid = tk_cre_flg(&(T_CFLG){.flgatr = TA_WSGL, .iflgptn = 1});
  mbxid = tk_cre_mbx(&(T_CMBX){.mbxatr = TA_TFIFO});
  if (semid <= 0 || flgid <= 0 || mbxid <= 0 ||
      tk_snd_mbx(mbxid, &message) != E_OK) {
    fail("object setup");
  }
}

static void delete_objects(void) {
  ER sem_deleted = tk_del_sem(semid);
  ER mbx_deleted = tk_del_mbx(mbxid);
  if (sem_deleted != E_OK) {
    fail("could not delete the fixture semaphore");
  }
  if (mbx_deleted != E_OK) {
    fail("could not delete the fixture mailbox");
  }
}

// Sources are masked before probing with MIE on; this never nests a trap.
static void probe_waits(void) {
  UINT saved_sources;
  __asm__ volatile("csrrw %0, mie, zero" : "=r"(saved_sources)::"memory");
  static const TMO limits[] = {TMO_POL, 50, TMO_FEVR};
  for (UINT irq = 0; irq < 2; ++irq) {
    if (irq) {
      EI(0);
    }
    for (UINT limit = 0; limit < 3; ++limit) {
      for (UINT api = 0; api < 5; ++api) {
        UINT pattern = SENTINEL;
        T_MSG *received = &sentinel;
        capture();
        ER result;
        switch (api) {
        case 0:
          result = tk_dly_tsk((RELTIM)limits[limit]);
          break;
        case 1:
          result = tk_slp_tsk(limits[limit]);
          break;
        case 2:
          result = tk_wai_sem(semid, 1, limits[limit]);
          break;
        case 3:
          result =
              tk_wai_flg(flgid, 1, TWF_ORW | TWF_CLR, &pattern, limits[limit]);
          break;
        default:
          result = tk_rcv_mbx(mbxid, &received, limits[limit]);
          break;
        }
        if (result != E_CTX || test_irq_enabled() != (BOOL)irq ||
            pattern != SENTINEL || received != &sentinel || !unchanged()) {
          fail("wait refusal changed state, output or interrupt status");
        }
      }
    }
    UINT ignored;
    DI(ignored);
  }
  __asm__ volatile("csrw mie, %0" ::"r"(saved_sources) : "memory");
}

static void never_run(INT stacd, void *exinf) {
  (void)stacd;
  (void)exinf;
}

static void check_isr_call(ER result, BOOL returns_id, const char *reason) {
  if ((returns_id ? result <= E_OK : result != E_OK) || test_irq_enabled()) {
    fail(reason);
  }
}

static void probe_compatible_calls(void) {
  static UW probe_stack[256] __attribute__((aligned(16)));
  probe_task = tk_cre_tsk(&(T_CTSK){.tskatr = TA_HLNG | TA_USERBUF,
                                    .task = (FP)never_run,
                                    .itskpri = PROBE_PRIORITY,
                                    .stksz = sizeof(probe_stack),
                                    .bufptr = probe_stack});
  check_isr_call(probe_task, TRUE, "tk_cre_tsk failed or changed IRQ state");

  probe_sem = tk_cre_sem(&(T_CSEM){.maxsem = 1});
  check_isr_call(probe_sem, TRUE, "tk_cre_sem failed or changed IRQ state");

  probe_flag = tk_cre_flg(&(T_CFLG){.flgatr = TA_WSGL, .iflgptn = 1});
  check_isr_call(probe_flag, TRUE, "tk_cre_flg failed or changed IRQ state");

  probe_mbx = tk_cre_mbx(&(T_CMBX){.mbxatr = TA_TFIFO});
  check_isr_call(probe_mbx, TRUE, "tk_cre_mbx failed or changed IRQ state");

  probe_deleted_mbx = tk_cre_mbx(&(T_CMBX){.mbxatr = TA_TFIFO});
  check_isr_call(probe_deleted_mbx, TRUE,
                 "second tk_cre_mbx failed or changed IRQ state");

  probe_flag_cleared = tk_clr_flg(probe_flag, 0);
  check_isr_call(probe_flag_cleared, FALSE,
                 "tk_clr_flg failed or changed IRQ state");

  probe_sem_deleted = tk_del_sem(probe_sem);
  check_isr_call(probe_sem_deleted, FALSE,
                 "tk_del_sem failed or changed IRQ state");

  probe_mbx_deleted = tk_del_mbx(probe_deleted_mbx);
  check_isr_call(probe_mbx_deleted, FALSE,
                 "tk_del_mbx failed or changed IRQ state");

  probe_message_sent = tk_snd_mbx(probe_mbx, &probe_message);
  check_isr_call(probe_message_sent, FALSE,
                 "tk_snd_mbx failed or changed IRQ state");
}

static __attribute__((__noreturn__)) void terminate_invalid(void) {
  capture();
  if (CONTEXT_TEST_MODE == DI_EXD || CONTEXT_TEST_MODE == ISR_EXD ||
      CONTEXT_TEST_MODE == NO_CURRENT_EXD) {
    tk_exd_tsk();
  } else {
    tk_ext_tsk();
  }
  fail("invalid termination returned");
}

__attribute__((__noreturn__)) void tkmc_context_panic(tkmc_context_api api) {
  if (CONTEXT_TEST_MODE >= RET_TASK) {
    tkmc_context_api expected = CONTEXT_TEST_MODE == RET_ASM_TASK_DI
                                    ? TKMC_CONTEXT_EXT_TSK
                                    : TKMC_CONTEXT_RET_INT;
    if (api != expected || tkmc_last_context_failure.api != expected ||
        tkmc_last_context_failure.error != E_CTX || test_irq_enabled() ||
        !unchanged()) {
      fail("invalid return context failure or state mismatch");
    }
    putstring("CONTEXT STOP OK\n");
    test_exit(0);
  }
  tkmc_context_api expected =
      CONTEXT_TEST_MODE % 2 ? TKMC_CONTEXT_EXD_TSK : TKMC_CONTEXT_EXT_TSK;
  if (CONTEXT_TEST_MODE < DI_EXT || api != expected ||
      tkmc_last_context_failure.api != expected ||
      tkmc_last_context_failure.error != E_CTX || test_irq_enabled() ||
      !unchanged()) {
    fail("wrong context failure or task state changed before stop");
  }
  putstring("CONTEXT STOP OK\n");
  test_exit(0);
}

void asm_task_di_ret_capture(void) { capture(); }

__attribute__((__noreturn__)) void tkmc_panic(UW cause, UW epc, UW value) {
  (void)cause;
  (void)epc;
  (void)value;
  fail("unexpected CPU exception");
}

ID __wrap_tk_cre_tsk(CONST T_CTSK *packet) {
  if (!startup_probed) {
    startup_probed = TRUE;
    if (CONTEXT_TEST_MODE == NO_CURRENT_WAIT) {
      make_objects();
      probe_waits();
      observed = TRUE;
    } else if (CONTEXT_TEST_MODE >= RET_NO_CURRENT) {
      capture();
      tk_ret_int();
      fail("tk_ret_int returned before current task existed");
    } else if (CONTEXT_TEST_MODE == NO_CURRENT_EXT ||
               CONTEXT_TEST_MODE == NO_CURRENT_EXD) {
      // Exercise current==NULL independently of the entry interrupt state.
      __asm__ volatile("csrw mie, zero" ::: "memory");
      EI(0);
      terminate_invalid();
    }
  }
  return __real_tk_cre_tsk(packet);
}

void *__wrap_schedule(void *sp) {
  if (!tkmc_in_isr()) {
    fail("dispatch entry missing ISR state");
  }
  ++dispatches;
  if (CONTEXT_TEST_MODE == RET_SOFTWARE) {
    capture();
    tk_ret_int();
    fail("tk_ret_int returned from software interrupt");
  }
  return __real_schedule(sp);
}

void __wrap_tkmc_timer_handler(void) {
  if (!tkmc_in_isr()) {
    fail("timer entry missing ISR state");
  }
  __real_tkmc_timer_handler();
  if (CONTEXT_TEST_MODE == RET_TIMER) {
    capture();
    tk_ret_int();
    fail("tk_ret_int returned from timer interrupt");
  }
  if (quiet_armed && current == expected_current && in_w(MSIP) == 0) {
    quiet_armed = FALSE;
    quiet_dispatches = dispatches;
    quiet_seen = TRUE;
  }
  if (!armed || current != expected_current) {
    return;
  }
  armed = FALSE;
  if (CONTEXT_TEST_MODE == ISR_EXT || CONTEXT_TEST_MODE == ISR_EXD) {
    __asm__ volatile("csrw mie, zero" ::: "memory");
    EI(0);
    terminate_invalid();
  }
  probe_waits();
  probe_compatible_calls();
  flagged = tk_set_flg(flgid, 2);
  notified = tk_sig_sem(gate, 1);
  if (child_done) {
    fail("child ran before ISR return");
  }
  observed = TRUE;
}

static void ret_int_hlng_handler(UINT intno) {
  (void)intno;
  out_b(UART_IER, 0);
  capture();
  tk_ret_int();
  fail("tk_ret_int returned from high-level handler");
}

static void child(void *arg) {
  (void)arg;
  child_result = tk_wai_sem(gate, 1, TMO_FEVR);
  child_in_isr = tkmc_in_isr();
  child_done = TRUE;
  test_checkpoint();
}

static void isr_context_compatible_calls_work_in_real_isr_then_dispatch(void) {
  make_objects();
  gate = tk_cre_sem(&(T_CSEM){.maxsem = 1});
  TEST_ASSERT_GREATER_THAN(0, gate);
  test_spawn(child, CHILD_PRIORITY, NULL);
  current->wupcnt = 1;
  expected_current = current;
  UINT before = dispatches;
  armed = TRUE;
  UW started = test_mtime();
  while (!observed &&
         (UW)(test_mtime() - started) < SPIN_MS * TEST_MTIME_PER_MS) {
  }
  ER deleted = tk_del_sem(gate);
  test_join_expect(1);
  TEST_ASSERT_TRUE(observed);
  TEST_ASSERT_FALSE(tkmc_in_isr());
  TEST_ASSERT_EQUAL(E_OK, notified);
  TEST_ASSERT_EQUAL(E_OK, flagged);
  TEST_ASSERT_EQUAL(E_OK, deleted);
  TEST_ASSERT_GREATER_THAN(0, probe_task);
  TEST_ASSERT_GREATER_THAN(0, probe_sem);
  TEST_ASSERT_GREATER_THAN(0, probe_flag);
  TEST_ASSERT_GREATER_THAN(0, probe_mbx);
  TEST_ASSERT_GREATER_THAN(0, probe_deleted_mbx);
  TEST_ASSERT_EQUAL(E_OK, probe_flag_cleared);
  TEST_ASSERT_EQUAL(E_OK, probe_sem_deleted);
  TEST_ASSERT_EQUAL(E_OK, probe_mbx_deleted);
  TEST_ASSERT_EQUAL(E_OK, probe_message_sent);
  TEST_ASSERT_EQUAL(E_NOEXS, tk_sig_sem(probe_sem, 1));
  TEST_ASSERT_EQUAL(E_NOEXS, tk_snd_mbx(probe_deleted_mbx, &probe_message));
  UINT pattern = 1;
  TEST_ASSERT_EQUAL(E_TMOUT,
                    tk_wai_flg(probe_flag, 1, TWF_ORW, &pattern, TMO_POL));
  T_MSG *received = NULL;
  TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(probe_mbx, &received, TMO_POL));
  TEST_ASSERT_EQUAL_PTR(&probe_message, received);
  TEST_ASSERT_EQUAL(E_OK, tk_del_mbx(probe_mbx));
  TEST_ASSERT_TRUE(child_done);
  TEST_ASSERT_FALSE(child_in_isr);
  TEST_ASSERT_EQUAL(E_OK, child_result);
  TEST_ASSERT_GREATER_THAN(before, dispatches);
  TEST_ASSERT_EQUAL_UINT(1, current->wupcnt);
  TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(TMO_POL));

  // A dispatch clears its own ISR state too. Observe a timer return without
  // one in between, so that clear cannot hide a missing timer-exit clear.
  quiet_armed = TRUE;
  BOOL quiet_return = FALSE;
  started = test_mtime();
  while (!quiet_return &&
         (UW)(test_mtime() - started) < SPIN_MS * TEST_MTIME_PER_MS) {
    if (quiet_seen) {
      UINT saved;
      DI(saved);
      quiet_return = dispatches == quiet_dispatches;
      BOOL active = tkmc_in_isr();
      quiet_seen = FALSE;
      quiet_armed = !quiet_return;
      EI(saved);
      if (quiet_return) {
        TEST_ASSERT_FALSE(active);
      }
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(quiet_return,
                           "no timer return without dispatch observed");

  // The kernel has neither tk_del_flg() nor tk_del_tsk(), so flgid,
  // probe_flag and probe_task stay.
  delete_objects();
}

#if CONTEXT_TEST_MODE == LIVE_ISR
static const test_case_t cases[] = {
    CASE(isr_context_compatible_calls_work_in_real_isr_then_dispatch),
};
TEST_SUITE_MAIN("task-context", cases, 4000)
#else
void usermain(int argument) {
  (void)argument;
  if (CONTEXT_TEST_MODE == NO_CURRENT_WAIT) {
    if (!observed || tkmc_in_isr()) {
      fail("prelaunch probe or ISR exit missing");
    }
    delete_objects();
    putstring("NO CURRENT WAIT OK\n");
    test_exit(0);
  }
  if (CONTEXT_TEST_MODE == NO_CURRENT_EXT ||
      CONTEXT_TEST_MODE == NO_CURRENT_EXD) {
    fail("prelaunch termination skipped");
  }
  if (CONTEXT_TEST_MODE == RET_TASK || CONTEXT_TEST_MODE == RET_TASK_DI) {
    if (CONTEXT_TEST_MODE == RET_TASK_DI) {
      UINT ignored;
      DI(ignored);
    }
    capture();
    tk_ret_int();
    fail("tk_ret_int returned from task context");
  }
#if CONTEXT_TEST_MODE == RET_ASM_TASK_DI
  {
    static _Alignas(16) UW stack[128];
    ID id = tk_cre_tsk(&(T_CTSK){.tskatr = TA_ASM | TA_USERBUF,
                                 .task = (FP)asm_task_di_ret_entry,
                                 .itskpri = ASM_TASK_PRIORITY,
                                 .stksz = sizeof(stack),
                                 .bufptr = stack});
    if (id <= 0 || tk_sta_tsk(id, 0) != E_OK) {
      fail("could not start DI ASM task");
    }
    (void)tk_slp_tsk(TMO_FEVR);
    fail("DI ASM task return did not stop");
  }
#endif
  if (CONTEXT_TEST_MODE == RET_HLNG) {
    const T_DINT dint = {.intatr = TA_HLNG, .inthdr = ret_int_hlng_handler};
    if (tk_def_int(UART_IRQ, &dint) != E_OK) {
      fail("could not define UART context handler");
    }
    EnableInt(UART_IRQ, PLIC_MAX_PRIORITY);
    out_b(UART_IER, IER_THRE);
    UW start = test_mtime();
    while ((UW)(test_mtime() - start) < SPIN_MS * TEST_MTIME_PER_MS) {
    }
    fail("UART context handler did not run");
  }
  if (CONTEXT_TEST_MODE == RET_SOFTWARE) {
    capture();
    dispatch();
    fail("software interrupt did not run");
  }
  if (CONTEXT_TEST_MODE == DI_EXT || CONTEXT_TEST_MODE == DI_EXD) {
    UINT ignored;
    DI(ignored);
    terminate_invalid();
  }
  expected_current = current;
  armed = TRUE;
  UW started = test_mtime();
  while ((UW)(test_mtime() - started) < SPIN_MS * TEST_MTIME_PER_MS) {
  }
  fail("ISR termination did not stop");
}
#endif
