/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01A1D5FE_C1A4_7E72_B4E2_B3322A1E0993
#define UUID_01A1D5FE_C1A4_7E72_B4E2_B3322A1E0993

/* RV32 context-record layout shared with kernel/intctl.h. */
#define TKMC_IRQCTX_PREVIOUS 0
#define TKMC_IRQCTX_FRAME 4
#define TKMC_IRQCTX_ENTRY_SP 8
#define TKMC_IRQCTX_HANDLER 12
#define TKMC_IRQCTX_DEPTH 16
#define TKMC_IRQCTX_INTNO 20
#define TKMC_IRQCTX_SAVED_THRESHOLD 24
#define TKMC_IRQCTX_KIND 28
#define TKMC_IRQCTX_SIZE 32

#define TKMC_INTERRUPT_KIND_NONE 0
#define TKMC_INTERRUPT_KIND_HLNG 1
#define TKMC_INTERRUPT_KIND_ASM 2
#define TKMC_INTERRUPT_KIND_MASK 3
#define TKMC_INTERRUPT_COMPLETED 4

/* The RV32 PLIC; C checks these literals against address.h. */
#define TKMC_PLIC_BASE_ADDRESS 0x0c000000
#define TKMC_PLIC_ENABLE_OFFSET 0x2000
#define TKMC_PLIC_THRESHOLD_OFFSET 0x200000
#define TKMC_PLIC_CLAIM_OFFSET 0x200004

#ifdef __ASSEMBLER__

/* The valid path returns inline without a C exit call or dispatch. */
// clang-format off
.macro TKMC_RET_INT_ASM
  la t0, tkmc_active_interrupt_context
  lw t0, 0(t0)
  beqz t0, 9f

  lw t1, TKMC_IRQCTX_KIND(t0)
  andi t1, t1, TKMC_INTERRUPT_KIND_MASK
  li t2, TKMC_INTERRUPT_KIND_ASM
  bne t1, t2, 9f
  lw t1, TKMC_IRQCTX_DEPTH(t0)
  la t2, tkmc_isr_depth
  lw t2, 0(t2)
  bne t1, t2, 9f
  lw t1, TKMC_IRQCTX_ENTRY_SP(t0)
  bne t1, sp, 9f
  lw t1, TKMC_IRQCTX_INTNO(t0)
  beqz t1, 9f
  lw t1, TKMC_IRQCTX_FRAME(t0)
  beqz t1, 9f
  la t1, current
  lw t1, 0(t1)
  beqz t1, 9f
  csrr t1, mstatus
  andi t1, t1, 8
  bnez t1, 9f

  /* The saved threshold must be restored before PLIC completion. */
  lw t1, TKMC_IRQCTX_SAVED_THRESHOLD(t0)
  li t2, TKMC_PLIC_BASE_ADDRESS + TKMC_PLIC_THRESHOLD_OFFSET
  sw t1, 0(t2)

  /* Complete once while enabled, then restore the handler's source state. */
  lw t1, TKMC_IRQCTX_KIND(t0)
  andi t1, t1, TKMC_INTERRUPT_COMPLETED
  bnez t1, 8f
  lw t2, TKMC_IRQCTX_INTNO(t0)
  srli t3, t2, 5
  slli t3, t3, 2
  li t6, TKMC_PLIC_BASE_ADDRESS + TKMC_PLIC_ENABLE_OFFSET
  add t3, t3, t6
  lw t4, 0(t3)
  andi t5, t2, 31
  li t6, 1
  sll t5, t6, t5
  or t6, t4, t5
  sw t6, 0(t3)
  li t6, TKMC_PLIC_BASE_ADDRESS + TKMC_PLIC_CLAIM_OFFSET
  sw t2, 0(t6)
  lw t1, TKMC_IRQCTX_KIND(t0)
  ori t1, t1, TKMC_INTERRUPT_COMPLETED
  sw t1, TKMC_IRQCTX_KIND(t0)
  and t6, t4, t5
  bnez t6, 8f
  not t5, t5
  and t4, t4, t5
  sw t4, 0(t3)
8:
  lw t1, TKMC_IRQCTX_PREVIOUS(t0)
  la t2, tkmc_active_interrupt_context
  sw t1, 0(t2)
  la t1, tkmc_isr_depth
  lw t2, 0(t1)
  addi t2, t2, -1
  sw t2, 0(t1)

  /* Only the outer trap masked MSIE/MTIE, recorded in frame word 29. */
  lw t1, TKMC_IRQCTX_DEPTH(t0)
  li t2, 1
  bne t1, t2, 7f
  lw t1, TKMC_IRQCTX_FRAME(t0)
  lw t2, 29*4(t1)
  andi t2, t2, 0x88
  csrs mie, t2
7:
  lw sp, TKMC_IRQCTX_FRAME(t0)
  li t1, 3<<11
  csrs mstatus, t1

  lw t0, 28*4(sp)
  csrw mepc, t0
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

9:
  j tkmc_invalid_ret_int
.endm
// clang-format on

#endif /* __ASSEMBLER__ */

#endif /* UUID_01A1D5FE_C1A4_7E72_B4E2_B3322A1E0993 */
