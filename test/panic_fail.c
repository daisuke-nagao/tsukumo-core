/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Replaces the kernel's weak tkmc_panic() in suites where no fault is expected.
// Their cases may turn interrupts off and leave the runner unable to time them
// out, so a fault must end the run with a :FAIL: line, not hang it. The panic
// suites expect a fault and use panic_hook.c instead.

#include <tk/tkernel.h>

#include "finish.h"
#include "putstring.h"

__attribute__((__noreturn__)) void tkmc_panic(UW mcause, UW mepc, UW mtval) {
  putstring(":FAIL: unexpected CPU exception mcause=");
  puthex(mcause);
  putstring(" mepc=");
  puthex(mepc);
  putstring(" mtval=");
  puthex(mtval);
  putstring("\n");
  test_exit(1);
}
