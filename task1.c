/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

volatile unsigned int *const UART0_BASE = (unsigned int *)0x10000000;

void task1(void) {
  *UART0_BASE = 'H';
  *UART0_BASE = 'e';
  *UART0_BASE = 'l';
  *UART0_BASE = 'l';
  *UART0_BASE = 'o';
  *UART0_BASE = ',';
  *UART0_BASE = ' ';
  *UART0_BASE = 'w';
  *UART0_BASE = 'o';
  *UART0_BASE = 'r';
  *UART0_BASE = 'l';
  *UART0_BASE = 'd';
  *UART0_BASE = '.';
  *UART0_BASE = '\n';
  while (1) {
  }
}
