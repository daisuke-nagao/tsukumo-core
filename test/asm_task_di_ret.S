/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

  .text
  .global asm_task_di_ret_entry, @function
  .balign 4
asm_task_di_ret_entry:
  addi sp, sp, -16
  sw ra, 12(sp)
  call asm_task_di_ret_capture
  lw ra, 12(sp)
  addi sp, sp, 16
  csrci mstatus, 8
  ret
