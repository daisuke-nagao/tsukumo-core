/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// A task loads from an address nothing is mapped at. The kernel must stop and
// report the cause (mcause 5), the address of the load and the address it tried
// to read. Before it did, the exception handler returned to the same load for
// ever. The verdict comes from the replaced tkmc_panic() in test/panic_hook.c.

#include <tk/tkernel.h>

#include "../panic_expect.h"
#include "../runner.h"

enum { LOAD_ACCESS_FAULT = 5 };

// Between the boot ROM (0x1000) and the test finisher (0x100000) of the QEMU
// virt machine there is nothing.
#define UNMAPPED_ADDRESS 0x00010000u

static void load_access_fault(void) {
  panic_expect.mcause = LOAD_ACCESS_FAULT;
  panic_expect.mtval = UNMAPPED_ADDRESS;
  // The address of the load is the expected mepc, stored just before.
  __asm__ volatile("la t0, 1f\n"
                   "sw t0, %0\n"
                   "li t1, %1\n"
                   "1: lw t2, 0(t1)\n"
                   : "=m"(panic_expect.mepc)
                   : "i"(UNMAPPED_ADDRESS)
                   : "t0", "t1", "t2");
}

static const test_case_t s_cases[] = {
    {"load_access_fault", load_access_fault},
};

TEST_SUITE_MAIN("panic-load-fault", s_cases, 2000)
