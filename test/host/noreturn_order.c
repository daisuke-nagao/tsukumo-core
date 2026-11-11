/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// panic.h keeps its never-return contract when <stdnoreturn.h> comes first.
// That header defines the macro noreturn, and Unity includes it on Windows
// clang (MSVC target) before the tests include panic.h. A plain
// __attribute__((noreturn)) then expands to an unknown attribute and is
// dropped. This file is built with -Werror=unknown-attributes and
// -Werror=return-type (see CMakeLists.txt): each function below ends in a
// call to a panic API and has no return, so a dropped attribute fails the
// build.
//
// The functions are static and unused, so no code is emitted for them and
// nothing has to define the RV32-only panic functions on the host. Both
// diagnostics come before code generation, so they still run.

#include <stdnoreturn.h>

#include "panic.h"

static __attribute__((unused)) int ends_in_fatal_trap(void) {
  tkmc_fatal_trap(0, 0, 0);
}

static __attribute__((unused)) int ends_in_panic(void) { tkmc_panic(0, 0, 0); }

static __attribute__((unused)) int ends_in_fatal_startup(void) {
  tkmc_fatal_startup(TKMC_STARTUP_NONE, E_OK);
}

static __attribute__((unused)) int ends_in_startup_panic(void) {
  tkmc_startup_panic(TKMC_STARTUP_NONE, E_OK);
}

static __attribute__((unused)) int ends_in_fatal_context(void) {
  tkmc_fatal_context(TKMC_CONTEXT_NONE);
}

static __attribute__((unused)) int ends_in_context_panic(void) {
  tkmc_context_panic(TKMC_CONTEXT_NONE);
}
