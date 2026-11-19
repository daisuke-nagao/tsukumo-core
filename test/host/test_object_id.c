/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Every call that takes the ID of a task or object refuses an ID outside 1 to
// the maximum of its kind with E_ID, before it forms the address of an entry in
// the kernel's table, and changes nothing. The IDs at both ends of the range
// get past the check: with no object there they are E_NOEXS. That also catches
// a check against the maximum of another kind, except between semaphores and
// event flags, whose maxima are the same.
//
// The other arguments are valid, so the ID is the only reason for a refusal,
// and a wait call polls, so it cannot block.

#include <limits.h>
#include <stdio.h>

#include <tk/tkernel.h>
#include <unity.h>

#include "fakes.h"

static const UINT FLAG_SENTINEL = 0xdeadbeefU;

static UINT s_flgptn;
static T_MSG s_message;
static T_MSG s_sentinel;
static T_MSG *s_received;

static ER call_sta_tsk(ID id) { return tk_sta_tsk(id, 0); }
static ER call_rel_wai(ID id) { return tk_rel_wai(id); }
static ER call_wup_tsk(ID id) { return tk_wup_tsk(id); }
static ER call_wai_sem(ID id) { return tk_wai_sem(id, 1, TMO_POL); }
static ER call_sig_sem(ID id) { return tk_sig_sem(id, 1); }
static ER call_del_sem(ID id) { return tk_del_sem(id); }
static ER call_wai_flg(ID id) {
  return tk_wai_flg(id, 1, TWF_ORW, &s_flgptn, TMO_POL);
}
static ER call_set_flg(ID id) { return tk_set_flg(id, 1); }
static ER call_clr_flg(ID id) { return tk_clr_flg(id, 0); }
static ER call_snd_mbx(ID id) { return tk_snd_mbx(id, &s_message); }
static ER call_rcv_mbx(ID id) { return tk_rcv_mbx(id, &s_received, TMO_POL); }
static ER call_del_mbx(ID id) { return tk_del_mbx(id); }

typedef struct {
  const char *name;
  ER (*call)(ID id);
  ID max;
  // Wait calls need a current task before looking up an object.
  BOOL from_a_task;
  // A future API that takes a task ID needs this mark.
  BOOL takes_task_id;
} api_t;

static const api_t s_apis[] = {
    {"tk_sta_tsk", call_sta_tsk, CFN_MAX_TSKID, FALSE, TRUE},
    {"tk_rel_wai", call_rel_wai, CFN_MAX_TSKID, FALSE, TRUE},
    {"tk_wup_tsk", call_wup_tsk, CFN_MAX_TSKID, FALSE, TRUE},
    {"tk_wai_sem", call_wai_sem, CFN_MAX_SEMID, TRUE, FALSE},
    {"tk_sig_sem", call_sig_sem, CFN_MAX_SEMID, FALSE, FALSE},
    {"tk_del_sem", call_del_sem, CFN_MAX_SEMID, FALSE, FALSE},
    {"tk_wai_flg", call_wai_flg, CFN_MAX_FLGID, TRUE, FALSE},
    {"tk_set_flg", call_set_flg, CFN_MAX_FLGID, FALSE, FALSE},
    {"tk_clr_flg", call_clr_flg, CFN_MAX_FLGID, FALSE, FALSE},
    {"tk_snd_mbx", call_snd_mbx, CFN_MAX_MBXID, FALSE, FALSE},
    {"tk_rcv_mbx", call_rcv_mbx, CFN_MAX_MBXID, TRUE, FALSE},
    {"tk_del_mbx", call_del_mbx, CFN_MAX_MBXID, FALSE, FALSE},
};

// Calls the API with id on a fresh kernel and checks that it returns expected
// and changes nothing, output arguments included. A current task exists when
// the API needs one or with_current_task is set.
static void check_call(const api_t *api, ID id, ER expected,
                       BOOL with_current_task) {
  char label[64];
  snprintf(label, sizeof(label), "%s(%d)%s", api->name, (int)id,
           with_current_task ? " with a current task" : "");

  host_reset();
  if (api->from_a_task || with_current_task) {
    host_set_current(host_make_task(HOST_HIGH_PRIORITY));
  }
  s_flgptn = FLAG_SENTINEL;
  s_received = &s_sentinel;
  host_snapshot_t before;
  host_take_snapshot(&before);

  TEST_ASSERT_EQUAL_INT_MESSAGE(expected, api->call(id), label);
  TEST_ASSERT_TRUE_MESSAGE(host_interrupts_enabled(), label);
  TEST_ASSERT_EQUAL_HEX_MESSAGE(FLAG_SENTINEL, s_flgptn, label);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(&s_sentinel, s_received, label);
  host_assert_unchanged(&before, label);
}

static void test_object_id_out_of_range_is_refused_and_changes_nothing(void) {
  for (UINT i = 0; i < HOST_COUNT(s_apis); ++i) {
    const ID out_of_range[] = {0, -1, INT_MIN, s_apis[i].max + 1};
    for (UINT j = 0; j < HOST_COUNT(out_of_range); ++j) {
      check_call(&s_apis[i], out_of_range[j], E_ID, FALSE);
    }
  }
}

static void test_object_id_at_both_ends_of_range_passes_id_check(void) {
  for (UINT i = 0; i < HOST_COUNT(s_apis); ++i) {
    check_call(&s_apis[i], 1, E_NOEXS, FALSE);
    check_call(&s_apis[i], s_apis[i].max, E_NOEXS, FALSE);
  }
}

// This differs from the rows of ID 0 above only by having a current task.
static void
test_object_id_tsk_self_is_refused_with_a_current_task_and_changes_nothing(
    void) {
  for (UINT i = 0; i < HOST_COUNT(s_apis); ++i) {
    if (s_apis[i].takes_task_id) {
      check_call(&s_apis[i], TSK_SELF, E_ID, TRUE);
    }
  }
}

void run_object_id_tests(void) {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_object_id_out_of_range_is_refused_and_changes_nothing);
  RUN_TEST(test_object_id_at_both_ends_of_range_passes_id_check);
  RUN_TEST(
      test_object_id_tsk_self_is_refused_with_a_current_task_and_changes_nothing);
}
