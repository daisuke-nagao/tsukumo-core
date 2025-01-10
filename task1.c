/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

volatile unsigned int *const UART0_BASE = (unsigned int *)0x10000000;

static void putstring(const char *str) {
  while (*str != '\0') {
    *UART0_BASE = *str;
    ++str;
  }
}

void task1(void) {
  putstring("Hello, world.\n");
  while (1) {
  }
}
