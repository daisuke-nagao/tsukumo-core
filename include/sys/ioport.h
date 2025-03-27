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
static inline void out_d(INT port, UD data) {
  // Write lower 32 bits first
  out_w(port, (UW)(data & 0xFFFFFFFFUL));
  // Then write upper 32 bits to next 4 bytes (offset +4)
  out_w(port + 4, (UW)(data >> 32));
}

static inline UB in_b(INT port) { return *(_UB *)port; }
static inline UH in_h(INT port) { return *(_UH *)port; }
static inline UW in_w(INT port) { return *(_UW *)port; }
static inline UD in_d(INT port) {
  UD low = (UD)in_w(port);
  UD high = (UD)in_w(port + 4);
  return low | (high << 32);
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01953352_90E9_7F9F_ABE3_31D32A8E0817 */
