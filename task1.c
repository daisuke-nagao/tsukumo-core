/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
#include <tk/tkernel.h>

#include "putstring.h"

extern ID get_tskid(unsigned int index);

// allocate stack for task1_a/b
// aligned(16) is required for RISC-V
static char task1_a_stack[1024 * 4] __attribute__((aligned(16)));
static char task1_b_stack[1024 * 4] __attribute__((aligned(16)));

enum TASK_INDEX {
  TASK1 = 0,
  TASK2,
  TASK3,
  TASK_NBOF,
};

// comment must be written in English
// Prototype declaration of static functions task1_a/b
static void task1_a(INT stacd, void *exinf);
static void task1_b(INT stacd, void *exinf);

static const char task1_a_exinf[] = "task1_a";
static const char task1_b_exinf[] = "task1_b";

// task1 launches task1_a, which in turn creates task1_b and executes it.
void task1(INT stacd, void *exinf) {

  putstring("task1 start\n");

  ID task1_a_id = tk_cre_tsk(&(T_CTSK){.exinf = (void *)task1_a_exinf,
                                       .tskatr = TA_HLNG | TA_USERBUF,
                                       .task = task1_a,
                                       .itskpri = 1,
                                       .stksz = sizeof(task1_a_stack),
                                       .bufptr = task1_a_stack});
  if (task1_a_id < 0) {
    putstring("task1_a_id < 0\n");
    tk_exd_tsk();
  }

  ER ercd = tk_sta_tsk(task1_a_id, stacd);
  // this task is preempted by task1_a
  if (ercd != E_OK) {
    putstring("tk_sta_tsk(task1_a_id, stacd) != E_OK\n");
    // task1_a/b must be deleted here, but tk_del_tsk() is not implemented yet.
    tk_exd_tsk();
  }

  tk_slp_tsk(TMO_FEVR); // wait for task1_a to finish

  ID task2_id = get_tskid(TASK2);
  tk_sta_tsk(task2_id, stacd);

  tk_exd_tsk();
}

static void task1_a(INT stacd, void *exinf) {
  putstring("task1_a start\n");

  ID task1_b_id = tk_cre_tsk(&(T_CTSK){.exinf = (void *)task1_b_exinf,
                                       .tskatr = TA_HLNG | TA_USERBUF,
                                       .task = task1_b,
                                       .itskpri = 1,
                                       .stksz = sizeof(task1_b_stack),
                                       .bufptr = task1_b_stack});
  if (task1_b_id < 0) {
    putstring("task1_b_id < 0\n");
    // task1_a must be deleted here, but tk_del_tsk() is not implemented yet.
    tk_exd_tsk();
  }

  tk_sta_tsk(task1_b_id, stacd);

  const char *msg = (const char *)exinf;
  INT c = 0;
  for (int i = 0; i < 10; ++i) {
    ER ercd = E_OK;
    ercd = tk_dly_tsk(990);
    putstring(msg);

    ++c;
    c %= 2;

    if (c) {
      tk_rel_wai(task1_b_id);
      tk_rel_wai(task1_b_id);
    } else {
      tk_wup_tsk(task1_b_id);
      tk_wup_tsk(task1_b_id);
      tk_wup_tsk(task1_b_id);
    }
    putstring(" task1_a\n");
  }

  ID task1_id = get_tskid(TASK1);
  tk_wup_tsk(task1_id);
  tk_exd_tsk();
}

static void task1_b(INT stacd, void *exinf) {
  putstring("task1_b start\n");

  const char *msg = (const char *)exinf;
  for (int i = 0; i < 30; ++i) {
    ER ercd = tk_slp_tsk(300);
    putstring(msg);
    if (ercd == E_OK) {
      putstring(" task1_b E_OK\n");
    } else if (ercd == E_RLWAI) {
      putstring(" task1_b E_RLWAI\n");
    } else if (ercd == E_TMOUT) {
      putstring(" task1_b E_TMOUT\n");
    } else {
      putstring(" task1_b !\n");
    }
  }

  tk_exd_tsk();
}
