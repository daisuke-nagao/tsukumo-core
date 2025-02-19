/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "ini_tsk.h"

extern void usermain(int _a0);
extern void tkmc_ext_tsk(void);

void tkmc_ini_tsk(INT stacd, void *exinf) {
  usermain(stacd);
  tkmc_ext_tsk();
}
