/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UUID_0194511A_FCFC_7A1D_B75D_151971DE4900
#define UUID_0194511A_FCFC_7A1D_B75D_151971DE4900

#include <tk/tkernel.h>

static const INT UART0_BASE = (INT)0x10000000;

static inline void tkmc_putchar(INT c) { out_w(UART0_BASE, c); }

static void putstring(const char *str) {
  while (*str != '\0') {
    tkmc_putchar(*str);
    ++str;
  }
}

// Prints a word as 8 hex digits, for the tests that report an address or a CSR.
static inline void puthex(UW value) {
  static const char digits[] = "0123456789abcdef";
  for (int shift = 28; shift >= 0; shift -= 4) {
    tkmc_putchar(digits[(value >> shift) & 0xf]);
  }
}

#endif /* UUID_0194511A_FCFC_7A1D_B75D_151971DE4900 */
