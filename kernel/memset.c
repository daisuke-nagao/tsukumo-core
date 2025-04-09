/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>

#include "memset.h"

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
    UW *dw = (UW *)d8;
    UW word_value = (value << 24) | (value << 16) | (value << 8) | value;

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
