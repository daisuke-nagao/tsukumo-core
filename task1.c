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
  TASK1 = 0, // Index for task1
  TASK2,     // Index for task2
  TASK3,     // Index for task3
  TASK_NBOF, // Total number of tasks
};

// Prototype declaration of static functions
static void task1_test_timeout(INT, void *);      // Tests timeout behavior
static void task1_test_release_wait(INT, void *); // Tests release wait behavior
static void task1_test_wakeup_multiple(INT, void *); // Tests multiple wakeups
static void task1_test_sleep_with_releases(INT,
                                           void *); // Tests sleep with releases
static void task1_test_sleep_with_wakeups(INT,
                                          void *); // Tests sleep with wakeups
static void task1_test_sleep_with_releases(
    INT, void *); // Duplicate declaration (to be removed if unnecessary)

void task1(INT stacd, void *exinf) {
  putstring("task1 start\n");

  // Test case: task1_test_timeout is expected to enter TMO state because no one
  // will wake it up
  {
    // Create and start task1_test_timeout
    ID tskid = tk_cre_tsk(&(T_CTSK){
        .exinf = NULL,
        .tskatr =
            TA_HLNG | TA_USERBUF,   // High-level language task with user buffer
        .task = task1_test_timeout, // Task function
        .itskpri = 1,               // Higher priority than task1
        .stksz = sizeof(g_stack1_1024B), // Stack size
        .bufptr = g_stack1_1024B});      // Stack buffer
    TEST_ASSERT_GREATER_THAN(0, tskid);
    if (tskid < 0) {
      putstring("tskid < 0\n"); // Error message
      tk_exd_tsk();             // Exit task
    }

    ER ercd = tk_sta_tsk(tskid, stacd); // Start task1_test_timeout
    TEST_ASSERT_EQUAL(E_OK, ercd);
    if (ercd != E_OK) {
      putstring("tk_sta_tsk(tskid, stacd) != E_OK\n"); // Error message
      tk_exd_tsk();                                    // Exit task
    }
  }

  tk_dly_tsk(100); // Delay for 100 ms to allow task1_test_timeout to complete

  // Test case: Start task1_test_release_wait
  {
    ID tskid = tk_cre_tsk(&(T_CTSK){
        .exinf = (void *)"task1_test_release_wait", // Task-specific information
        .tskatr =
            TA_HLNG | TA_USERBUF, // High-level language task with user buffer
        .task = task1_test_release_wait, // Task function
        .itskpri = 1, // Same priority as task1_test_sleep_with_releases
        .stksz = sizeof(g_stack1_1024B), // Stack size
        .bufptr = g_stack1_1024B});      // Stack buffer
    TEST_ASSERT_GREATER_THAN(0, tskid);
    if (tskid < 0) {
      putstring("tskid < 0\n"); // Error message
      tk_exd_tsk();             // Exit task
    }

    ER ercd = tk_sta_tsk(tskid, stacd); // Start task1_test_release_wait
    TEST_ASSERT_EQUAL(E_OK, ercd);
    if (ercd != E_OK) {
      putstring("tk_sta_tsk(tskid, stacd) != E_OK\n"); // Error message
      tk_exd_tsk();                                    // Exit task
    }
  }

  tk_dly_tsk(
      100); // Delay for 100 ms to allow task1_test_release_wait to complete

  // Test case: Start task1_test_wakeup_multiple
  {
    ID tskid = tk_cre_tsk(&(T_CTSK){
        .exinf = NULL,
        .tskatr =
            TA_HLNG | TA_USERBUF, // High-level language task with user buffer
        .task = task1_test_wakeup_multiple, // Task function
        .itskpri = 1,                       // Higher priority than task1
        .stksz = sizeof(g_stack1_1024B),    // Stack size
        .bufptr = g_stack1_1024B});         // Stack buffer
    TEST_ASSERT_GREATER_THAN(0, tskid);
    if (tskid < 0) {
      putstring("tskid < 0\n"); // Error message
      tk_exd_tsk();             // Exit task
    }

    ER ercd = tk_sta_tsk(tskid, stacd); // Start task1_test_wakeup_multiple
    TEST_ASSERT_EQUAL(E_OK, ercd);
    if (ercd != E_OK) {
      putstring("tk_sta_tsk(tskid, stacd) != E_OK\n"); // Error message
      tk_exd_tsk();                                    // Exit task
    }

    tk_slp_tsk(TMO_FEVR); // Wait indefinitely for task1_test_wakeup_multiple to
                          // complete
  }

  tk_dly_tsk(
      100); // Delay for 100 ms to allow task1_test_wakeup_multiple to complete

  ER ercd = tk_slp_tsk(TMO_FEVR); // Wait indefinitely for all tasks to complete
  TEST_ASSERT_EQUAL(E_OK, ercd);

  // Start the next task (task2)
  ID next_tskid = get_tskid(TASK2);
  TEST_ASSERT_GREATER_THAN(0, next_tskid);
  if (next_tskid > 0) {
    tk_sta_tsk(next_tskid, stacd); // Start task2
  }

  putstring("task1 finish\n");
  tk_exd_tsk(); // Exit task1
}

// Task function: task1_test_timeout
// This task is expected to enter TMO (timeout) state because no one will wake
// it up
static void task1_test_timeout(INT stacd, void *exinf) {
  putstring("task1_test_timeout start\n");

  ER ercd = tk_slp_tsk(50); // Sleep for 50 ms and expect timeout
  TEST_ASSERT_EQUAL(E_TMOUT, ercd);

  putstring("task1_test_timeout finish\n");

  tk_exd_tsk(); // Exit task
}

static void task1_test_release_wait(INT stacd, void *exinf) {
  putstring("task1_test_release_wait start\n");

  // Create and start task1_test_sleep_with_releases
  ID task1_b_id = tk_cre_tsk(&(T_CTSK){
      .exinf = NULL,
      .tskatr =
          TA_HLNG | TA_USERBUF, // High-level language task with user buffer
      .task = task1_test_sleep_with_releases, // Task function
      .itskpri = 1, // Same priority as task1_test_sleep_with_releases
      .stksz = sizeof(g_stack2_1024B), // Stack size
      .bufptr = g_stack2_1024B});      // Stack buffer
  TEST_ASSERT_GREATER_THAN(0, task1_b_id);
  if (task1_b_id < 0) {
    putstring("task1_b_id < 0\n"); // Error message
    tk_exd_tsk();                  // Exit task
  }

  ER ercd =
      tk_sta_tsk(task1_b_id, stacd); // Start task1_test_sleep_with_releases
  TEST_ASSERT_EQUAL(E_OK, ercd);

  extern void tkmc_yield(void);
  tkmc_yield(); // Yield control to other tasks

  // Release wait state for task1_test_sleep_with_releases
  ercd = tk_rel_wai(task1_b_id);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  tkmc_yield();
  ercd = tk_rel_wai(task1_b_id);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  tkmc_yield();

  // Wake up task1
  ID task1_id = get_tskid(TASK1);
  tk_wup_tsk(task1_id);

  putstring("task1_test_release_wait finish\n");
  tk_exd_tsk(); // Exit task
}

void task1_test_sleep_with_releases(INT stacd, void *exinf) {
  putstring("task1_test_sleep_with_releases start\n");

  ER ercd;
  ercd = tk_slp_tsk(50); // Sleep for 50 ms and expect release wait
  TEST_ASSERT_EQUAL(E_RLWAI, ercd);
  ercd = tk_slp_tsk(50); // Sleep for 50 ms and expect release wait
  TEST_ASSERT_EQUAL(E_RLWAI, ercd);
  ercd = tk_slp_tsk(50); // Sleep for 50 ms and expect timeout
  TEST_ASSERT_EQUAL(E_TMOUT, ercd);

  putstring("task1_test_sleep_with_releases finish\n");
  tk_exd_tsk(); // Exit task
}

// Task function: task1_test_sleep_with_wakeups
// Similar to task1_test_sleep_with_releases but expects 3 E_OK results and 1
// E_TMOUT
static void task1_test_sleep_with_wakeups(INT stacd, void *exinf) {
  putstring("task1_test_sleep_with_wakeups start\n");

  ER ercd;
  ercd = tk_slp_tsk(50); // Sleep for 50 ms and expect wakeup
  TEST_ASSERT_EQUAL(E_OK, ercd);
  ercd = tk_slp_tsk(50); // Sleep for 50 ms and expect wakeup
  TEST_ASSERT_EQUAL(E_OK, ercd);
  ercd = tk_slp_tsk(50); // Sleep for 50 ms and expect wakeup
  TEST_ASSERT_EQUAL(E_OK, ercd);
  ercd = tk_slp_tsk(50); // Sleep for 50 ms and expect timeout
  TEST_ASSERT_EQUAL(E_TMOUT, ercd);

  putstring("task1_test_sleep_with_wakeups finish\n");
  tk_exd_tsk(); // Exit task
}

// Task function: task1_test_wakeup_multiple
// Executes tk_wup_tsk 3 times, similar to task1_test_release_wait
static void task1_test_wakeup_multiple(INT stacd, void *exinf) {
  putstring("task1_test_wakeup_multiple start\n");

  // Create and start task1_test_sleep_with_wakeups
  ID task1_b_id = tk_cre_tsk(&(T_CTSK){
      .exinf = NULL,
      .tskatr =
          TA_HLNG | TA_USERBUF, // High-level language task with user buffer
      .task = task1_test_sleep_with_wakeups, // Task function
      .itskpri = 1, // Same priority as task1_test_sleep_with_releases
      .stksz = sizeof(g_stack2_1024B), // Stack size
      .bufptr = g_stack2_1024B});      // Stack buffer
  TEST_ASSERT_GREATER_THAN(0, task1_b_id);
  if (task1_b_id < 0) {
    putstring("task1_b_id < 0\n"); // Error message
    tk_exd_tsk();                  // Exit task
  }

  ER ercd =
      tk_sta_tsk(task1_b_id, stacd); // Start task1_test_sleep_with_wakeups
  TEST_ASSERT_EQUAL(E_OK, ercd);

  extern void tkmc_yield(void);
  tkmc_yield(); // Yield control to other tasks

  // Wake up task1_test_sleep_with_wakeups multiple times
  ercd = tk_wup_tsk(task1_b_id);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  tkmc_yield();
  ercd = tk_wup_tsk(task1_b_id);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  tkmc_yield();
  ercd = tk_wup_tsk(task1_b_id);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  tkmc_yield();

  // Wake up task1
  ID task1_id = get_tskid(TASK1);
  tk_wup_tsk(task1_id);

  putstring("task1_test_wakeup_multiple finish\n");
  tk_exd_tsk(); // Exit task
}
