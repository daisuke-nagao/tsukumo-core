/* SPDX-FileCopyrightText: 2024 Daisuke Nagao */
/* SPDX-License-Identifier: MIT */

static void clear_bss(void);

void tkmc_start(int a0, int a1) {
  clear_bss();
  return;
}

static void clear_bss(void) {}
