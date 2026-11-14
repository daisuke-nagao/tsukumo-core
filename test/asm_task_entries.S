/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

  .text

  .macro RECORD_ASM_TASK_ENTRY
  la t0, asm_task_seen_stacd
  sw a0, 0(t0)
  la t0, asm_task_seen_exinf
  sw a1, 0(t0)
  la t0, asm_task_seen_sp
  sw sp, 0(t0)
  la t0, asm_task_entry_count
  lw t1, 0(t0)
  addi t1, t1, 1
  sw t1, 0(t0)
  .endm

  .global asm_task_return_entry, @function
  .balign 4
asm_task_return_entry:
  RECORD_ASM_TASK_ENTRY
  li t0, 0x41534d
  beq a0, t0, 1f
  ret
1:
  tail tk_exd_tsk

  .global asm_task_ext_entry, @function
  .balign 4
asm_task_ext_entry:
  RECORD_ASM_TASK_ENTRY
  li t0, 0x41534d
  beq a0, t0, 1f
  tail tk_ext_tsk
1:
  tail tk_exd_tsk

  .global asm_task_exd_entry, @function
  .balign 4
asm_task_exd_entry:
  RECORD_ASM_TASK_ENTRY
  tail tk_exd_tsk
