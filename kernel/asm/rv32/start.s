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

  la a0, trap_handlers
  addi a0, a0, 1
  csrw mtvec, a0

  csrsi mie, (1<<3)
  csrsi mstatus, (1<<3)

  li a0, 0x2000000
  li a1, 1
  sw a1, 0(a0)

  la sp, _stack_end
  call tkmc_start
1:
  wfi
  j 1b

  .balign 4
mtrap_handler:
  // Clear MSIP
  li a0, 0x2000000
  li a1, 0
  sw a1, 0(a0)
  mret

  .balign 4
default_handler:
  mret

  .balign 16
trap_handlers:
  j default_handler
  .balign 4
  j default_handler
  .balign 4
  j default_handler
  .balign 4
  j mtrap_handler
  .balign 4
  j default_handler
  .balign 4
  j default_handler
  .balign 4
  j default_handler
  .balign 4
  j default_handler
  .balign 4
  j default_handler
  .balign 4
  j default_handler
  .balign 4
  j default_handler
  .balign 4
  j default_handler
  .balign 4
  j default_handler
  .balign 4
  j default_handler
  .balign 4
  j default_handler
  .balign 4
  j default_handler
