/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>

extern void task1(INT stacd, void *exinf);

static UW task1_stack[1024] __attribute__((aligned(16)));
static UW task2_stack[1024] __attribute__((aligned(16)));

static const char hello_world[] = "Hello, world.";
static const char fizzbuzz[] = "FizzBuzz.";

void usermain(int _a0) __attribute__((weak));
void usermain(int _a0) {

  T_CTSK pk_ctsk1 = {
      .exinf = (void *)hello_world,
      .tskatr = TA_USERBUF,
      .task = (FP)task1,
      .itskpri = 2,
      .stksz = sizeof(task1_stack),
      .bufptr = task1_stack,
  };

  T_CTSK pk_ctsk2 = {
      .exinf = (void *)fizzbuzz,
      .tskatr = TA_USERBUF,
      .task = (FP)task1,
      .itskpri = 2,
      .stksz = sizeof(task2_stack),
      .bufptr = task2_stack,
  };
  ID task1_id = tk_cre_tsk(&pk_ctsk1);
  ID task2_id = tk_cre_tsk(&pk_ctsk2);

  tk_sta_tsk(task1_id, 1);
  tk_sta_tsk(task2_id, 2);
}
