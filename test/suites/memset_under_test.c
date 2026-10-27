/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// kernel/memset.c, compiled again for the memset suite with the shift check on
// (see CMakeLists.txt). It is a file of its own so that the compile options
// reach this copy only: the options of a source file apply to every target
// that uses the file, and the kernel library must not get them.
#include "../../kernel/memset.c"
