/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "asm/rv32/address.h"
#include "spin_with_registers.h"
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
 * UW depth_spin_arm_word(volatile UW *flag, UW deadline, volatile UW *reg,
 *                        UW value)
 *
 * The spin of spin_with_registers.h armed by a word store, for the RTC's
 * IRQ-enable register. The UART's interrupt enable register takes a byte, which
 * test_spin_with_registers (test/spin_with_registers.S) stores. Each level of a
 * nested chain calls one of the two to start its child, so the registers of
 * every interrupted level cross a trap.
 */
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
