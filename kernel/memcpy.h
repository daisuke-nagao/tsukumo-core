/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01A18BD5_1E66_7CE0_9C92_1DEF99FB569A
#define UUID_01A18BD5_1E66_7CE0_9C92_1DEF99FB569A

#include <tk/tkernel.h>

typedef typeof(sizeof(void *)) size_t;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// The definition adds restrict; it is left out here so that C++ can read this.
extern void *tkmc_memcpy(void *dest, CONST void *src, size_t n);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A18BD5_1E66_7CE0_9C92_1DEF99FB569A */
