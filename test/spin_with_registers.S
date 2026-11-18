/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "spin_with_registers.h"

/*
 * UW test_spin_with_registers(volatile UW *flag, UW deadline,
 *                              volatile UB *reg, UB value)
 *
 * The spin of spin_with_registers.h, armed by a byte store.
 */
  SPIN test_spin_with_registers, sb
