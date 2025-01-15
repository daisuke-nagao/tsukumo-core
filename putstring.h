/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UUID_0194511A_FCFC_7A1D_B75D_151971DE4900
#define UUID_0194511A_FCFC_7A1D_B75D_151971DE4900

volatile static unsigned int *const UART0_BASE = (unsigned int *)0x10000000;

static void putstring(const char *str) {
  while (*str != '\0') {
    *UART0_BASE = *str;
    ++str;
  }
}

#endif /* UUID_0194511A_FCFC_7A1D_B75D_151971DE4900 */