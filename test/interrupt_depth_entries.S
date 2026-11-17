/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "asm/rv32/address.h"
#include <tk/asm/rv32/interrupt.h>

/*
 * UW depth_paint_task_stack(volatile UW *bottom, UW canary, UW paint,
 *                           UW *sp_out, UW guard)
 * A leaf uses the caller's SP as the paint ceiling, leaving its live frame and
 * a 256-byte guard untouched while painting the unused prefix below it.
 */
  .text
  .global depth_paint_task_stack, @function
  .balign 4
depth_paint_task_stack:
  sw sp, 0(a3)
  sw a1, 0(a0)
  addi a0, a0, 4
  sub t0, sp, a4
.Ldepth_paint_loop:
  bgeu a0, t0, .Ldepth_paint_done
  sw a2, 0(a0)
  addi a0, a0, 4
  j .Ldepth_paint_loop
.Ldepth_paint_done:
  mv a0, t0
  ret

/*
 * UW depth_spin_arm_byte(volatile UW *flag, UW deadline, volatile UB *reg,
 *                        UB value)
 * UW depth_spin_arm_word(volatile UW *flag, UW deadline, volatile UW *reg,
 *                        UW value)
 *
 * test_spin_with_registers (test/spin_with_registers.S) with a choice of the
 * arming store: the UART's interrupt enable register takes a byte, the RTC's
 * IRQ-enable register a word. Each level of a nested chain calls one to start
 * its child, so the registers of every interrupted level cross a trap.
 *
 * The return value is the same: bit n set means the n-th patterned register
 * (t2-t6, a2-a7, s0-s11) changed, bit 31 that the deadline passed before
 * *flag became nonzero, 0 that the flag was set and every register survived.
 *
 * Not done: one function with a width argument. The width would need a
 * register of its own through the wait, and every register is patterned.
 */
  .macro PATTERN reg, n
  li \reg, 0x7e570000 + \n
  .endm

  .macro CHECK reg, n, bit
  li t0, 0x7e570000 + \n
  beq \reg, t0, 1f
  li t1, 1 << \bit
  or a0, a0, t1
1:
  .endm

  .macro SPIN name, store
  .text
  .global \name, @function
  .balign 4
\name:
  addi sp, sp, -16*4
  sw ra, 0*4(sp)
  sw s0, 1*4(sp)
  sw s1, 2*4(sp)
  sw s2, 3*4(sp)
  sw s3, 4*4(sp)
  sw s4, 5*4(sp)
  sw s5, 6*4(sp)
  sw s6, 7*4(sp)
  sw s7, 8*4(sp)
  sw s8, 9*4(sp)
  sw s9, 10*4(sp)
  sw s10, 11*4(sp)
  sw s11, 12*4(sp)

  mv t0, a2
  mv t1, a3

  PATTERN t2, 0
  PATTERN t3, 1
  PATTERN t4, 2
  PATTERN t5, 3
  PATTERN t6, 4
  PATTERN a2, 5
  PATTERN a3, 6
  PATTERN a4, 7
  PATTERN a5, 8
  PATTERN a6, 9
  PATTERN a7, 10
  PATTERN s0, 11
  PATTERN s1, 12
  PATTERN s2, 13
  PATTERN s3, 14
  PATTERN s4, 15
  PATTERN s5, 16
  PATTERN s6, 17
  PATTERN s7, 18
  PATTERN s8, 19
  PATTERN s9, 20
  PATTERN s10, 21
  PATTERN s11, 22

  \store t1, 0(t0)
  li t0, CLINT_BASE_ADDRESS+CLINT_MTIME_OFFSET
2:
  lw t1, 0(a0)
  bnez t1, 3f
  lw t1, 0(t0)
  sub t1, t1, a1
  bltz t1, 2b
  li a0, 0x80000000
  j 4f
3:
  li a0, 0
4:
  CHECK t2, 0, 0
  CHECK t3, 1, 1
  CHECK t4, 2, 2
  CHECK t5, 3, 3
  CHECK t6, 4, 4
  CHECK a2, 5, 5
  CHECK a3, 6, 6
  CHECK a4, 7, 7
  CHECK a5, 8, 8
  CHECK a6, 9, 9
  CHECK a7, 10, 10
  CHECK s0, 11, 11
  CHECK s1, 12, 12
  CHECK s2, 13, 13
  CHECK s3, 14, 14
  CHECK s4, 15, 15
  CHECK s5, 16, 16
  CHECK s6, 17, 17
  CHECK s7, 18, 18
  CHECK s8, 19, 19
  CHECK s9, 20, 20
  CHECK s10, 21, 21
  CHECK s11, 22, 22

  lw ra, 0*4(sp)
  lw s0, 1*4(sp)
  lw s1, 2*4(sp)
  lw s2, 3*4(sp)
  lw s3, 4*4(sp)
  lw s4, 5*4(sp)
  lw s5, 6*4(sp)
  lw s6, 7*4(sp)
  lw s7, 8*4(sp)
  lw s8, 9*4(sp)
  lw s9, 10*4(sp)
  lw s10, 11*4(sp)
  lw s11, 12*4(sp)
  addi sp, sp, 16*4
  ret
  .endm

  SPIN depth_spin_arm_byte, sb
  SPIN depth_spin_arm_word, sw

/*
 * TA_ASM entry of a chain level: depth_asm_callback runs the level and returns
 * nonzero to leave by the direct return, 0 to leave through tk_ret_int.
 */
  .text
  .global depth_asm_entry, @function
  .balign 4
depth_asm_entry:
  mv s0, sp
  call depth_asm_callback
  mv sp, s0
  beqz a0, 1f
  TKMC_RET_INT_ASM
1:
  tail tk_ret_int
