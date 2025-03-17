/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>

extern void task1(INT stacd, void *exinf);
extern void task2(INT stacd, void *exinf);

static UW task1_stack[256] __attribute__((aligned(16)));
static UW task2_stack[256] __attribute__((aligned(16)));

static ID s_id_map[2] = {
    0,
    0,
};

enum TASK_INDEX {
  TASK1 = 0,
  TASK2 = 1,
  TASK_NBOF,
};

ID get_tskid(unsigned int index) {
  if (index >= TASK_NBOF) {
    return E_PAR;
  }
  return s_id_map[index];
}

static const char task1_exinf[] = "Task1";
static const char task2_exinf[] = "Task2";

void usermain(int _a0) __attribute__((weak));
void usermain(int _a0) {

  T_CTSK pk_ctsk1 = {
      .exinf = (void *)task1_exinf,
      .tskatr = TA_USERBUF,
      .task = (FP)task1,
      .itskpri = 2,
      .stksz = sizeof(task1_stack),
      .bufptr = task1_stack,
  };

  T_CTSK pk_ctsk2 = {
      .exinf = (void *)task2_exinf,
      .tskatr = TA_USERBUF,
      .task = (FP)task2,
      .itskpri = 2,
      .stksz = sizeof(task2_stack),
      .bufptr = task2_stack,
  };
  ID task1_id = tk_cre_tsk(&pk_ctsk1);
  ID task2_id = tk_cre_tsk(&pk_ctsk2);

  s_id_map[TASK1] = task1_id;
  s_id_map[TASK2] = task2_id;

  tk_sta_tsk(task1_id, 0);
  tk_sta_tsk(task2_id, 0);
}
