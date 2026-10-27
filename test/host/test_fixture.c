/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of the test setup itself: setUp() resets the fake machine and the
// kernel before every test, and a test must not see what an earlier test left
// behind, or the tests would pass or fail depending on their order.

#include <tk/tkernel.h>
#include <unity.h>

#include "fakes.h"
#include "task.h"

// Creates kernel objects until the kernel has no more, and returns how many it
// made. Nothing deletes them.
static UINT create_semaphores_until_full(void) {
  UINT created = 0;
  for (;;) {
    ID semid = tk_cre_sem(
        &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
    if (semid < 0) {
      TEST_ASSERT_EQUAL(E_LIMIT, semid);
      return created;
    }
    ++created;
  }
}

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
  TEST_ASSERT_EQUAL_UINT(CFN_MAX_SEMID, create_semaphores_until_full());
  TEST_ASSERT_EQUAL_UINT(CFN_MAX_FLGID, create_flags_until_full());
  TEST_ASSERT_EQUAL_UINT(CFN_MAX_MBXID, create_mailboxes_until_full());
}

// After host_reset() the kernel is as after a boot, however much the previous
// test used: every object is free again, no task is queued, and the timer
// queue and the access log are empty.
void test_a_reset_gives_back_every_kernel_object(void) {
  use_up_the_kernel();
  host_log_count = HOST_LOG_MAX;
  host_reset();

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
  TEST_ASSERT_EQUAL_UINT(CFN_MAX_SEMID, create_semaphores_until_full());
  TEST_ASSERT_EQUAL_UINT(CFN_MAX_FLGID, create_flags_until_full());
  TEST_ASSERT_EQUAL_UINT(CFN_MAX_MBXID, create_mailboxes_until_full());
}
