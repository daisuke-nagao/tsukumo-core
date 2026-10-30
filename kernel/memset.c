/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>

#include "memset.h"

// The word loops below read and write objects of any type through a word
// pointer. C lets only character types access any object, so a plain UW access
// to, say, a float is undefined, and an optimizer that inlines this function
// may move it past the caller's own float accesses. may_alias (a clang and GCC
// extension) gives a type the aliasing of a character type. The kernel is not
// built with -fno-strict-aliasing instead, which would weaken it everywhere.
// Nested: #if does not skip the right side of ||, and a compiler without
// __has_attribute could not parse it.
#if defined(__has_attribute)
#if !__has_attribute(may_alias)
#error "the word loops need __attribute__((may_alias))"
#endif
#else
#error "the word loops need __attribute__((may_alias))"
#endif
typedef UW __attribute__((may_alias)) UW_ANY;

/*
 * tkmc_memset - Fills a block of memory with a specified value.
 *
 * Parameters:
 * - dest: Pointer to the memory block to fill.
 * - c: Value to set (converted to unsigned char).
 * - n: Number of bytes to set.
 *
 * Returns:
 * - Pointer to the memory block (dest).
 */
void *tkmc_memset(void *dest, int c, size_t n) {
  UB *d8 = (UB *)dest; // Pointer for byte-wise access
  UB value = (UB)c;    // Convert the value to unsigned char

  /* For small sizes, perform a simple byte-by-byte fill */
  if (n < 32) {
    while (n--) {
      *d8++ = value;
    }
    return dest;
  }

  /*
   * For larger sizes, optimize by filling in word-aligned chunks
   * when the destination is aligned to a 4-byte boundary.
   */
  while (((UINT)d8 & 0x3) && n) {
    *d8++ = value;
    n--;
  }

  /* Fill memory in 4-byte chunks if aligned */
  if (n >= 4) {
    UW_ANY *dw = (UW_ANY *)d8;
    // Unsigned arithmetic: value is promoted to int, and 0x80 << 24 does not
    // fit in an int, which is undefined behaviour.
    UW word_value = (UW)value * 0x01010101u;

    while (n >= 32) {
      dw[0] = word_value;
      dw[1] = word_value;
      dw[2] = word_value;
      dw[3] = word_value;
      dw[4] = word_value;
      dw[5] = word_value;
      dw[6] = word_value;
      dw[7] = word_value;
      dw += 8;
      n -= 32;
    }

    while (n >= 4) {
      *dw++ = word_value;
      n -= 4;
    }

    d8 = (UB *)dw; // Revert to byte pointer for remaining bytes
  }

  /* Fill any remaining bytes */
  while (n--) {
    *d8++ = value;
  }

  return dest;
}

/*
 * Alias memset to tkmc_memset using a weak symbol.
 * This allows the system to use tkmc_memset as the default implementation of
 * memset.
 */
__attribute__((weak, alias("tkmc_memset"))) void *memset(void *dest, int c,
                                                         size_t n);
