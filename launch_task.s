/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

.section .text
  .global tkmc_launch_task, @function
tkmc_launch_task:
  mv sp, a0
  mv ra, a1
  ret
