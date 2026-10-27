/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Tests of tkmc_memset(). The copy of kernel/memset.c in this ELF is built with
// the shift check in trap mode, because the undefined shift in it leaves the
// bytes right, and only the check can see it: the first bad shift executes
// unimp (the trap instruction of RISC-V clang), the kernel stops at the trap,
// and test/panic_hook.c prints the cause (mcause 2, illegal instruction) and
// ends the run with a failure status. The same matrix runs on
// the PC with UBSan, see test/host/test_memset.c.

#include <tk/tkernel.h>

#include "../memset_matrix.h"
#include "../runner.h"

static void fills_exactly_the_requested_range(void) { memset_matrix_check(); }

static const test_case_t s_cases[] = {
    {"fills_exactly_the_requested_range", fills_exactly_the_requested_range},
};

// The matrix runs about 250 million instructions. With -icount guest time is 32
// ns per instruction, so it takes 8 seconds of guest time, while the host needs
// under a second; the limit is guest time, so it is set well above.
TEST_SUITE_MAIN("memset", s_cases, 30000)
