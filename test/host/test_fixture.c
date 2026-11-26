/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of the test setup itself: setUp() resets the fake machine and the
// kernel before every test, and a test must not see what an earlier test left
// behind, or the tests would pass or fail depending on their order.
// The PLIC model is checked here too: the controller tests rely on it claiming
// and completing as the real one does.

#include <tk/tkernel.h>
#include <unity.h>

#include "fakes.h"
#include "task.h"

// Creates kernel objects until the kernel has no more, and returns how many it
// made. Nothing deletes them.
static UINT create_flags_until_full(void) {
  UINT created = 0;
  for (;;) {
    ID flgid = tk_cre_flg(&(T_CFLG){.exinf = 0, .flgatr = TA_TFIFO});
    if (flgid < 0) {
      TEST_ASSERT_EQUAL(E_LIMIT, flgid);
      return created;
    }
    ++created;
  }
}

static UINT create_mailboxes_until_full(void) {
  UINT created = 0;
  for (;;) {
    ID mbxid = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TFIFO});
    if (mbxid < 0) {
      TEST_ASSERT_EQUAL(E_LIMIT, mbxid);
      return created;
    }
    ++created;
  }
}

// Uses up every kernel object, and leaves a task on the timer queue and others
// on the ready queues.
static void use_up_the_kernel(void) {
  ID sleeper = host_make_task(4);
  host_set_current(sleeper);
  HOST_EXPECT_WAIT(tk_dly_tsk(50));
  for (UINT i = 1; i < CFN_MAX_TSKID; ++i) {
    host_make_task(5);
  }
  TEST_ASSERT_EQUAL_UINT(CFN_MAX_SEMID, host_count_creatable_semaphores());
  TEST_ASSERT_EQUAL_UINT(CFN_MAX_FLGID, create_flags_until_full());
  TEST_ASSERT_EQUAL_UINT(CFN_MAX_MBXID, create_mailboxes_until_full());
}

// After host_reset() the kernel is as after a boot, however much the previous
// test used: every object is free again, no task is queued, the timer queue
// and the access log are empty, and the counting handler has counted nothing.
static void test_kernel_reset_gives_back_every_kernel_object(void) {
  use_up_the_kernel();
  host_log_count = HOST_LOG_MAX;
  host_counting_handler(CFN_MAX_INTNO - 1);
  host_reset();

  TEST_ASSERT_EQUAL_UINT(0, host_handler_calls);
  TEST_ASSERT_EQUAL_UINT(0, host_handler_intno);

  TEST_ASSERT_NULL(tkmc_get_highest_priority_task());
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  TEST_ASSERT_NULL(current);
  TEST_ASSERT_NULL(next);
  TEST_ASSERT_TRUE(host_interrupts_enabled());
  TEST_ASSERT_EQUAL_UINT(0, host_log_count);
  host_check_kernel_queues();

  // Every task and every object can be made again.
  for (UINT i = 0; i < CFN_MAX_TSKID; ++i) {
    host_make_task(5);
  }
  TEST_ASSERT_EQUAL_UINT(CFN_MAX_SEMID, host_count_creatable_semaphores());
  TEST_ASSERT_EQUAL_UINT(CFN_MAX_FLGID, create_flags_until_full());
  TEST_ASSERT_EQUAL_UINT(CFN_MAX_MBXID, create_mailboxes_until_full());
}

static void
test_plic_model_claims_enabled_pending_sources_by_priority_then_id(void) {
  const UINT highest = 70, tied_small = 33, tied_large = 40, lowest = 10;
  const UINT disabled = 11, zero_priority = 12;
  host_plic_set_enabled(highest, TRUE);
  host_plic_set_enabled(tied_small, TRUE);
  host_plic_set_enabled(tied_large, TRUE);
  host_plic_set_enabled(lowest, TRUE);
  host_plic_set_enabled(zero_priority, TRUE);
  host_plic_set_priority(highest, 7);
  host_plic_set_priority(tied_small, 3);
  host_plic_set_priority(tied_large, 3);
  host_plic_set_priority(lowest, 1);
  host_plic_set_priority(disabled, 7);
  host_plic_set_priority(zero_priority, 0);
  host_plic_set_threshold(7);
  host_plic_raise(highest);
  host_plic_raise(tied_small);
  host_plic_raise(tied_large);
  host_plic_raise(lowest);
  host_plic_raise(disabled);
  host_plic_raise(zero_priority);

  TEST_ASSERT_EQUAL_UINT(highest, in_w(host_plic_claim_port()));
  host_plic_raise(highest);
  TEST_ASSERT_FALSE_MESSAGE(host_plic_pending(highest),
                            "a claimed source became pending again");
  out_w(host_plic_claim_port(), highest);
  host_plic_raise(highest);
  TEST_ASSERT_TRUE_MESSAGE(host_plic_pending(highest),
                           "a completed source was not raised again");
  const UINT order[] = {highest, tied_small, tied_large, lowest};
  for (UINT i = 0; i < HOST_COUNT(order); ++i) {
    UINT claimed = in_w(host_plic_claim_port());
    TEST_ASSERT_EQUAL_UINT(order[i], claimed);
    out_w(host_plic_claim_port(), claimed);
  }
  TEST_ASSERT_EQUAL_UINT(0, in_w(host_plic_claim_port()));
  TEST_ASSERT_TRUE(CheckInt(disabled));
  TEST_ASSERT_TRUE(CheckInt(zero_priority));
  TEST_ASSERT_EQUAL_UINT(0, host_plic_ignored_completions());
}

static void
test_plic_model_keeps_claim_on_disabled_completion_until_reenabled(void) {
  const UINT src = 30;
  host_plic_set_enabled(src, TRUE);
  host_plic_set_priority(src, 3);
  host_plic_raise(src);
  TEST_ASSERT_EQUAL_UINT(src, in_w(host_plic_claim_port()));
  host_plic_set_enabled(src, FALSE);
  out_w(host_plic_claim_port(), src);
  host_plic_raise(src);
  TEST_ASSERT_FALSE_MESSAGE(CheckInt(src), "a disabled completion released");
  TEST_ASSERT_EQUAL_UINT(1, host_plic_ignored_completions());
  host_plic_set_enabled(src, TRUE);
  out_w(host_plic_claim_port(), src);
  host_plic_raise(src);
  TEST_ASSERT_TRUE_MESSAGE(CheckInt(src), "an enabled completion did not");
  UINT reclaimed = in_w(host_plic_claim_port());
  TEST_ASSERT_EQUAL_UINT(src, reclaimed);
  out_w(host_plic_claim_port(), reclaimed);
}

void run_fixture_tests(void) {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_kernel_reset_gives_back_every_kernel_object);
  RUN_TEST(test_plic_model_claims_enabled_pending_sources_by_priority_then_id);
  RUN_TEST(test_plic_model_keeps_claim_on_disabled_completion_until_reenabled);
}
