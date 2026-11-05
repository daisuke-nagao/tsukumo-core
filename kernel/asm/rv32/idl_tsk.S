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
  /* Stop until an interrupt is pending and enabled in mie. wfi does not depend
     on mstatus.MIE, so such an interrupt that arrives before it only makes it
     return at once. */
  wfi
  call tkmc_yield
  j 1b
