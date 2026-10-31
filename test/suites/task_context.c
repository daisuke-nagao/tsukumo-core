/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>
#include <unity.h>

#include "../finish.h"
#include "../putstring.h"
#include "asm/rv32/address.h"
#include "context.h"
#include "event_flag.h"
#include "mailbox.h"
#include "panic.h"
#include "runner.h"
#include "semaphore.h"

enum {
  LIVE_ISR,
  NO_CURRENT_WAIT,
  DI_EXT,
  DI_EXD,
  ISR_EXT,
  ISR_EXD,
  NO_CURRENT_EXT,
  NO_CURRENT_EXD
};
_Static_assert(CONTEXT_TEST_MODE >= LIVE_ISR &&
                   CONTEXT_TEST_MODE <= NO_CURRENT_EXD,
               "unknown context fixture");

extern SEMCB tkmc_semcbs[CFN_MAX_SEMID];
extern FLGCB tkmc_flgcbs[CFN_MAX_FLGID];
extern MBXCB tkmc_mbxcbs[CFN_MAX_MBXID];
extern tkmc_list_head tkmc_timer_queue;
extern ID __real_tk_cre_tsk(CONST T_CTSK *packet);
extern void __real_tkmc_timer_handler(void);
extern void **__real_schedule(void *sp);

enum { MSIP = CLINT_BASE_ADDRESS + CLINT_MSIP_OFFSET, SENTINEL = 0x12345678 };
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
static volatile BOOL armed, observed, child_done, child_in_isr;
static volatile ER child_result, notified;
static volatile UINT dispatches;
static volatile BOOL quiet_armed, quiet_seen;
static volatile UINT quiet_dispatches;
static TCB *expected_current;
static BOOL startup_probed;

static __attribute__((noreturn)) void fail(const char *reason) {
  putstring("TASK CONTEXT :FAIL: ");
  putstring(reason);
  putstring("\n");
  test_exit(1);
}

static void copy_bytes(void *to, const void *from, UINT size) {
  unsigned char *dst = to;
  const unsigned char *src = from;
  for (UINT i = 0; i < size; ++i) {
    dst[i] = src[i];
  }
}

static BOOL equal_bytes(const void *a, const void *b, UINT size) {
  const unsigned char *left = a, *right = b;
  for (UINT i = 0; i < size; ++i) {
    if (left[i] != right[i]) {
      return FALSE;
    }
  }
  return TRUE;
}

static void capture(void) {
  copy_bytes(snapshot.tasks, tkmc_tcbs, sizeof(snapshot.tasks));
  copy_bytes(snapshot.ready, tkmc_ready_queue, sizeof(snapshot.ready));
  copy_bytes(&snapshot.timer, &tkmc_timer_queue, sizeof(snapshot.timer));
  copy_bytes(snapshot.sems, tkmc_semcbs, sizeof(snapshot.sems));
  copy_bytes(snapshot.flags, tkmc_flgcbs, sizeof(snapshot.flags));
  copy_bytes(snapshot.mailboxes, tkmc_mbxcbs, sizeof(snapshot.mailboxes));
  snapshot.running = current;
  snapshot.scheduled = next;
  snapshot.isr = tkmc_isr_active;
  snapshot.msip = in_w(MSIP);
}

static BOOL unchanged(void) {
  return equal_bytes(snapshot.tasks, tkmc_tcbs, sizeof(snapshot.tasks)) &&
         equal_bytes(snapshot.ready, tkmc_ready_queue,
                     sizeof(snapshot.ready)) &&
         equal_bytes(&snapshot.timer, &tkmc_timer_queue,
                     sizeof(snapshot.timer)) &&
         equal_bytes(snapshot.sems, tkmc_semcbs, sizeof(snapshot.sems)) &&
         equal_bytes(snapshot.flags, tkmc_flgcbs, sizeof(snapshot.flags)) &&
         equal_bytes(snapshot.mailboxes, tkmc_mbxcbs,
                     sizeof(snapshot.mailboxes)) &&
         snapshot.running == current && snapshot.scheduled == next &&
         snapshot.isr == tkmc_isr_active && snapshot.msip == in_w(MSIP);
}

static BOOL irq_enabled(void) {
  UINT status;
  __asm__ volatile("csrr %0, mstatus" : "=r"(status)::"memory");
  return (status & MSTATUS_MIE) != 0;
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
        if (result != E_CTX || irq_enabled() != (BOOL)irq ||
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

static __attribute__((noreturn)) void terminate_invalid(void) {
  capture();
  if (CONTEXT_TEST_MODE == DI_EXD || CONTEXT_TEST_MODE == ISR_EXD ||
      CONTEXT_TEST_MODE == NO_CURRENT_EXD) {
    tk_exd_tsk();
  } else {
    tk_ext_tsk();
  }
  fail("invalid termination returned");
}

void tkmc_context_panic(tkmc_context_api api) {
  tkmc_context_api expected =
      CONTEXT_TEST_MODE % 2 ? TKMC_CONTEXT_EXD_TSK : TKMC_CONTEXT_EXT_TSK;
  if (CONTEXT_TEST_MODE < DI_EXT || api != expected ||
      tkmc_last_context_failure.api != expected ||
      tkmc_last_context_failure.error != E_CTX || irq_enabled() ||
      !unchanged()) {
    fail("wrong context failure or task state changed before stop");
  }
  putstring("CONTEXT STOP OK\n");
  test_exit(0);
}

void tkmc_panic(UW cause, UW epc, UW value) {
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
    } else if (CONTEXT_TEST_MODE >= NO_CURRENT_EXT) {
      // Exercise current==NULL independently of the entry interrupt state.
      __asm__ volatile("csrw mie, zero" ::: "memory");
      EI(0);
      terminate_invalid();
    }
  }
  return __real_tk_cre_tsk(packet);
}

void **__wrap_schedule(void *sp) {
  if (!tkmc_in_isr()) {
    fail("dispatch entry missing ISR state");
  }
  ++dispatches;
  return __real_schedule(sp);
}

void __wrap_tkmc_timer_handler(void) {
  if (!tkmc_in_isr()) {
    fail("timer entry missing ISR state");
  }
  __real_tkmc_timer_handler();
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
  notified = tk_sig_sem(gate, 1);
  if (child_done) {
    fail("child ran before ISR return");
  }
  observed = TRUE;
}

static void child(void *arg) {
  (void)arg;
  child_result = tk_wai_sem(gate, 1, TMO_FEVR);
  child_in_isr = tkmc_in_isr();
  child_done = TRUE;
  test_checkpoint();
}

static void real_isr_refuses_waits_then_dispatches(void) {
  make_objects();
  gate = tk_cre_sem(&(T_CSEM){.maxsem = 1});
  TEST_ASSERT_GREATER_THAN(0, gate);
  test_spawn(child, 2, NULL);
  current->wupcnt = 1;
  expected_current = current;
  UINT before = dispatches;
  armed = TRUE;
  UW started = test_mtime();
  while (!observed && (UW)(test_mtime() - started) < 2000 * TEST_MTIME_PER_MS) {
  }
  ER deleted = tk_del_sem(gate);
  test_join_expect(1);
  TEST_ASSERT_TRUE(observed);
  TEST_ASSERT_FALSE(tkmc_in_isr());
  TEST_ASSERT_EQUAL(E_OK, notified);
  TEST_ASSERT_EQUAL(E_OK, deleted);
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
         (UW)(test_mtime() - started) < 2000 * TEST_MTIME_PER_MS) {
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
}

#if CONTEXT_TEST_MODE == 0
static const test_case_t cases[] = {
    {"real_isr_refuses_waits_then_dispatches",
     real_isr_refuses_waits_then_dispatches},
};
TEST_SUITE_MAIN("task-context", cases, 4000)
#else
void usermain(int argument) {
  (void)argument;
  if (CONTEXT_TEST_MODE == NO_CURRENT_WAIT) {
    if (!observed || tkmc_in_isr()) {
      fail("prelaunch probe or ISR exit missing");
    }
    putstring("NO CURRENT WAIT OK\n");
    test_exit(0);
  }
  if (CONTEXT_TEST_MODE >= NO_CURRENT_EXT) {
    fail("prelaunch termination skipped");
  }
  if (CONTEXT_TEST_MODE == DI_EXT || CONTEXT_TEST_MODE == DI_EXD) {
    UINT ignored;
    DI(ignored);
    terminate_invalid();
  }
  expected_current = current;
  armed = TRUE;
  UW started = test_mtime();
  while ((UW)(test_mtime() - started) < 2000 * TEST_MTIME_PER_MS) {
  }
  fail("ISR termination did not stop");
}
#endif
