/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
#include "address.h"
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

  /* Enable software and timer interrupts */
  li t0, (1<<3) | (1<<7)
  csrs mie, t0

  la sp, _stack_end

  call tkmc_start
1:
  wfi
  j 1b

  .balign 4
mtrap_handler:
  addi sp, sp, -32*4

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

  /* Clear software interrupt */
  li a0, CLINT_BASE_ADDRESS+CLINT_MSIP_OFFSET
  sw zero, 0(a0)

  mv a0, sp
  call schedule
  mv sp, a0

  lw a0, 28*4(sp)
  csrw mepc, a0

  lw t6, 27*4(sp)
  lw t5, 26*4(sp)
  lw t4, 25*4(sp)
  lw t3, 24*4(sp)
  lw s11, 23*4(sp)
  lw s10, 22*4(sp)
  lw s9, 21*4(sp)
  lw s8, 20*4(sp)
  lw s7, 19*4(sp)
  lw s6, 18*4(sp)
  lw s5, 17*4(sp)
  lw s4, 16*4(sp)
  lw s3, 15*4(sp)
  lw s2, 14*4(sp)
  lw a7, 13*4(sp)
  lw a6, 12*4(sp)
  lw a5, 11*4(sp)
  lw a4, 10*4(sp)
  lw a3, 9*4(sp)
  lw a2, 8*4(sp)
  lw a1, 7*4(sp)
  lw a0, 6*4(sp)
  lw s1, 5*4(sp)
  lw s0, 4*4(sp)
  lw t2, 3*4(sp)
  lw t1, 2*4(sp)
  lw t0, 1*4(sp)
  lw ra, 0*4(sp)

  addi sp, sp, 32*4
  mret

  .balign 4
timer_interrupt:
  addi sp, sp, -32*4

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

  call tkmc_timer_handler

  /* Return from interrupt */
  lw t6, 27*4(sp)
  lw t5, 26*4(sp)
  lw t4, 25*4(sp)
  lw t3, 24*4(sp)
  lw s11, 23*4(sp)
  lw s10, 22*4(sp)
  lw s9, 21*4(sp)
  lw s8, 20*4(sp)
  lw s7, 19*4(sp)
  lw s6, 18*4(sp)
  lw s5, 17*4(sp)
  lw s4, 16*4(sp)
  lw s3, 15*4(sp)
  lw s2, 14*4(sp)
  lw a7, 13*4(sp)
  lw a6, 12*4(sp)
  lw a5, 11*4(sp)
  lw a4, 10*4(sp)
  lw a3, 9*4(sp)
  lw a2, 8*4(sp)
  lw a1, 7*4(sp)
  lw a0, 6*4(sp)
  lw s1, 5*4(sp)
  lw s0, 4*4(sp)
  lw t2, 3*4(sp)
  lw t1, 2*4(sp)
  lw t0, 1*4(sp)
  lw ra, 0*4(sp)

  addi sp, sp, 32*4
  mret

  .balign 4
default_handler:
  mret

  .balign 16
trap_handlers:
  j default_handler    /* 0: Reserved */
  .balign 4
  j default_handler    /* 1: Supervisor software interrupt */
  .balign 4
  j default_handler    /* 2: Reserved */
  .balign 4
  j mtrap_handler      /* 3: Machine software interrupt (MSIP) */
  .balign 4
  j default_handler    /* 4: Reserved */
  .balign 4
  j default_handler    /* 5: Reserved */
  .balign 4
  j default_handler    /* 6: Reserved */
  .balign 4
  j timer_interrupt    /* 7: Machine timer interrupt (MTIP) */
  .balign 4
  j default_handler    /* 8: Reserved */
  .balign 4
  j default_handler    /* 9: Supervisor external interrupt */
  .balign 4
  j default_handler    /* 10: Reserved */
  .balign 4
  j default_handler    /* 11: Machine external interrupt (MEIP) */
  .balign 4
  j default_handler    /* 12: Reserved */
  .balign 4
  j default_handler    /* 13: Reserved */
  .balign 4
  j default_handler    /* 14: Reserved */
  .balign 4
  j default_handler    /* 15: Reserved */

