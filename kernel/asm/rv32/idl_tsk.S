/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

  .text
  .global tkmc_idl_tsk, @function
  .balign 4
tkmc_idl_tsk:
1:
  /* Keep a wake-up pending between the ready check and wfi. A locally enabled
     interrupt ends wfi even with mstatus.MIE clear. s0 survives the C call. */
  csrrci s0, mstatus, 8
  call tkmc_idle_can_halt
  beqz a0, 2f
  wfi
2:
  andi s0, s0, 8
  csrs mstatus, s0
  call tkmc_yield
  j 1b
