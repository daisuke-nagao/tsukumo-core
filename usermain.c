/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>

extern void task1(INT stacd, void *exinf);
extern void task2(INT stacd, void *exinf);
extern void task3(INT stacd, void *exinf);
extern void task4(INT stacd, void *exinf);

static UW task1_stack[256] __attribute__((aligned(16)));
static UW task2_stack[256] __attribute__((aligned(16)));
static UW task3_stack[256] __attribute__((aligned(16)));
static UW task4_stack[256] __attribute__((aligned(16)));

enum TASK_INDEX {
  TASK1 = 0,
  TASK2,
  TASK3,
  TASK4,
  TASK_NBOF,
};

static ID s_id_map[TASK_NBOF] = {
    0,
    0,
    0,
    0,
};

ID get_tskid(unsigned int index) {
  if (index >= TASK_NBOF) {
    return E_PAR;
  }
  return s_id_map[index];
}

static const char task1_exinf[] = "Task1";
static const char task2_exinf[] = "Task2";

T_CTSK s_pk_ctsk[] = {
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
    if (tskid > 0) {
      s_id_map[i] = tskid;
    }
  }

  tk_sta_tsk(get_tskid(TASK1), TASK1);
}
