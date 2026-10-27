/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01A17B4C_378C_7E73_9577_C3EE0CF218EE
#define UUID_01A17B4C_378C_7E73_9577_C3EE0CF218EE

#include <tk/tkernel.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// Test finisher of the QEMU virt machine. Writing FINISHER_PASS makes QEMU exit
// with status 0. Writing FINISHER_FAIL with a code in bits 31:16 makes it exit
// with that code, so ctest can judge the run by its exit status. The code must
// not be 0, or a failed run would look like a pass.
static const INT FINISHER_BASE = (INT)0x100000;
static const UW FINISHER_PASS = 0x5555;
static const UW FINISHER_FAIL = 0x3333;
static const UW FINISHER_EXIT_CODE_SHIFT = 16;

/// Ends the QEMU run for the test runner: status 0 if no case failed, 1 if any
/// did. Never returns.
static inline __attribute__((noreturn)) void test_exit(UW failures) {
  // One write only. QEMU may process the shutdown request after the guest has
  // run on, and a second finisher write might replace the exit code; a single
  // write avoids having to find out.
  out_w(FINISHER_BASE, failures
                           ? (FINISHER_FAIL | (1u << FINISHER_EXIT_CODE_SHIFT))
                           : FINISHER_PASS);
  for (;;) {
    __asm__ volatile("wfi");
  }
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A17B4C_378C_7E73_9577_C3EE0CF218EE */
