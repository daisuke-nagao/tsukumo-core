/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
  .section .reset, "ax", @progbits
  .global __context_switch, @function
  .global __launch_task, @function
  .balign 4
__context_switch:
    /* a0 = next_sp: void** */
    /* a1 = current_sp: void** */
    addi  sp, sp, -4*13
    sw    s0, 0*4(sp)
    sw    s1, 1*4(sp)
    sw    s2, 2*4(sp)
    sw    s3, 3*4(sp)
    sw    s4, 4*4(sp)
    sw    s5, 5*4(sp)
    sw    s6, 6*4(sp)
    sw    s7, 7*4(sp)
    sw    s8, 8*4(sp)
    sw    s9, 9*4(sp)
    sw    s10, 10*4(sp)
    sw    s11, 11*4(sp)
    sw    ra, 12*4(sp)
    sw    sp, (a1)
__launch_task:
    lw    sp, (a0)
    lw    s0, 0*4(sp)
    lw    s1, 1*4(sp)
    lw    s2, 2*4(sp)
    lw    s3, 3*4(sp)
    lw    s4, 4*4(sp)
    lw    s5, 5*4(sp)
    lw    s6, 6*4(sp)
    lw    s7, 7*4(sp)
    lw    s8, 8*4(sp)
    lw    s9, 9*4(sp)
    lw    s10, 10*4(sp)
    lw    s11, 11*4(sp)
    lw    ra, 12*4(sp)
    addi  sp, sp, 4*13
    ret