/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01953352_90E9_7F9F_ABE3_31D32A8E0817
#define UUID_01953352_90E9_7F9F_ABE3_31D32A8E0817

#include <tk/typedef.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

static inline void out_b(INT port, UB data) { *(_UB *)port = data; }
static inline void out_h(INT port, UH data) { *(_UH *)port = data; }
static inline void out_w(INT port, UW data) { *(_UW *)port = data; }

static inline UB in_b(INT port) { return *(_UB *)port; }
static inline UH in_h(INT port) { return *(_UH *)port; }
static inline UW in_w(INT port) { return *(_UW *)port; }

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01953352_90E9_7F9F_ABE3_31D32A8E0817 */
