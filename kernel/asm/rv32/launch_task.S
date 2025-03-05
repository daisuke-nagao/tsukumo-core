/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
  .text
  .global __launch_task, @function
  .balign 4
__launch_task:
    lw    sp, (a0)
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
    lw a3, 28*4(sp)
    addi  sp, sp, 4*32
    jr a3
