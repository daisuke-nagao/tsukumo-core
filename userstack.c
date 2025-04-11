/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>

#include "userstack.h"

UW g_stack1_1024B[256] __attribute__((aligned(16)));
UW g_stack2_1024B[256] __attribute__((aligned(16)));

