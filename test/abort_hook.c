/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// unity.c expands UNITY_TEST_ABORT() and is compiled once, so the hook it calls
// has to exist in every ELF that links Unity. It is a variable and not a
// per-ELF compile definition because a definition would never reach unity.c.
// NULL keeps the old behavior: the failed assert only returns.
void (*test_abort_hook)(void) = 0;
