/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
  .section .reset, "ax", @progbits
  .global _start, @function
_start:
  .option norelax
  la gp, __global_pointer$
  .option relax
  la sp, _stack_end
  call tkmc_start
1:
  wfi
  j 1b
