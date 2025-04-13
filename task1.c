/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
#include <tk/tkernel.h>

#include <unity.h>

#include "putstring.h"
#include "userstack.h"

extern ID get_tskid(unsigned int index);

enum TASK_INDEX {
  TASK1 = 0,
  TASK2,
  TASK3,
  TASK_NBOF,
};

// Prototype declaration of static functions with updated names
static void task1_test_timeout(INT stacd, void *exinf);
static void task1_test_release_wait(INT stacd, void *exinf);
static void task1_test_wakeup_multiple(INT stacd, void *exinf);
static void task1_test_sleep_with_releases(INT stacd, void *exinf);
static void task1_test_sleep_with_wakeups(INT stacd, void *exinf);

static const char task1_a_exinf[] = "task1_a";
static const char task1_b_exinf[] = "task1_b";

// task1 launches task1_test_timeout, which in turn creates task1_test_release_wait and executes it.
void task1(INT stacd, void *exinf) {

  putstring("task1 start\n");

  // task1_test_timeout is expected to be in TMO state because no one will wake it up
  {
    // create task1_test_timeout and start it
    ID task1_test_timeout_id = tk_cre_tsk(&(T_CTSK){
        .exinf = NULL,
        .tskatr = TA_HLNG | TA_USERBUF,
        .task = task1_test_timeout,
        .itskpri = 1, // task1_test_timeout is higher priority than task1
        .stksz = sizeof(g_stack1_1024B),
        .bufptr = g_stack1_1024B});
    TEST_ASSERT_GREATER_THAN(0, task1_test_timeout_id);
    if (task1_test_timeout_id < 0) {
      putstring("task1_test_timeout_id < 0\n");
      tk_exd_tsk();
    }

    ER ercd = tk_sta_tsk(task1_test_timeout_id, stacd); // task1 is preempted
    TEST_ASSERT_EQUAL(E_OK, ercd);
    if (ercd != E_OK) {
      putstring("tk_sta_tsk(task1_test_timeout_id, stacd) != E_OK\n");
      tk_exd_tsk();
    }
  }

  tk_dly_tsk(100); // sleep 100 ms because task1_test_timeout waits for 50 ms

  {
    // Add code to start task1_test_release_wait
    ID task1_test_release_wait_id =
        tk_cre_tsk(&(T_CTSK){.exinf = (void *)"task1_test_release_wait",
                             .tskatr = TA_HLNG | TA_USERBUF,
                             .task = task1_test_release_wait,
                             .itskpri = 1, // Same priority as task1_test_sleep_with_releases
                             .stksz = sizeof(g_stack1_1024B),
                             .bufptr = g_stack1_1024B});
    TEST_ASSERT_GREATER_THAN(0, task1_test_release_wait_id);
    if (task1_test_release_wait_id < 0) {
      putstring("task1_test_release_wait_id < 0\n");
      tk_exd_tsk();
    }

    ER ercd = tk_sta_tsk(task1_test_release_wait_id, stacd);
    TEST_ASSERT_EQUAL(E_OK, ercd);
    if (ercd != E_OK) {
      putstring("tk_sta_tsk(task1_test_release_wait_id, stacd) != E_OK\n");
      tk_exd_tsk();
    }
  }

  tk_dly_tsk(100); // sleep 100 ms to let task1_test_release_wait finish
  {
    ID task1_test_wakeup_multiple_id = tk_cre_tsk(
        &(T_CTSK){.exinf = (void *)task1_a_exinf,
                  .tskatr = TA_HLNG | TA_USERBUF,
                  .task = task1_test_wakeup_multiple,
                  .itskpri = 1, // task1_test_wakeup_multiple is higher priority than task1
                  .stksz = sizeof(g_stack1_1024B),
                  .bufptr = g_stack1_1024B});
    TEST_ASSERT_GREATER_THAN(0, task1_test_wakeup_multiple_id);
    if (task1_test_wakeup_multiple_id < 0) {
      putstring("task1_test_wakeup_multiple_id < 0\n");
      tk_exd_tsk();
    }

    ER ercd = tk_sta_tsk(task1_test_wakeup_multiple_id, stacd);
    // this task is preempted by task1_test_wakeup_multiple
    TEST_ASSERT_EQUAL(E_OK, ercd);
    if (ercd != E_OK) {
      putstring("tk_sta_tsk(task1_test_wakeup_multiple_id, stacd) != E_OK\n");
      // task1_test_sleep_with_releases must be deleted here, but tk_del_tsk() is not implemented yet.
      tk_exd_tsk();
    }

    tk_slp_tsk(TMO_FEVR); // wait for task1_test_wakeup_multiple to finish
  }

  tk_dly_tsk(100); // sleep 100 ms to let task1_test_wakeup_multiple finish

  ER ercd = tk_slp_tsk(TMO_FEVR); // wait for all tasks to finish
  TEST_ASSERT_EQUAL(E_OK, ercd);

  // start next task
  ID task2_id = get_tskid(TASK2);
  TEST_ASSERT_GREATER_THAN(0, task2_id);
  if (task2_id > 0) {
    tk_sta_tsk(task2_id, stacd);
  }

  putstring("task1 finish\n");
  tk_exd_tsk();
}

// this task is expected to be in TMO state because no one will wake it up
static void task1_test_timeout(INT stacd, void *exinf) {
  putstring("task1_test_timeout start\n");

  ER ercd = tk_slp_tsk(50); // expect to be in TMO state
  TEST_ASSERT_EQUAL(E_TMOUT, ercd);

  putstring("task1_test_timeout finish\n");

  tk_exd_tsk();
}

static void task1_test_sleep_with_releases(INT, void *);

static void task1_test_release_wait(INT stacd, void *exinf) {
  putstring("task1_test_release_wait start\n");

  ID task1_b_id = tk_cre_tsk(
      &(T_CTSK){.exinf = (void *)task1_b_exinf,
                .tskatr = TA_HLNG | TA_USERBUF,
                .task = task1_test_sleep_with_releases, // Expect tk_slp_tsk to be RELWAI
                .itskpri = 1, // task1_b is the same priority as task1_test_sleep_with_releases
                .stksz = sizeof(g_stack2_1024B),
                .bufptr = g_stack2_1024B});
  TEST_ASSERT_GREATER_THAN(0, task1_b_id);
  if (task1_b_id < 0) {
    putstring("task1_b_id < 0\n");
    // task1_test_sleep_with_releases must be deleted here, but tk_del_tsk() is not implemented yet.
    tk_exd_tsk();
  }
  ER ercd = E_OK;

  ercd = tk_sta_tsk(task1_b_id, stacd);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  extern void tkmc_yield(void);
  tkmc_yield();

  ercd = tk_rel_wai(task1_b_id);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  tkmc_yield();
  ercd = tk_rel_wai(task1_b_id);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  tkmc_yield();

  ID task1_id = get_tskid(TASK1);
  tk_wup_tsk(task1_id);

  putstring("task1_test_release_wait finish\n");
  tk_exd_tsk();
}

void task1_test_sleep_with_releases(INT stacd, void *exinf) {
  putstring("task1_test_sleep_with_releases start\n");

  ER ercd;
  ercd = tk_slp_tsk(50);
  TEST_ASSERT_EQUAL(E_RLWAI, ercd);
  ercd = tk_slp_tsk(50);
  TEST_ASSERT_EQUAL(E_RLWAI, ercd);
  ercd = tk_slp_tsk(50);
  TEST_ASSERT_EQUAL(E_TMOUT, ercd);

  putstring("task1_test_sleep_with_releases finish\n");
  tk_exd_tsk();
}

// task1_test_sleep_with_wakeups is similar to task1_test_sleep_with_releases but expects 3 E_OK results and 1 E_TMOUT
static void task1_test_sleep_with_wakeups(INT stacd, void *exinf) {
  putstring("task1_test_sleep_with_wakeups start\n");

  ER ercd;
  ercd = tk_slp_tsk(50);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  ercd = tk_slp_tsk(50);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  ercd = tk_slp_tsk(50);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  ercd = tk_slp_tsk(50);
  TEST_ASSERT_EQUAL(E_TMOUT, ercd);

  putstring("task1_test_sleep_with_wakeups finish\n");
  tk_exd_tsk();
}

// task1_test_wakeup_multiple executes tk_wup_tsk 3 times, similar to task1_test_release_wait
static void task1_test_wakeup_multiple(INT stacd, void *exinf) {
  putstring("task1_test_wakeup_multiple start\n");

  ID task1_b_id = tk_cre_tsk(
      &(T_CTSK){.exinf = (void *)task1_b_exinf,
                .tskatr = TA_HLNG | TA_USERBUF,
                .task = task1_test_sleep_with_wakeups,
                .itskpri = 1, // task1_b has the same priority as task1_test_sleep_with_releases
                .stksz = sizeof(g_stack2_1024B),
                .bufptr = g_stack2_1024B});
  TEST_ASSERT_GREATER_THAN(0, task1_b_id);
  if (task1_b_id < 0) {
    putstring("task1_b_id < 0\n");
    tk_exd_tsk();
  }
  ER ercd = tk_sta_tsk(task1_b_id, stacd);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  extern void tkmc_yield(void);
  tkmc_yield();

  ercd = tk_wup_tsk(task1_b_id);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  tkmc_yield();
  ercd = tk_wup_tsk(task1_b_id);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  tkmc_yield();
  ercd = tk_wup_tsk(task1_b_id);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  tkmc_yield();

  ID task1_id = get_tskid(TASK1);
  tk_wup_tsk(task1_id);

  putstring("task1_test_wakeup_multiple finish\n");
  tk_exd_tsk();
}
