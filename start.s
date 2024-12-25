/* SPDX-FileCopyrightText: 2024 Daisuke Nagao */
/* SPDX-License-Identifier: MIT */
  .section .reset, "ax", @progbits
  .global _start, @function
_start:
1:
  wfi
  j 1b
