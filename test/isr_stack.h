/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// The interrupt stack that the RV32 trap entries switch to (linker.ld), for
// the QEMU suites that measure how much of it a trap uses: paint it, let the
// trap run, and count the words that are no longer painted.

#ifndef UUID_01A21B73_B426_7113_98A3_C3F326350E99
#define UUID_01A21B73_B426_7113_98A3_C3F326350E99

#include <tk/tkernel.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern unsigned char _isr_stack_start[];
extern unsigned char _isr_stack_end[];

/// The word a painted stack holds where nothing has written.
enum { STACK_PAINT = 0x5a5a5a5a };

/// Whether address is on the interrupt stack. The stack grows down from
/// _isr_stack_end, so the end itself counts and the start does not.
static inline BOOL on_the_interrupt_stack(UW address) {
  return address > (UW)_isr_stack_start && address <= (UW)_isr_stack_end;
}

/// Writes STACK_PAINT to every word from start up to end.
static inline void paint_stack(unsigned char *start, unsigned char *end) {
  for (volatile UW *word = (volatile UW *)start; word < (volatile UW *)end;
       ++word) {
    *word = STACK_PAINT;
  }
}

/// How many bytes of the painted interrupt stack a trap has used: the bytes
/// from the lowest word that is no longer painted up to the end.
static inline UINT interrupt_stack_high_water(void) {
  const volatile UW *word = (const volatile UW *)_isr_stack_start;
  while (word < (const volatile UW *)_isr_stack_end && *word == STACK_PAINT) {
    ++word;
  }
  return (UINT)((UW)_isr_stack_end - (UW)word);
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A21B73_B426_7113_98A3_C3F326350E99 */
