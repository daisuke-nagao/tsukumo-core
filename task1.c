/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "putstring.h"
#include <typedef.h>

extern void tkmc_context_switch(ID tskid);

void task1(void) {
  putstring("Hello, world\n");
  while (1) {
    putstring("Hello, world.\n");
    tkmc_context_switch(1);
  }
}
