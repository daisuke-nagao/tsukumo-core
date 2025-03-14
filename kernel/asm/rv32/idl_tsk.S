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
  li a0, 0
  call tk_dly_tsk
  j 1b
