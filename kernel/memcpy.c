/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>

typedef typeof(sizeof(void *)) size_t;

void *tkmc_memcpy(void *restrict dest, CONST void *restrict src, size_t n) {
  /* Pointers for accessing data in 8-bit increments */
  UB *d8 = (UB *)dest;
  CONST UB *s8 = (CONST UB *)src;

  /* For small sizes, perform a simple byte-by-byte copy */
  if (n < 32) {
    while (n--) {
      *d8++ = *s8++;
    }
    return dest;
  }

  /*
   * In RISC-V 32-bit implementations, misaligned accesses with lw/sw
   * instructions can trigger alignment exceptions or significantly degrade
   * performance. Therefore, it is safer to perform word-aligned copies only
   * when both d8 and s8 are aligned to the same 4-byte boundary.
   *
   * First, check whether dest and src share the same alignment offset.
   * If the alignments differ, proceed with byte-by-byte copying
   * (or adjust alignment first by copying a few bytes).
   */

  /*
   * If the lower 2 bits of the difference are non-zero, src and dest are
   * misaligned */
  if ((((UINT)d8) ^ ((UINT)s8)) & 0x3) {
    /* Perform byte-by-byte copying */
    while (n--) {
      *d8++ = *s8++;
    }
    return dest;
  }

  /*
   * Since the alignments of src and dest are the same,
   * align dest to a 4-byte boundary by performing byte-by-byte copying.
   */
  while ((((UINT)d8) & 0x3) && n) {
    *d8++ = *s8++;
    n--;
  }

  /*
   * After the above, dest is aligned to a 4-byte boundary,
   * allowing word-aligned copying
   */
  {
    /* Recast the pointers to 32-bit (4-byte) pointers */
    UW *dw = (UW *)d8;
    CONST UW *sw = (CONST UW *)s8;

    /*
     * Perform larger loop iterations to reduce loop overhead
     * by copying data in blocks of 32 bytes (8 words per iteration).
     */
    while (n >= 32) {
      dw[0] = sw[0];
      dw[1] = sw[1];
      dw[2] = sw[2];
      dw[3] = sw[3];
      dw[4] = sw[4];
      dw[5] = sw[5];
      dw[6] = sw[6];
      dw[7] = sw[7];
      dw += 8;
      sw += 8;
      n -= 32;
    }

    /* Copy remaining data in blocks of 4 bytes (1 word per iteration) */
    while (n >= 4) {
      *dw++ = *sw++;
      n -= 4;
    }

    /*
     * After finishing word-aligned copying,
     * revert the pointers back to 8-bit pointers
     */
    d8 = (UB *)dw;
    s8 = (CONST UB *)sw;
  }

  /* Copy the remaining bytes */
  while (n--) {
    *d8++ = *s8++;
  }

  return dest;
}

__attribute__((weak, alias("tkmc_memcpy"))) void *
memcpy(void *restrict dest, CONST void *restrict src, size_t n);
