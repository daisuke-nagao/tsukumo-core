/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "tasks.h"

static ID s_id_map[TASK_NBOF] = {0, 0, 0, 0};

ID get_tskid(unsigned int index) {
  if (index >= TASK_NBOF) {
    return E_PAR;
  }
  return s_id_map[index];
}

ER set_tskid(unsigned int index, ID tskid) {
  if (index >= TASK_NBOF) {
    return E_PAR;
  }

  s_id_map[index] = tskid;

  return E_OK;
}
