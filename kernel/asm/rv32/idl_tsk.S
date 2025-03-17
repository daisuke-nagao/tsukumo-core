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
  call tkmc_yield
  j 1b
