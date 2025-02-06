/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
  .section .reset, "ax", @progbits
  .global _start, @function
  .balign 4
_start:
  .option norelax
  la gp, __global_pointer$
  .option relax

  la a0, mtrap_handler
  csrw mtvec, a0

  li a0, (1<<3)
  csrs mie, a0
  li a0, (1<<3)
  csrs mstatus, a0

  li a0, 0x2000000
  li a1, 1
  sw a1, 0(a0)

  la sp, _stack_end
  call tkmc_start
1:
  wfi
  j 1b

mtrap_handler:
  // Clear MSIP
  li a0, 0x2000000
  li a1, 0
  sw a1, 0(a0)
  mret
