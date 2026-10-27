/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Stands in for include/sys/ioport.h in the PC unit tests. The real header
// turns an address into a pointer and reads or writes it, which on a PC would
// be a wild pointer. These go to a small fake device model in
// test/host/fakes.c instead, which keeps a log of every access and refuses
// addresses the kernel has no reason to touch.

#ifndef UUID_01A17C3E_90B6_7913_8466_4F7028FB79E3
#define UUID_01A17C3E_90B6_7913_8466_4F7028FB79E3

#include <tk/typedef.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern UD host_in(INT port, UINT width);
extern void host_out(INT port, UINT width, UD data);

static inline void out_b(INT port, UB data) { host_out(port, 1, data); }
static inline void out_h(INT port, UH data) { host_out(port, 2, data); }
static inline void out_w(INT port, UW data) { host_out(port, 4, data); }
static inline void out_d(INT port, UD data) {
  // Same order as the real header: lower 32 bits first.
  out_w(port, (UW)(data & 0xFFFFFFFFUL));
  out_w(port + 4, (UW)(data >> 32));
}

static inline UB in_b(INT port) { return (UB)host_in(port, 1); }
static inline UH in_h(INT port) { return (UH)host_in(port, 2); }
static inline UW in_w(INT port) { return (UW)host_in(port, 4); }
static inline UD in_d(INT port) {
  UD low = (UD)in_w(port);
  UD high = (UD)in_w(port + 4);
  return low | (high << 32);
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A17C3E_90B6_7913_8466_4F7028FB79E3 */
