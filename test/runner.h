/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01A17BD3_A8B7_73E0_8A61_5AF2739D1329
#define UUID_01A17BD3_A8B7_73E0_8A61_5AF2739D1329

#include <tk/tkernel.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/// One test case. fn runs in its own task, so a failed assert ends only that
/// task and the runner moves on to the next case.
typedef struct {
  const char *name;
  void (*fn)(void);
} test_case_t;

/// Starts the runner task. Called from usermain, which runs on a 512 byte
/// stack and must not assert. limit_ms is the time one case may take before it
/// is reported as a timeout and the suite is aborted.
extern void test_start_runner(const char *suite, const test_case_t *cases,
                              UINT count, UINT limit_ms);

/// Defines usermain for a suite. A suite without cases fails to compile, so it
/// can never report "0 Tests 0 Failures" as a success.
#define TEST_SUITE_MAIN(suite, cases, limit_ms)                                \
  _Static_assert(sizeof(cases) / sizeof((cases)[0]) > 0,                       \
                 "a test suite needs at least one case");                      \
  void usermain(int _a0) {                                                     \
    (void)_a0;                                                                 \
    test_start_runner((suite), (cases), sizeof(cases) / sizeof((cases)[0]),    \
                      (limit_ms));                                             \
  }

/// Starts a child task of the running case and returns its ID. Registered
/// children are waited for before the case is concluded, even if the case never
/// calls test_join(). prio is the kernel priority (lower is higher); a child
/// that has a higher priority than the case runs as soon as it starts. A failed
/// assert in fn ends only the child.
extern ID test_spawn(void (*fn)(void *), PRI prio, void *arg);

/// Waits until every child spawned by the running case has exited.
extern void test_join(void);

/// Called by a child after the asserts that matter. A child that never ran, or
/// ran to the wrong place, would otherwise let the case pass without checking
/// anything.
extern void test_checkpoint(void);

/// test_join(), then asserts that exactly n checkpoints were reached.
extern void test_join_expect(UINT n);

/// Kernel priorities of the runner and of a running case (lower is higher). A
/// child that must start running at once, before test_spawn() returns, takes a
/// priority below TEST_CASE_PRIORITY in value; a child that must wait until the
/// case blocks takes one above it. Suites check their choices against this with
/// _Static_assert.
enum { TEST_RUNNER_PRIORITY = 1, TEST_CASE_PRIORITY = 4 };

/// The machine timer. It counts TEST_MTIME_PER_MS per millisecond and wraps
/// after about 429 seconds, so compare differences only.
enum { TEST_MTIME_PER_MS = 10000 };
extern UW test_mtime(void);

/// Returns test_mtime() taken in the middle of a kernel tick (10 ms). A timed
/// wait of n ticks lasts between n-1 and n ticks from an arbitrary start, so a
/// lower-bound check can only tell "n ticks" from "n-1 ticks" when the start is
/// not right after a tick edge, where a wait that is one tick short still lasts
/// as long as the bound. From mid-tick the two differ by half a tick.
extern UW test_mtime_mid_tick(void);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A17BD3_A8B7_73E0_8A61_5AF2739D1329 */
