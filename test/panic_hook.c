/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Replaces the kernel's weak tkmc_panic() in the panic suites. The suites end
// here and not in the runner, so the verdict is a line of output ("PANIC OK")
// that ctest matches: an exit status alone cannot tell a trap from a run that
// finished without trapping.

#include <tk/tkernel.h>

#include "finish.h"
#include "panic.h"
#include "panic_expect.h"
#include "putstring.h"

volatile panic_expect_t panic_expect;

static void put_field(const char *name, UW got, UW want) {
  putstring(" ");
  putstring(name);
  putstring("=");
  puthex(got);
  if (got != want) {
    putstring("(want ");
    puthex(want);
    putstring(")");
  }
}

void tkmc_panic(UW mcause, UW mepc, UW mtval) {
  UW want_cause = panic_expect.mcause;
  UW want_epc = panic_expect.mepc;
  UW want_tval = panic_expect.mtval;
  // The arguments and the record must both match: they are two paths from the
  // CSRs to here.
  int ok = mcause == want_cause && mepc == want_epc && mtval == want_tval &&
           tkmc_last_trap.mcause == want_cause &&
           tkmc_last_trap.mepc == want_epc && tkmc_last_trap.mtval == want_tval;

  putstring(ok ? "PANIC OK" : "PANIC MISMATCH");
  put_field("mcause", mcause, want_cause);
  put_field("mepc", mepc, want_epc);
  put_field("mtval", mtval, want_tval);
  putstring("\n");
  test_exit(ok ? 0 : 1);
}
