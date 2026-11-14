/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// A wait call made with interrupts off: from an interrupt handler (the trap
// clears MIE) or from a task inside DI(). Such a call can never be switched
// out, because the switch is taken when interrupts come back on. The kernel
// refuses it with E_CTX before it touches anything; it used to put the caller
// on a wait queue, return E_OK and leave a running task in the waiting state.
// The additional context matrix checks absent current and independent ISR
// state, including MIE on, and captures invalid termination before mutation.
//
// A refusal is checked to change nothing the kernel keeps: every task, ready
// queue, timer queue, semaphore, event flag and mailbox as bytes, the current
// and next task, and the context switch requests (the writes to the software
// interrupt register; its level is no use, setup may have left it set).
// host_check_kernel_queues() is not enough for this: it accepts a task that is
// waiting without being on any queue.
//
// Errors in the arguments come first (except the E_PAR of tk_wai_sem() for a
// count above the maximum, which needs the semaphore and comes after E_NOEXS),
// and the check comes before the object checks (E_NOEXS, E_OBJ), the immediate
// success and the poll.

#include <stdio.h>
#include <stdlib.h>

#include <tk/tkernel.h>
#include <unity.h>

#include "context.h"
#include "event_flag.h"
#include "fakes.h"
#include "intctl.h"
#include "mailbox.h"
#include "panic.h"
#include "semaphore.h"
#include "task.h"

static const UINT FLAG_SENTINEL = 0xdeadbeefU;

static jmp_buf s_context_failure;
static volatile BOOL s_catch_context, s_failure_irq_off;
static volatile tkmc_context_api s_failed_api;

// The host does not compile the RV32 panic implementation. QEMU tests the
// real record/hook; this trap checks guard placement and unchanged kernel
// state.
__attribute__((__noreturn__)) void tkmc_fatal_context(tkmc_context_api api) {
  if (!s_catch_context) {
    fprintf(stderr, "unexpected fatal context api=%d during host test %s\n",
            (int)api,
            Unity.CurrentTestName != NULL ? Unity.CurrentTestName
                                          : "<unknown>");
    abort();
  }
  s_failed_api = api;
  s_failure_irq_off = !host_interrupts_enabled();
  longjmp(s_context_failure, 1);
}

enum {
  HIGH_PRIORITY = 4,
  UNUSED_ID = 2, // No object of any kind has this ID in the fixture.
  WAIT_MS = 100
};

typedef enum { API_DLY, API_SLP, API_SEM, API_FLG, API_MBX, API_COUNT } api_t;

static const char *const api_name[API_COUNT] = {
    "tk_dly_tsk", "tk_slp_tsk", "tk_wai_sem", "tk_wai_flg", "tk_rcv_mbx"};

static ID s_self;
static ID s_other;
static ID s_semid;
static ID s_flgid;
static ID s_mbxid;
static T_MSG s_message;
static T_MSG s_sentinel;

// Two tasks of one priority (the first one runs) and one object of each kind.
// The priorities must be equal: a refusal that wrongly yielded would change
// nothing visible when the running task is alone at its priority.
// With ready set every call that could succeed at once has what it needs: a
// wake-up request is queued for the running task, the semaphore has a unit, the
// flag is set and the mailbox holds a message.
static void fresh(BOOL ready) {
  s_self = host_make_task(HIGH_PRIORITY);
  s_other = host_make_task(HIGH_PRIORITY);
  s_semid = tk_cre_sem(&(T_CSEM){
      .exinf = 0, .sematr = TA_TFIFO, .isemcnt = ready ? 1 : 0, .maxsem = 1});
  s_flgid = tk_cre_flg(&(T_CFLG){.exinf = 0, .flgatr = TA_WSGL});
  s_mbxid = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TFIFO});
  TEST_ASSERT_GREATER_THAN(0, s_semid);
  TEST_ASSERT_GREATER_THAN(0, s_flgid);
  TEST_ASSERT_GREATER_THAN(0, s_mbxid);
  host_set_current(s_self);
  if (ready) {
    tkmc_tcbs[s_self - 1].wupcnt = 1; // tk_wup_tsk() refuses a self-wakeup.
    TEST_ASSERT_EQUAL(E_OK, tk_set_flg(s_flgid, 0x1));
    TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(s_mbxid, &s_message));
  }
}

// One call of a wait API. The ID and the arguments that matter to the checks
// are in the row; USE_FIXTURE_ID takes the ID of the object in the fixture.
enum { USE_FIXTURE_ID = -1000 };

typedef struct {
  const char *label;
  api_t api;
  ID id;
  INT cnt;
  TMO tmo;
  UINT waiptn;
  UINT wfmode;
  BOOL null_output; // Flag/mailbox: no place to put the output.
  ER expected;
} row_t;

static ER call_row(const row_t *row, UINT *flgptn, T_MSG **message) {
  switch (row->api) {
  case API_DLY:
    return tk_dly_tsk(row->tmo);
  case API_SLP:
    return tk_slp_tsk(row->tmo);
  case API_SEM:
    return tk_wai_sem(row->id == USE_FIXTURE_ID ? s_semid : row->id, row->cnt,
                      row->tmo);
  case API_FLG:
    return tk_wai_flg(row->id == USE_FIXTURE_ID ? s_flgid : row->id,
                      row->waiptn, row->wfmode,
                      row->null_output ? NULL : flgptn, row->tmo);
  case API_MBX:
    return tk_rcv_mbx(row->id == USE_FIXTURE_ID ? s_mbxid : row->id,
                      row->null_output ? NULL : message, row->tmo);
  case API_COUNT:
    break;
  }
  TEST_FAIL_MESSAGE("no such API");
  return E_SYS;
}

static row_t valid_row(api_t api, TMO tmo) {
  return (row_t){.label = api_name[api],
                 .api = api,
                 .id = USE_FIXTURE_ID,
                 .cnt = 1,
                 .tmo = tmo,
                 .waiptn = 0x1,
                 .wfmode = TWF_ORW,
                 .null_output = FALSE,
                 .expected = E_CTX};
}

// Makes the call of the row with interrupts off, in the fixture as it stands,
// and checks the result, that interrupts are still off after it, that the
// outputs were not written and that nothing the kernel keeps has changed.
static void check_call_with_interrupts_off(const row_t *row) {
  UINT flgptn = FLAG_SENTINEL;
  T_MSG *message = &s_sentinel;
  host_snapshot_t before;
  host_take_snapshot(&before);

  UINT saved;
  DI(saved);
  ER ercd = call_row(row, &flgptn, &message);
  BOOL still_off = !host_interrupts_enabled();
  EI(saved);

  TEST_ASSERT_EQUAL_INT_MESSAGE(row->expected, ercd, row->label);
  TEST_ASSERT_TRUE_MESSAGE(still_off, row->label);
  TEST_ASSERT_TRUE_MESSAGE(host_interrupts_enabled(), row->label);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(FLAG_SENTINEL, flgptn, row->label);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(&s_sentinel, message, row->label);
  host_assert_unchanged(&before, row->label);
  host_check_kernel_queues();
}

static void check_every_wait(BOOL ready) {
  // For tk_dly_tsk(), whose delay is unsigned, TMO_FEVR is the longest delay.
  static const TMO limits[] = {TMO_FEVR, WAIT_MS, TMO_POL};
  for (api_t api = API_DLY; api < API_COUNT; ++api) {
    for (UINT i = 0; i < sizeof(limits) / sizeof(limits[0]); ++i) {
      host_reset();
      fresh(ready);
      row_t row = valid_row(api, limits[i]);
      char label[64];
      snprintf(label, sizeof(label), "%s, limit %d, %s", api_name[api],
               (int)limits[i], ready ? "can succeed at once" : "would wait");
      row.label = label;
      check_call_with_interrupts_off(&row);
    }
  }
}

// Every way to call a wait API is refused: for ever, with a time limit and as a
// poll (tk_dly_tsk(0) is its poll: it returns at once), when the call would
// block.
void test_every_wait_call_with_interrupts_off_is_refused_and_changes_nothing(
    void) {
  check_every_wait(FALSE);
}

// The same when the call could succeed without blocking: the refusal still
// comes first, and the unit, the pattern, the message and the wake-up request
// are not used up.
void test_a_wait_that_could_succeed_at_once_is_refused_too(void) {
  check_every_wait(TRUE);
}

// Errors in the arguments are reported as they always were; only a call whose
// arguments are right is refused for the context.
void test_argument_errors_come_before_the_context_check(void) {
  const row_t rows[] = {
      {"tk_wai_sem: ID 0", API_SEM, 0, 1, WAIT_MS, 1, TWF_ORW, FALSE, E_ID},
      {"tk_wai_sem: ID too large", API_SEM, CFN_MAX_SEMID + 1, 1, WAIT_MS, 1,
       TWF_ORW, FALSE, E_ID},
      {"tk_wai_flg: ID 0", API_FLG, 0, 1, WAIT_MS, 1, TWF_ORW, FALSE, E_ID},
      {"tk_wai_flg: ID -1", API_FLG, -1, 1, WAIT_MS, 1, TWF_ORW, FALSE, E_ID},
      {"tk_wai_flg: ID too large", API_FLG, CFN_MAX_FLGID + 1, 1, WAIT_MS, 1,
       TWF_ORW, FALSE, E_ID},
      {"tk_rcv_mbx: ID 0", API_MBX, 0, 1, WAIT_MS, 1, TWF_ORW, FALSE, E_ID},
      {"tk_rcv_mbx: ID too large", API_MBX, CFN_MAX_MBXID + 1, 1, WAIT_MS, 1,
       TWF_ORW, FALSE, E_ID},
      {"tk_slp_tsk: time below for ever", API_SLP, USE_FIXTURE_ID, 1,
       TMO_FEVR - 1, 1, TWF_ORW, FALSE, E_PAR},
      {"tk_wai_sem: count 0", API_SEM, USE_FIXTURE_ID, 0, WAIT_MS, 1, TWF_ORW,
       FALSE, E_PAR},
      {"tk_wai_sem: time below for ever", API_SEM, USE_FIXTURE_ID, 1,
       TMO_FEVR - 1, 1, TWF_ORW, FALSE, E_PAR},
      {"tk_wai_flg: empty pattern", API_FLG, USE_FIXTURE_ID, 1, WAIT_MS, 0,
       TWF_ORW, FALSE, E_PAR},
      {"tk_wai_flg: unknown mode bits", API_FLG, USE_FIXTURE_ID, 1, WAIT_MS, 1,
       ~0U, FALSE, E_PAR},
      {"tk_wai_flg: time below for ever", API_FLG, USE_FIXTURE_ID, 1,
       TMO_FEVR - 1, 1, TWF_ORW, FALSE, E_PAR},
      {"tk_rcv_mbx: no place for the message", API_MBX, USE_FIXTURE_ID, 1,
       WAIT_MS, 1, TWF_ORW, TRUE, E_PAR},
      {"tk_rcv_mbx: time below for ever", API_MBX, USE_FIXTURE_ID, 1,
       TMO_FEVR - 1, 1, TWF_ORW, FALSE, E_PAR},
  };
  for (UINT i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
    host_reset();
    fresh(FALSE);
    check_call_with_interrupts_off(&rows[i]);
  }
}

// An object that does not exist is not reported ahead of the context: the
// check comes before the object is looked at.
void test_the_context_check_comes_before_the_object_checks(void) {
  const row_t rows[] = {
      {"tk_wai_sem: no such semaphore", API_SEM, UNUSED_ID, 1, WAIT_MS, 1,
       TWF_ORW, FALSE, E_CTX},
      {"tk_wai_flg: no such flag", API_FLG, UNUSED_ID, 1, WAIT_MS, 1, TWF_ORW,
       FALSE, E_CTX},
      {"tk_rcv_mbx: no such mailbox", API_MBX, UNUSED_ID, 1, WAIT_MS, 1,
       TWF_ORW, FALSE, E_CTX},
  };
  for (UINT i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
    host_reset();
    fresh(FALSE);
    check_call_with_interrupts_off(&rows[i]);
  }
}

// A flag that allows one waiter answers a second waiter with E_OBJ, and the
// context check comes before that too.
void test_the_context_check_comes_before_the_one_waiter_check_of_a_flag(void) {
  fresh(FALSE);
  UINT pattern = 0;
  host_set_current(s_other);
  HOST_EXPECT_WAIT(tk_wai_flg(s_flgid, 0x1, TWF_ORW, &pattern, TMO_FEVR));
  host_set_current(s_self);

  const row_t row = valid_row(API_FLG, TMO_FEVR);
  check_call_with_interrupts_off(&row);
}

// tk_dly_tsk(0) is the one wait that does not block: with interrupts on it
// returns E_OK at once and changes nothing, as the reference implementation
// waits only for a positive delay. It does not give the processor to a task of
// the same priority either, which the peer here would show: the order of the
// ready queue, the next task and the switch requests stay as they were.
void test_a_delay_of_zero_with_interrupts_on_changes_nothing_and_leaves_them_on(
    void) {
  ID self = host_make_task(HIGH_PRIORITY);
  ID peer = host_make_task(HIGH_PRIORITY);
  host_set_current(self);
  // The peer is ready behind the running task: a yield would show.
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tkmc_tcbs[peer - 1].tskstat);
  TEST_ASSERT_EQUAL_PTR(&tkmc_tcbs[self - 1], next);
  host_snapshot_t before;
  host_take_snapshot(&before);

  TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(0));

  TEST_ASSERT_TRUE(host_interrupts_enabled());
  host_assert_unchanged(&before, "a delay of zero changed the kernel");
}

void test_invalid_termination_context_stops_before_changing_tasks(void) {
  for (UINT api = 0; api < 2; ++api) {
    for (UINT mode = 0; mode < 3; ++mode) {
      for (UINT irq = 0; irq < 2; ++irq) {
        // A valid task with IRQ on is the permitted termination path.
        if (mode == 0 && irq) {
          continue;
        }
        host_reset();
        fresh(FALSE);
        if (mode == 1) {
          current = NULL;
        }
        tkmc_isr_depth = mode == 2;
        host_snapshot_t before;
        host_take_snapshot(&before);
        UINT saved;
        if (!irq) {
          DI(saved);
        }
        s_catch_context = TRUE;
        volatile BOOL returned = FALSE;
        if (setjmp(s_context_failure) == 0) {
          if (api) {
            tk_exd_tsk();
          } else {
            tk_ext_tsk();
          }
          returned = TRUE;
        }
        s_catch_context = FALSE;
        EI(0);
        TEST_ASSERT_FALSE_MESSAGE(returned, "invalid termination returned");
        TEST_ASSERT_EQUAL(api ? TKMC_CONTEXT_EXD_TSK : TKMC_CONTEXT_EXT_TSK,
                          s_failed_api);
        TEST_ASSERT_TRUE(s_failure_irq_off);
        TEST_ASSERT_EQUAL(mode == 2, tkmc_in_isr());
        host_assert_unchanged(&before,
                              "invalid termination must not unlink a task");
        host_check_kernel_queues();
      }
    }
  }
}

void test_isr_and_absent_current_refuse_every_wait_without_changes(void) {
  static const TMO limits[] = {TMO_POL, 100, TMO_FEVR};
  for (UINT mode = 0; mode < 2; ++mode) {
    for (UINT irq = 0; irq < 2; ++irq) {
      for (UINT ready = 0; ready < 2; ++ready) {
        for (api_t api = API_DLY; api < API_COUNT; ++api) {
          for (UINT limit = 0; limit < 3; ++limit) {
            host_reset();
            fresh(ready);
            if (mode == 0) {
              current = NULL;
            }
            tkmc_isr_depth = mode == 1;
            row_t row = valid_row(api, limits[limit]);
            UINT pattern = FLAG_SENTINEL;
            T_MSG *message = &s_sentinel;
            host_snapshot_t before;
            host_take_snapshot(&before);
            UINT saved;
            if (!irq) {
              DI(saved);
            }
            ER result = call_row(&row, &pattern, &message);
            BOOL after_irq = host_interrupts_enabled();
            EI(0);
            TEST_ASSERT_EQUAL_INT_MESSAGE(E_CTX, result, row.label);
            TEST_ASSERT_EQUAL(irq, after_irq);
            TEST_ASSERT_EQUAL(mode == 1, tkmc_in_isr());
            TEST_ASSERT_EQUAL_UINT(FLAG_SENTINEL, pattern);
            TEST_ASSERT_EQUAL_PTR(&s_sentinel, message);
            host_assert_unchanged(&before, row.label);
            host_check_kernel_queues();
          }
        }
      }
    }
  }
  tkmc_isr_depth = 1;
  host_reset();
  TEST_ASSERT_FALSE(tkmc_in_isr());
}

// The depth counts traps, and a trap taken inside a trap makes it 2: that is
// still the ISR state, so every wait is refused as at depth 1. Kept apart from
// the loop above, whose mode distinguishes the kinds of context, not depths.
void test_a_trap_inside_a_trap_is_still_the_isr_state_for_every_wait(void) {
  for (api_t api = API_DLY; api < API_COUNT; ++api) {
    host_reset();
    fresh(TRUE);
    tkmc_isr_depth = 2;
    row_t row = valid_row(api, TMO_FEVR);
    UINT pattern = FLAG_SENTINEL;
    T_MSG *message = &s_sentinel;
    host_snapshot_t before;
    host_take_snapshot(&before);
    ER result = call_row(&row, &pattern, &message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(E_CTX, result, row.label);
    TEST_ASSERT_TRUE(tkmc_in_isr());
    host_assert_unchanged(&before, row.label);
    host_check_kernel_queues();
  }
  tkmc_isr_depth = 0;
}

void test_isr_and_absent_current_preserve_argument_error_priority(void) {
  for (UINT mode = 0; mode < 2; ++mode) {
    for (api_t api = API_SLP; api < API_COUNT; ++api) {
      for (UINT bad = 0; bad < 5; ++bad) {
        host_reset();
        fresh(TRUE);
        if (!mode) {
          current = NULL;
        }
        tkmc_isr_depth = mode;
        row_t row = valid_row(api, TMO_POL);
        row.expected = E_PAR;
        switch (bad) {
        case 0:
          row.tmo = -2;
          break;
        case 1:
          if (api == API_SLP) {
            continue;
          }
          row.id = 0;
          row.expected = E_ID;
          break;
        case 2:
          if (api == API_SLP) {
            continue;
          }
          row.id = UNUSED_ID;
          row.expected = E_CTX;
          break;
        case 3:
          if (api == API_SLP) {
            continue;
          }
          if (api == API_SEM) {
            row.cnt = 0;
          } else {
            row.null_output = TRUE;
          }
          break;
        default:
          if (api == API_SLP) {
            continue;
          }
          if (api == API_SEM) {
            row.cnt = 2;
            row.expected = E_CTX;
          } else if (api == API_FLG) {
            row.waiptn = 0;
          } else {
            continue;
          }
          break;
        }
        UINT pattern = FLAG_SENTINEL;
        T_MSG *message = &s_sentinel;
        host_snapshot_t before;
        host_take_snapshot(&before);
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            row.expected, call_row(&row, &pattern, &message), row.label);
        TEST_ASSERT_TRUE(host_interrupts_enabled());
        TEST_ASSERT_EQUAL(mode, tkmc_in_isr());
        TEST_ASSERT_EQUAL_UINT(FLAG_SENTINEL, pattern);
        TEST_ASSERT_EQUAL_PTR(&s_sentinel, message);
        host_assert_unchanged(&before, row.label);
      }
    }
  }
}

static void assert_ret_int_stops(void *entry_sp) {
  host_snapshot_t before;
  host_take_snapshot(&before);
  s_failed_api = TKMC_CONTEXT_NONE;
  s_failure_irq_off = FALSE;
  s_catch_context = TRUE;
  volatile BOOL returned = FALSE;
  if (setjmp(s_context_failure) == 0) {
    tkmc_ret_int_from_sp(entry_sp);
    returned = TRUE;
  }
  s_catch_context = FALSE;
  TEST_ASSERT_FALSE_MESSAGE(returned, "tk_ret_int returned in a bad context");
  TEST_ASSERT_EQUAL(TKMC_CONTEXT_RET_INT, s_failed_api);
  TEST_ASSERT_TRUE(s_failure_irq_off);
  host_assert_unchanged(&before, "invalid tk_ret_int changed kernel state");
  host_check_kernel_queues();
}

static void raw_hl_handler(UINT intno) { (void)intno; }

static void prepare_asm_context(tkmc_interrupt_context *context, UINT intno,
                                UW *frame) {
  const T_DINT assembler = {.intatr = TA_ASM, .inthdr = (FP)raw_hl_handler};
  EI(0);
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(intno, &assembler));
  EnableInt(intno, 3);
  host_plic_raise(intno);
  tkmc_isr_depth = 1;
  UINT saved;
  DI(saved);
  (void)saved;
  tkmc_prepare_external_interrupt(context, frame);
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_ASM, context->kind);
  TEST_ASSERT_EQUAL_UINT(intno, context->intno);
}

void test_tk_ret_int_stops_in_non_asm_contexts(void) {
  UINT saved;
  // Task context with interrupts on or off.
  for (UINT enabled = 0; enabled < 2; ++enabled) {
    host_reset();
    fresh(FALSE);
    UINT saved = 0;
    if (!enabled) {
      DI(saved);
    }
    assert_ret_int_stops(NULL);
    EI(saved);
  }

  // No current task, and an ISR context without an active ASM external frame
  // (the host model cannot distinguish timer from software trap entry).
  host_reset();
  fresh(FALSE);
  current = NULL;
  assert_ret_int_stops(NULL);
  EI(0);

  host_reset();
  fresh(FALSE);
  tkmc_isr_depth = 1;
  DI(saved);
  assert_ret_int_stops(NULL);
  tkmc_isr_depth = 0;
  EI(saved);

  // An active high-level external handler is still not a tk_ret_int caller.
  host_reset();
  fresh(FALSE);
  TEST_ASSERT_EQUAL(E_OK,
                    tk_def_int(10, &(T_DINT){.intatr = TA_HLNG,
                                             .inthdr = (FP)raw_hl_handler}));
  EnableInt(10, 1);
  host_plic_raise(10);
  tkmc_isr_depth = 1;
  DI(saved);
  _Alignas(16) UW frame[32] = {0};
  tkmc_interrupt_context context;
  tkmc_prepare_external_interrupt(&context, frame);
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_HLNG, context.kind);
  // Keep task tables unchanged, then finish the synthetic trap so its claim
  // does not leak into another host test.
  assert_ret_int_stops(&context);
  tkmc_finish_external_interrupt(&context);
  tkmc_isr_depth = 0;
  EI(0);
}

void test_tk_ret_int_rejects_invalid_asm_contexts(void) {
  _Alignas(16) UW frame[32] = {0};
  tkmc_interrupt_context context;

  // A context record is usable only from the entry stack pointer associated
  // with that exact claim.
  host_reset();
  fresh(FALSE);
  prepare_asm_context(&context, 10, frame);
  assert_ret_int_stops((unsigned char *)&context + sizeof(void *));
  tkmc_finish_external_interrupt(&context);
  tkmc_isr_depth = 0;
  EI(0);

  // A record from an outer depth cannot be returned while a nested trap is
  // active.
  host_reset();
  fresh(FALSE);
  prepare_asm_context(&context, 10, frame);
  tkmc_isr_depth = 2;
  assert_ret_int_stops(&context);
  tkmc_isr_depth = 1;
  tkmc_finish_external_interrupt(&context);
  tkmc_isr_depth = 0;
  EI(0);

  // The public entry is an interrupt exit and requires MIE to be off at its
  // call site. A valid ASM record with MIE on must fail before completing it.
  host_reset();
  fresh(FALSE);
  prepare_asm_context(&context, 10, frame);
  EI(0);
  assert_ret_int_stops(&context);
  tkmc_finish_external_interrupt(&context);
  tkmc_isr_depth = 0;
  EI(0);

  // A valid active ASM record is still rejected when no task is current.
  host_reset();
  fresh(FALSE);
  prepare_asm_context(&context, 10, frame);
  TCB *saved_current = current;
  current = NULL;
  assert_ret_int_stops(&context);
  current = saved_current;
  tkmc_finish_external_interrupt(&context);
  tkmc_isr_depth = 0;
  EI(0);

  // The inner HLNG record remains invalid even when its previous record is a
  // valid ASM external interrupt.
  _Alignas(16) UW inner_frame[32] = {0};
  tkmc_interrupt_context outer, inner;
  host_reset();
  fresh(FALSE);
  TEST_ASSERT_EQUAL(E_OK,
                    tk_def_int(11, &(T_DINT){.intatr = TA_HLNG,
                                             .inthdr = (FP)raw_hl_handler}));
  EnableInt(11, 5);
  prepare_asm_context(&outer, 10, frame);
  host_plic_raise(11);
  tkmc_isr_depth = 2;
  tkmc_prepare_external_interrupt(&inner, inner_frame);
  TEST_ASSERT_EQUAL_PTR(&outer, inner.previous);
  TEST_ASSERT_EQUAL_UINT(TKMC_INTERRUPT_KIND_HLNG, inner.kind);
  assert_ret_int_stops(&inner);
  tkmc_finish_external_interrupt(&inner);
  tkmc_isr_depth = 1;
  tkmc_finish_external_interrupt(&outer);
  tkmc_isr_depth = 0;
  EI(0);
}
