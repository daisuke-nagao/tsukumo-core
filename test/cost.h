/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// What the cost suites (mailbox-cost, timer-cost, timer-cost-max) measure
// against.

#ifndef UUID_01A1EDF0_394E_7DB0_A8CB_8DA069DDF268
#define UUID_01A1EDF0_394E_7DB0_A8CB_8DA069DDF268

#include <tk/tkernel.h>

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// The most instructions a measured kernel path may take: a tenth of a kernel
// tick (10 ms) at -icount shift 5, which holds 312500 instructions. It is a
// count, not a share of a tick, so it does not follow the shift.
enum { BUDGET_INSTRUCTIONS = 31250 };

extern tkmc_list_head tkmc_timer_queue;

// The number of tasks on the kernel's timer queue.
static inline UINT test_timer_queue_length(void) {
  UINT length = 0;
  for (tkmc_list_head *link = tkmc_timer_queue.next; link != &tkmc_timer_queue;
       link = link->next) {
    ++length;
  }
  return length;
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A1EDF0_394E_7DB0_A8CB_8DA069DDF268 */
