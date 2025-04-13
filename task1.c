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

// comment must be written in English
// Prototype declaration of static functions task1_a/b
static void task1_expect_tmo(INT stacd, void *exinf);
static void task1_a(INT stacd, void *exinf);
static void task1_b(INT stacd, void *exinf);
static void task1_rel_wai_twice(INT stacd, void *exinf);
static void task1_wup_tsk_third_time(INT stacd, void *exinf);

static const char task1_a_exinf[] = "task1_a";
static const char task1_b_exinf[] = "task1_b";

// task1 launches task1_a, which in turn creates task1_b and executes it.
void task1(INT stacd, void *exinf) {

  putstring("task1 start\n");

  // task1_expect_tmo is expected to be in TMO state because no one will wake it
  // up
  {
    // create task1_expect_tmo and start it
    ID task1_expect_tmo_id = tk_cre_tsk(&(T_CTSK){
        .exinf = NULL,
        .tskatr = TA_HLNG | TA_USERBUF,
        .task = task1_expect_tmo,
        .itskpri = 1, // task1_expect_tmo is higher priority than task1
        .stksz = sizeof(g_stack1_1024B),
        .bufptr = g_stack1_1024B});
    TEST_ASSERT_GREATER_THAN(0, task1_expect_tmo_id);
    if (task1_expect_tmo_id < 0) {
      putstring("task1_expect_tmo_id < 0\n");
      tk_exd_tsk();
    }

    ER ercd = tk_sta_tsk(task1_expect_tmo_id, stacd); // task1 is preempted
    TEST_ASSERT_EQUAL(E_OK, ercd);
    if (ercd != E_OK) {
      putstring("tk_sta_tsk(task1_expect_tmo_id, stacd) != E_OK\n");
      tk_exd_tsk();
    }
  }

  tk_dly_tsk(100); // sleep 100 ms because task1_expect_tmo waits for 50 ms

  {
    // Add code to start task1_rel_wai_twice
    ID task1_rel_wai_twice_id =
        tk_cre_tsk(&(T_CTSK){.exinf = (void *)"task1_rel_wai_twice",
                             .tskatr = TA_HLNG | TA_USERBUF,
                             .task = task1_rel_wai_twice,
                             .itskpri = 1, // Same priority as task1_a
                             .stksz = sizeof(g_stack1_1024B),
                             .bufptr = g_stack1_1024B});
    TEST_ASSERT_GREATER_THAN(0, task1_rel_wai_twice_id);
    if (task1_rel_wai_twice_id < 0) {
      putstring("task1_rel_wai_twice_id < 0\n");
      tk_exd_tsk();
    }

    ER ercd = tk_sta_tsk(task1_rel_wai_twice_id, stacd);
    TEST_ASSERT_EQUAL(E_OK, ercd);
    if (ercd != E_OK) {
      putstring("tk_sta_tsk(task1_rel_wai_twice_id, stacd) != E_OK\n");
      tk_exd_tsk();
    }
  }

  tk_dly_tsk(100); // sleep 100 ms to let task1_rel_wai_twice finish
  {
    ID task1_a_id = tk_cre_tsk(
        &(T_CTSK){.exinf = (void *)task1_a_exinf,
                  .tskatr = TA_HLNG | TA_USERBUF,
                  .task = task1_wup_tsk_third_time,
                  .itskpri = 1, // task1_a is higher priority than task1
                  .stksz = sizeof(g_stack1_1024B),
                  .bufptr = g_stack1_1024B});
    TEST_ASSERT_GREATER_THAN(0, task1_a_id);
    if (task1_a_id < 0) {
      putstring("task1_a_id < 0\n");
      tk_exd_tsk();
    }

    ER ercd = tk_sta_tsk(task1_a_id, stacd);
    // this task is preempted by task1_a
    TEST_ASSERT_EQUAL(E_OK, ercd);
    if (ercd != E_OK) {
      putstring("tk_sta_tsk(task1_a_id, stacd) != E_OK\n");
      // task1_a/b must be deleted here, but tk_del_tsk() is not implemented
      // yet.
      tk_exd_tsk();
    }

    tk_slp_tsk(TMO_FEVR); // wait for task1_a to finish
  }

  tk_dly_tsk(100); // sleep 100 ms to let task1_a finish

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
static void task1_expect_tmo(INT stacd, void *exinf) {
  putstring("task1_expect_tmo start\n");

  ER ercd = tk_slp_tsk(50); // expect to be in TMO state
  TEST_ASSERT_EQUAL(E_TMOUT, ercd);

  putstring("task1_expect_tmo finish\n");

  tk_exd_tsk();
}

static void task1_slp_tsk_a(INT, void *);

static void task1_rel_wai_twice(INT stacd, void *exinf) {
  putstring("task1_rel_wai_twice start\n");

  ID task1_b_id = tk_cre_tsk(
      &(T_CTSK){.exinf = (void *)task1_b_exinf,
                .tskatr = TA_HLNG | TA_USERBUF,
                .task = task1_slp_tsk_a, // Expect tk_slp_tsk to be RELWAI
                .itskpri = 1, // task1_b is the same priority as task1_a
                .stksz = sizeof(g_stack2_1024B),
                .bufptr = g_stack2_1024B});
  TEST_ASSERT_GREATER_THAN(0, task1_b_id);
  if (task1_b_id < 0) {
    putstring("task1_b_id < 0\n");
    // task1_a must be deleted here, but tk_del_tsk() is not implemented yet.
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

  putstring("task1_rel_wai_twice finish\n");
  tk_exd_tsk();
}

void task1_slp_tsk_a(INT stacd, void *exinf) {
  putstring("task1_slp_tsk_a start\n");

  ER ercd;
  ercd = tk_slp_tsk(50);
  TEST_ASSERT_EQUAL(E_RLWAI, ercd);
  ercd = tk_slp_tsk(50);
  TEST_ASSERT_EQUAL(E_RLWAI, ercd);
  ercd = tk_slp_tsk(50);
  TEST_ASSERT_EQUAL(E_TMOUT, ercd);

  putstring("task1_slp_tsk_a finish\n");
  tk_exd_tsk();
}

static void task1_slp_tsk_b(INT stacd, void *exinf);
// task1_rel_wai_twiceみたいに、tk_wup_tskを3回実行するタスクを実装する
static void task1_wup_tsk_third_time(INT stacd, void *exinf) {
  putstring("task1_wup_tsk_third_time start\n");

  ID task1_b_id = tk_cre_tsk(
      &(T_CTSK){.exinf = (void *)task1_b_exinf,
                .tskatr = TA_HLNG | TA_USERBUF,
                .task = task1_slp_tsk_b,
                .itskpri = 1, // task1_b is the same priority as task1_a
                .stksz = sizeof(g_stack2_1024B),
                .bufptr = g_stack2_1024B});
  TEST_ASSERT_GREATER_THAN(0, task1_b_id);
  if (task1_b_id < 0) {
    putstring("task1_b_id < 0\n");
    // task1_a must be deleted here, but tk_del_tsk() is not implemented yet.
    tk_exd_tsk();
  }
  ER ercd = E_OK;

  ercd = tk_sta_tsk(task1_b_id, stacd);
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

  putstring("task1_wup_tsk_third_time finish\n");
  tk_exd_tsk();
}

// task1_slp_tsk_aみたいなタスクだけど期待結果は３回E_OKで４回目にE_TMOUTになること
static void task1_slp_tsk_b(INT stacd, void *exinf) {
  putstring("task1_slp_tsk_b start\n");

  ER ercd;
  ercd = tk_slp_tsk(50);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  ercd = tk_slp_tsk(50);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  ercd = tk_slp_tsk(50);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  ercd = tk_slp_tsk(50);
  TEST_ASSERT_EQUAL(E_TMOUT, ercd);

  putstring("task1_slp_tsk_b finish\n");
  tk_exd_tsk();
}
