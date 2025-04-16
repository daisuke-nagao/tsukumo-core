/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>
#include <unity.h>

#include "putstring.h"
#include "tasks.h"

static UW task1_stack[256] __attribute__((aligned(16)));
static UW task2_stack[256] __attribute__((aligned(16)));
static UW task3_stack[256] __attribute__((aligned(16)));
static UW task4_stack[256] __attribute__((aligned(16)));

static const char task1_exinf[] = "Task1";
static const char task2_exinf[] = "Task2";

T_CTSK s_pk_ctsk[TASK_NBOF] = {
    {
        .exinf = (void *)task1_exinf,
        .tskatr = TA_USERBUF,
        .task = (FP)task1,
        .itskpri = 2,
        .stksz = sizeof(task1_stack),
        .bufptr = task1_stack,
    },
    {
        .exinf = (void *)task2_exinf,
        .tskatr = TA_USERBUF,
        .task = (FP)task2,
        .itskpri = 2,
        .stksz = sizeof(task2_stack),
        .bufptr = task2_stack,
    },
    {
        .exinf = (void *)NULL,
        .tskatr = TA_USERBUF,
        .task = (FP)task3,
        .itskpri = 3,
        .stksz = sizeof(task3_stack),
        .bufptr = task3_stack,
    },
    {
        .exinf = (void *)NULL,
        .tskatr = TA_USERBUF,
        .task = (FP)task4,
        .itskpri = 3,
        .stksz = sizeof(task4_stack),
        .bufptr = task4_stack,
    },
};

void usermain(int _a0) {
  for (int i = 0; i < sizeof(s_pk_ctsk) / sizeof(s_pk_ctsk[0]); ++i) {
    ID tskid = tk_cre_tsk(&s_pk_ctsk[i]);
    TEST_ASSERT_GREATER_THAN(0, tskid);
    if (tskid > 0) {
      ER ercd = set_tskid(i, tskid);
      (void)ercd;
    }
  }

  ER ercd = tk_sta_tsk(get_tskid(TASK1), TASK1);
  TEST_ASSERT_EQUAL(E_OK, ercd);
}

void (*outputChar)(int) = tkmc_putchar;

void setUp(void) {
  // Initialization code for tests can be added here.
}

void tearDown(void) {
  // Cleanup code for tests can be added here.
}