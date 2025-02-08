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

  la sp, _stack_end

  li a0, 0x2000000
  li a1, 1
  sw a1, 0(a0)

  call tkmc_start
1:
  wfi
  j 1b

  .balign 4
mtrap_handler:
  addi sp, sp, -29*4

  sw ra, 0*4(sp)
  sw t0, 1*4(sp)
  sw t1, 2*4(sp)
  sw t2, 3*4(sp)
  sw s0, 4*4(sp)
  sw s1, 5*4(sp)
  sw a0, 6*4(sp)
  sw a1, 7*4(sp)
  sw a2, 8*4(sp)
  sw a3, 9*4(sp)
  sw a4, 10*4(sp)
  sw a5, 11*4(sp)
  sw a6, 12*4(sp)
  sw a7, 13*4(sp)
  sw s2, 14*4(sp)
  sw s3, 15*4(sp)
  sw s4, 16*4(sp)
  sw s5, 17*4(sp)
  sw s6, 18*4(sp)
  sw s7, 19*4(sp)
  sw s8, 20*4(sp)
  sw s9, 21*4(sp)
  sw s10, 22*4(sp)
  sw s11, 23*4(sp)
  sw t3, 24*4(sp)
  sw t4, 25*4(sp)
  sw t5, 26*4(sp)
  sw t6, 27*4(sp)

  csrr a0, mepc
  sw a0, 28*4(sp)

  // Clear MSIP
  li a0, 0x2000000
  li a1, 0
  sw a1, 0(a0)

  lw a0, 28*4(sp)
  csrw mepc, a0

  lw ra, 0*4(sp)
  lw t0, 1*4(sp)
  lw t1, 2*4(sp)
  lw t2, 3*4(sp)
  lw s0, 4*4(sp)
  lw s1, 5*4(sp)
  lw a0, 6*4(sp)
  lw a1, 7*4(sp)
  lw a2, 8*4(sp)
  lw a3, 9*4(sp)
  lw a4, 10*4(sp)
  lw a5, 11*4(sp)
  lw a6, 12*4(sp)
  lw a7, 13*4(sp)
  lw s2, 14*4(sp)
  lw s3, 15*4(sp)
  lw s4, 16*4(sp)
  lw s5, 17*4(sp)
  lw s6, 18*4(sp)
  lw s7, 19*4(sp)
  lw s8, 20*4(sp)
  lw s9, 21*4(sp)
  lw s10, 22*4(sp)
  lw s11, 23*4(sp)
  lw t3, 24*4(sp)
  lw t4, 25*4(sp)
  lw t5, 26*4(sp)
  lw t6, 27*4(sp)




  addi sp, sp, 29*4
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
