/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// kernel/memset.c, compiled unchanged into the PC tests. Its weak alias
// `memset` is not wanted here: the C library has one, and task.c uses it. The
// headers come first, so that only the source file sees the two macros: the
// first empties the attribute that makes the alias, the second renames the
// declaration that is left, so it cannot clash with the library's.
#include <tk/tkernel.h>

#include "memset.h"

#define alias(name)
#define memset unused_memset_alias

#include "../../kernel/memset.c"
