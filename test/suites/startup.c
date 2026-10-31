/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// These run before a case task or runner exists. Link wrappers inject one
// failed API result; every earlier step still uses the kernel's real API.
#include <tk/tkernel.h>

#include "../finish.h"
#include "../putstring.h"
#include "ini_tsk.h"
#include "panic.h"
#include "task.h"

#ifndef STARTUP_ENABLE_MIE
#define STARTUP_ENABLE_MIE 0
#endif

_Static_assert(STARTUP_FAIL_STAGE >= TKMC_STARTUP_NONE &&
                   STARTUP_FAIL_STAGE <= TKMC_STARTUP_IDLE_START,
               "invalid injected stage");

extern ID __real_tk_cre_tsk(CONST T_CTSK *packet);
extern ER __real_tk_sta_tsk(ID id, INT argument);
extern TCB *__real_tkmc_get_highest_priority_task(void);
extern void __real_tkmc_start_timer(void);
extern void __real___launch_task(void **sp);
extern void tkmc_idl_tsk(INT, void *);

static UINT step, selections, timer_starts, launches;
static ID created[2];
static INT boot_argument;
static BOOL injected, injected_with_mie;

static __attribute__((noreturn)) void fail(const char *what) {
  putstring("STARTUP :FAIL: ");
  putstring(what);
  putstring("\n");
  test_exit(1);
}

static UINT interrupt_status(void) {
  UINT status;
  __asm__ volatile("csrr %0, mstatus" : "=r"(status)::"memory");
  return status;
}

static BOOL enter_step(tkmc_startup_stage stage) {
  if (injected || ++step != (UINT)stage) {
    fail("continued after failure or wrong API order");
  }
  if (stage != STARTUP_FAIL_STAGE) {
    return FALSE;
  }
  injected = TRUE;
  if (STARTUP_ENABLE_MIE) {
    // No interrupt handler may run before startup has installed current.
    __asm__ volatile("csrw mie, zero" ::: "memory");
    EI(0);
  }
  injected_with_mie = (interrupt_status() & MSTATUS_MIE) != 0;
  return TRUE;
}

ID __wrap_tk_cre_tsk(CONST T_CTSK *packet) {
  BOOL initial = step == 0;
  tkmc_startup_stage stage =
      initial ? TKMC_STARTUP_INITIAL_CREATE : TKMC_STARTUP_IDLE_CREATE;
  BOOL inject = enter_step(stage);
  if (!packet || packet->task != (FP)(initial ? tkmc_ini_tsk : tkmc_idl_tsk) ||
      packet->itskpri != (initial ? 1 : CFN_MAX_PRI)) {
    fail("wrong creation packet");
  }
  if (inject) {
    return E_LIMIT;
  }
  ID id = __real_tk_cre_tsk(packet);
  if (id <= 0) {
    fail("real task creation failed");
  }
  created[initial ? 0 : 1] = id;
  return id;
}

ER __wrap_tk_sta_tsk(ID id, INT argument) {
  BOOL initial = step == TKMC_STARTUP_INITIAL_CREATE;
  BOOL inject = enter_step(initial ? TKMC_STARTUP_INITIAL_START
                                   : TKMC_STARTUP_IDLE_START);
  if (id != created[initial ? 0 : 1] || id <= 0) {
    fail("wrong task ID");
  }
  if (initial) {
    boot_argument = argument;
  } else if (argument != boot_argument) {
    fail("startup argument changed");
  }
  return inject ? E_OBJ : __real_tk_sta_tsk(id, argument);
}

static void require_success(void) {
  if (STARTUP_FAIL_STAGE != TKMC_STARTUP_NONE ||
      step != TKMC_STARTUP_IDLE_START || injected) {
    fail("normal startup continued after an injected error");
  }
}

TCB *__wrap_tkmc_get_highest_priority_task(void) {
  require_success();
  ++selections;
  return __real_tkmc_get_highest_priority_task();
}

void __wrap_tkmc_start_timer(void) {
  require_success();
  ++timer_starts;
  __real_tkmc_start_timer();
}

void __wrap___launch_task(void **sp) {
  require_success();
  ++launches;
  __real___launch_task(sp);
  fail("initial launch returned");
}

void tkmc_startup_panic(tkmc_startup_stage stage, ER error) {
  ER expected =
      stage == TKMC_STARTUP_INITIAL_CREATE || stage == TKMC_STARTUP_IDLE_CREATE
          ? E_LIMIT
          : E_OBJ;
  if (!injected || stage != STARTUP_FAIL_STAGE || step != (UINT)stage ||
      error != expected || tkmc_last_startup_failure.stage != stage ||
      tkmc_last_startup_failure.error != error) {
    fail("wrong startup failure record or hook arguments");
  }
  if ((interrupt_status() & MSTATUS_MIE) ||
      injected_with_mie != (BOOL)STARTUP_ENABLE_MIE || current != NULL ||
      selections || timer_starts || launches || tkmc_last_trap.mcause ||
      tkmc_last_trap.mepc || tkmc_last_trap.mtval) {
    fail("interrupt, scheduling or CPU trap state changed");
  }
  putstring("STARTUP FAILURE OK stage=");
  puthex((UW)stage);
  putstring("\n");
  test_exit(0);
}

void tkmc_panic(UW mcause, UW mepc, UW mtval) {
  (void)mcause;
  (void)mepc;
  (void)mtval;
  fail("unexpected CPU exception");
}

void usermain(int argument) {
  require_success();
  if (selections != 1 || timer_starts != 1 || launches != 1 ||
      argument != boot_argument || !current || current->tskid != created[0] ||
      tkmc_last_startup_failure.stage != TKMC_STARTUP_NONE ||
      tkmc_last_startup_failure.error != 0) {
    fail("wrong successful startup state");
  }
  putstring("STARTUP SUCCESS OK\n");
  test_exit(0);
}
