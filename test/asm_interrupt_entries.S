/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/asm/rv32/interrupt.h>

  .text

  .macro RECORD_IRQ_ENTRY
  la t0, asm_irq_seen_intno
  sw a0, 0(t0)
  la t0, asm_irq_seen_frame
  sw a1, 0(t0)
  la t0, asm_irq_seen_sp
  sw sp, 0(t0)
  csrr t1, mstatus
  la t0, asm_irq_seen_mstatus
  sw t1, 0(t0)
  csrr t1, mie
  la t0, asm_irq_seen_mie
  sw t1, 0(t0)
  .endm

  .global asm_irq_ret_entry, @function
  .balign 4
asm_irq_ret_entry:
  RECORD_IRQ_ENTRY
  mv s0, sp
  call asm_irq_callback
  mv sp, s0
  tail tk_ret_int

  .global asm_irq_direct_entry, @function
  .balign 4
asm_irq_direct_entry:
  RECORD_IRQ_ENTRY
  mv s0, sp
  call asm_irq_callback
  mv sp, s0
  TKMC_RET_INT_ASM
