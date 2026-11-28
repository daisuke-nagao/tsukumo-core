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
// state, including MIE on.
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

#include <tk/tkernel.h>
#include <unity.h>

#include "context.h"
#include "fakes.h"

static const UINT FLAG_SENTINEL = 0xdeadbeefU;

enum { UNUSED_ID = 2 }; // No object of any kind has this ID in the fixture.

typedef enum { API_DLY, API_SLP, API_SEM, API_FLG, API_MBX, API_COUNT } api_t;

static const char *const api_name[API_COUNT] = {
    "tk_dly_tsk", "tk_slp_tsk", "tk_wai_sem", "tk_wai_flg", "tk_rcv_mbx"};

static ID s_self;
static ID s_other;
static host_objects_t s_obj;
static T_MSG s_message;
static T_MSG s_sentinel;

// Two tasks of one priority (the first one runs) and one object of each kind.
// The priorities must be equal: a refusal that wrongly yielded would change
// nothing visible when the running task is alone at its priority.
// With ready set every call that could succeed at once has what it needs: a
// wake-up request is queued for the running task, the semaphore has a unit, the
// flag is set and the mailbox holds a message.
static void fresh(BOOL ready) {
  s_self = host_make_task(HOST_HIGH_PRIORITY);
  s_other = host_make_task(HOST_HIGH_PRIORITY);
  s_obj = host_make_objects();
  host_set_current(s_self);
  if (ready) {
    tkmc_tcbs[s_self - 1].wupcnt = 1; // tk_wup_tsk() refuses a self-wakeup.
    TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(s_obj.semid, 1));
    TEST_ASSERT_EQUAL(E_OK, tk_set_flg(s_obj.flgid, 0x1));
    TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(s_obj.mbxid, &s_message));
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
    return tk_wai_sem(row->id == USE_FIXTURE_ID ? s_obj.semid : row->id,
                      row->cnt, row->tmo);
  case API_FLG:
    return tk_wai_flg(row->id == USE_FIXTURE_ID ? s_obj.flgid : row->id,
                      row->waiptn, row->wfmode,
                      row->null_output ? NULL : flgptn, row->tmo);
  case API_MBX:
    return tk_rcv_mbx(row->id == USE_FIXTURE_ID ? s_obj.mbxid : row->id,
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

// Where a call is made: whether a task is current, the ISR depth (the number
// of traps the RV32 entries have taken) and whether interrupts are off.
typedef struct {
  const char *name;
  BOOL no_task;
  UINT isr_depth;
  BOOL interrupts_off;
} context_t;

static const context_t TASK_IN_DI = {"a task inside DI()", FALSE, 0, TRUE};

// Makes the call of the row in the context, in the fixture as it stands, and
// checks the result, that the interrupt flag and the ISR state are as the call
// found them, that the outputs were not written and that nothing the kernel
// keeps has changed.
static void check_call_in(const context_t *context, const row_t *row) {
  char label[128];
  snprintf(label, sizeof(label), "%s, in %s", row->label, context->name);
  UINT flgptn = FLAG_SENTINEL;
  T_MSG *message = &s_sentinel;
  if (context->no_task) {
    current = NULL;
  }
  host_snapshot_t before;
  host_take_snapshot(&before);

  const host_context_t entered = host_enter(FALSE, context->interrupts_off);
  tkmc_isr_depth = context->isr_depth;
  ER ercd = call_row(row, &flgptn, &message);
  TEST_ASSERT_EQUAL_MESSAGE(!context->interrupts_off, host_interrupts_enabled(),
                            label);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(context->isr_depth, tkmc_isr_depth, label);
  host_leave(entered);

  TEST_ASSERT_EQUAL_INT_MESSAGE(row->expected, ercd, label);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(FLAG_SENTINEL, flgptn, label);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(&s_sentinel, message, label);
  host_assert_unchanged(&before, label);
  host_check_kernel_queues();
}

// Errors in the arguments are reported as they always were; only a call whose
// arguments are right is refused for the context.
static void test_wait_context_argument_errors_come_before_context_check(void) {
  const row_t rows[] = {
      {"tk_wai_sem: ID 0", API_SEM, 0, 1, HOST_WAIT_MS, 1, TWF_ORW, FALSE,
       E_ID},
      {"tk_wai_sem: ID too large", API_SEM, CFN_MAX_SEMID + 1, 1, HOST_WAIT_MS,
       1, TWF_ORW, FALSE, E_ID},
      {"tk_wai_flg: ID 0", API_FLG, 0, 1, HOST_WAIT_MS, 1, TWF_ORW, FALSE,
       E_ID},
      {"tk_wai_flg: ID -1", API_FLG, -1, 1, HOST_WAIT_MS, 1, TWF_ORW, FALSE,
       E_ID},
      {"tk_wai_flg: ID too large", API_FLG, CFN_MAX_FLGID + 1, 1, HOST_WAIT_MS,
       1, TWF_ORW, FALSE, E_ID},
      {"tk_rcv_mbx: ID 0", API_MBX, 0, 1, HOST_WAIT_MS, 1, TWF_ORW, FALSE,
       E_ID},
      {"tk_rcv_mbx: ID too large", API_MBX, CFN_MAX_MBXID + 1, 1, HOST_WAIT_MS,
       1, TWF_ORW, FALSE, E_ID},
      {"tk_slp_tsk: time below for ever", API_SLP, USE_FIXTURE_ID, 1,
       TMO_FEVR - 1, 1, TWF_ORW, FALSE, E_PAR},
      {"tk_wai_sem: count 0", API_SEM, USE_FIXTURE_ID, 0, HOST_WAIT_MS, 1,
       TWF_ORW, FALSE, E_PAR},
      {"tk_wai_sem: time below for ever", API_SEM, USE_FIXTURE_ID, 1,
       TMO_FEVR - 1, 1, TWF_ORW, FALSE, E_PAR},
      {"tk_wai_flg: empty pattern", API_FLG, USE_FIXTURE_ID, 1, HOST_WAIT_MS, 0,
       TWF_ORW, FALSE, E_PAR},
      {"tk_wai_flg: unknown mode bits", API_FLG, USE_FIXTURE_ID, 1,
       HOST_WAIT_MS, 1, ~0U, FALSE, E_PAR},
      {"tk_wai_flg: time below for ever", API_FLG, USE_FIXTURE_ID, 1,
       TMO_FEVR - 1, 1, TWF_ORW, FALSE, E_PAR},
      {"tk_rcv_mbx: no place for the message", API_MBX, USE_FIXTURE_ID, 1,
       HOST_WAIT_MS, 1, TWF_ORW, TRUE, E_PAR},
      {"tk_rcv_mbx: time below for ever", API_MBX, USE_FIXTURE_ID, 1,
       TMO_FEVR - 1, 1, TWF_ORW, FALSE, E_PAR},
  };
  for (UINT i = 0; i < HOST_COUNT(rows); ++i) {
    host_reset();
    fresh(FALSE);
    check_call_in(&TASK_IN_DI, &rows[i]);
  }
}

// An object that does not exist is not reported ahead of the context: the
// check comes before the object is looked at.
static void test_wait_context_check_comes_before_object_checks(void) {
  const row_t rows[] = {
      {"tk_wai_sem: no such semaphore", API_SEM, UNUSED_ID, 1, HOST_WAIT_MS, 1,
       TWF_ORW, FALSE, E_CTX},
      {"tk_wai_flg: no such flag", API_FLG, UNUSED_ID, 1, HOST_WAIT_MS, 1,
       TWF_ORW, FALSE, E_CTX},
      {"tk_rcv_mbx: no such mailbox", API_MBX, UNUSED_ID, 1, HOST_WAIT_MS, 1,
       TWF_ORW, FALSE, E_CTX},
  };
  for (UINT i = 0; i < HOST_COUNT(rows); ++i) {
    host_reset();
    fresh(FALSE);
    check_call_in(&TASK_IN_DI, &rows[i]);
  }
}

// A flag that allows one waiter answers a second waiter with E_OBJ, and the
// context check comes before that too.
static void
test_wait_context_check_comes_before_one_waiter_check_of_flag(void) {
  fresh(FALSE);
  UINT pattern = 0;
  host_set_current(s_other);
  HOST_EXPECT_WAIT(tk_wai_flg(s_obj.flgid, 0x1, TWF_ORW, &pattern, TMO_FEVR));
  host_set_current(s_self);

  const row_t row = valid_row(API_FLG, TMO_FEVR);
  check_call_in(&TASK_IN_DI, &row);
}

// tk_dly_tsk(0) is the one wait that does not block: with interrupts on it
// returns E_OK at once and changes nothing, as the reference implementation
// waits only for a positive delay. It does not give the processor to a task of
// the same priority either, which the peer here would show: the order of the
// ready queue, the next task and the switch requests stay as they were.
static void
test_tk_dly_tsk_zero_with_interrupts_on_changes_nothing_and_leaves_them_on(
    void) {
  ID self = host_make_task(HOST_HIGH_PRIORITY);
  ID peer = host_make_task(HOST_HIGH_PRIORITY);
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

// A task inside DI(), no task, or an ISR cannot wait: every way to call a wait
// API is refused with E_CTX and changes nothing. That holds for ever, with a
// time limit and as a poll (tk_dly_tsk(0) is its poll: it returns at once,
// and for tk_dly_tsk(), whose delay is unsigned, TMO_FEVR is the longest
// delay), when the call would block and when it could succeed at once: the
// refusal still comes first, and the unit, the pattern, the message and the
// wake-up request are not used up. An ISR or no task is refused whatever the
// interrupt flag says.
static const context_t NO_WAIT_CONTEXTS[] = {
    {"a task inside DI()", FALSE, 0, TRUE},
    {"no current task, interrupts off", TRUE, 0, TRUE},
    {"no current task, interrupts on", TRUE, 0, FALSE},
    {"an ISR, interrupts off", FALSE, 1, TRUE},
    {"an ISR, interrupts on", FALSE, 1, FALSE},
};

static void
test_wait_context_di_isr_and_absent_current_refuse_every_wait_without_changes(
    void) {
  static const TMO limits[] = {TMO_POL, HOST_WAIT_MS, TMO_FEVR};
  for (UINT c = 0; c < HOST_COUNT(NO_WAIT_CONTEXTS); ++c) {
    for (UINT ready = 0; ready < 2; ++ready) {
      for (api_t api = API_DLY; api < API_COUNT; ++api) {
        for (UINT limit = 0; limit < HOST_COUNT(limits); ++limit) {
          host_reset();
          fresh(ready);
          row_t row = valid_row(api, limits[limit]);
          char label[64];
          snprintf(label, sizeof(label), "%s, limit %d, %s", api_name[api],
                   (int)limits[limit],
                   ready ? "can succeed at once" : "would wait");
          row.label = label;
          check_call_in(&NO_WAIT_CONTEXTS[c], &row);
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
// the table above, whose rows are kinds of context, not depths.
static void
test_wait_context_nested_trap_still_counts_as_isr_state_for_every_wait(void) {
  static const context_t nested = {"a trap inside a trap", FALSE, 2, FALSE};
  for (api_t api = API_DLY; api < API_COUNT; ++api) {
    host_reset();
    fresh(TRUE);
    const row_t row = valid_row(api, TMO_FEVR);
    check_call_in(&nested, &row);
  }
}

// In an ISR, or with no task, errors in the arguments still come first, and
// the context check comes before the object checks.
static void
test_wait_context_isr_and_absent_current_keep_argument_error_priority(void) {
  static const context_t contexts[] = {
      {"no current task", TRUE, 0, FALSE},
      {"an ISR", FALSE, 1, FALSE},
  };
  const row_t rows[] = {
      {"tk_slp_tsk: time -2", API_SLP, USE_FIXTURE_ID, 1, -2, 1, TWF_ORW, FALSE,
       E_PAR},
      {"tk_wai_sem: time -2", API_SEM, USE_FIXTURE_ID, 1, -2, 1, TWF_ORW, FALSE,
       E_PAR},
      {"tk_wai_sem: ID 0", API_SEM, 0, 1, TMO_POL, 1, TWF_ORW, FALSE, E_ID},
      {"tk_wai_sem: no such semaphore", API_SEM, UNUSED_ID, 1, TMO_POL, 1,
       TWF_ORW, FALSE, E_CTX},
      {"tk_wai_sem: count 0", API_SEM, USE_FIXTURE_ID, 0, TMO_POL, 1, TWF_ORW,
       FALSE, E_PAR},
      // Above the maximum of the semaphore: that needs the semaphore, so the
      // context check comes first.
      {"tk_wai_sem: count 2", API_SEM, USE_FIXTURE_ID, 2, TMO_POL, 1, TWF_ORW,
       FALSE, E_CTX},
      {"tk_wai_flg: time -2", API_FLG, USE_FIXTURE_ID, 1, -2, 1, TWF_ORW, FALSE,
       E_PAR},
      {"tk_wai_flg: ID 0", API_FLG, 0, 1, TMO_POL, 1, TWF_ORW, FALSE, E_ID},
      {"tk_wai_flg: no such flag", API_FLG, UNUSED_ID, 1, TMO_POL, 1, TWF_ORW,
       FALSE, E_CTX},
      {"tk_wai_flg: no place for the pattern", API_FLG, USE_FIXTURE_ID, 1,
       TMO_POL, 1, TWF_ORW, TRUE, E_PAR},
      {"tk_wai_flg: empty pattern", API_FLG, USE_FIXTURE_ID, 1, TMO_POL, 0,
       TWF_ORW, FALSE, E_PAR},
      {"tk_rcv_mbx: time -2", API_MBX, USE_FIXTURE_ID, 1, -2, 1, TWF_ORW, FALSE,
       E_PAR},
      {"tk_rcv_mbx: ID 0", API_MBX, 0, 1, TMO_POL, 1, TWF_ORW, FALSE, E_ID},
      {"tk_rcv_mbx: no such mailbox", API_MBX, UNUSED_ID, 1, TMO_POL, 1,
       TWF_ORW, FALSE, E_CTX},
      {"tk_rcv_mbx: no place for the message", API_MBX, USE_FIXTURE_ID, 1,
       TMO_POL, 1, TWF_ORW, TRUE, E_PAR},
  };
  for (UINT c = 0; c < HOST_COUNT(contexts); ++c) {
    for (UINT i = 0; i < HOST_COUNT(rows); ++i) {
      host_reset();
      fresh(TRUE);
      check_call_in(&contexts[c], &rows[i]);
    }
  }
}

void run_wait_context_tests(void) {
  UnitySetTestFile(__FILE__);
  RUN_TEST(
      test_wait_context_di_isr_and_absent_current_refuse_every_wait_without_changes);
  RUN_TEST(
      test_wait_context_nested_trap_still_counts_as_isr_state_for_every_wait);
  RUN_TEST(
      test_wait_context_isr_and_absent_current_keep_argument_error_priority);
  RUN_TEST(test_wait_context_argument_errors_come_before_context_check);
  RUN_TEST(test_wait_context_check_comes_before_object_checks);
  RUN_TEST(test_wait_context_check_comes_before_one_waiter_check_of_flag);
  RUN_TEST(
      test_tk_dly_tsk_zero_with_interrupts_on_changes_nothing_and_leaves_them_on);
}
