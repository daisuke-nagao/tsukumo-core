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
//
// A refusal is checked to change nothing the kernel keeps: every task, ready
// queue, timer queue, semaphore, event flag and mailbox as bytes, the current
// and next task, and the context switch requests (the writes to the software
// interrupt register; its level is no use, setup may have left it set).
// host_check_kernel_queues() is not enough for this: it accepts a task that is
// waiting without being on any queue.
//
// Errors in the arguments come first, and the check comes before the object
// checks (E_NOEXS, E_OBJ), the immediate success and the poll.

#include <stdio.h>

#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "event_flag.h"
#include "fakes.h"
#include "mailbox.h"
#include "semaphore.h"
#include "task.h"

static const UINT FLAG_SENTINEL = 0xdeadbeefU;

enum {
  HIGH_PRIORITY = 4,
  MSIP = CLINT_BASE_ADDRESS + CLINT_MSIP_OFFSET,
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
// The priorities must be equal: tk_dly_tsk(0) yields, and a yield that a
// refusal wrongly makes changes nothing visible when the running task is alone
// at its priority. With ready set every call that could succeed at once has
// what it needs: a wake-up request is queued for the running task, the
// semaphore has a unit, the flag is set and the mailbox holds a message.
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
    TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(s_self));
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
  BOOL null_output; // tk_rcv_mbx(): no place to put the message.
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
                      row->waiptn, row->wfmode, flgptn, row->tmo);
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
  static const TMO limits[] = {TMO_FEVR, WAIT_MS, TMO_POL};
  for (api_t api = API_DLY; api < API_COUNT; ++api) {
    for (UINT i = 0; i < sizeof(limits) / sizeof(limits[0]); ++i) {
      // A delay of "for ever" is an argument error, not a wait.
      if (api == API_DLY && limits[i] == TMO_FEVR) {
        continue;
      }
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
// poll (tk_dly_tsk(0) is its poll: it yields), when the call would block.
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
      {"tk_dly_tsk: negative time", API_DLY, USE_FIXTURE_ID, 1, -2, 1, TWF_ORW,
       FALSE, E_PAR},
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
// yields to a task of the same priority and returns with interrupts still on.
// The refusal for the context moved in front of it; this keeps it working.
void test_a_delay_of_zero_with_interrupts_on_yields_and_leaves_them_on(void) {
  ID self = host_make_task(HIGH_PRIORITY);
  ID peer = host_make_task(HIGH_PRIORITY);
  host_set_current(self);
  TEST_ASSERT_EQUAL_PTR(&tkmc_tcbs[self - 1], next);
  UINT requests = host_write_count(MSIP);

  TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(0));

  TEST_ASSERT_TRUE(host_interrupts_enabled());
  TEST_ASSERT_EQUAL_PTR(&tkmc_tcbs[peer - 1], next);
  TEST_ASSERT_EQUAL_UINT(requests + 1, host_write_count(MSIP));
  host_check_kernel_queues();
}
