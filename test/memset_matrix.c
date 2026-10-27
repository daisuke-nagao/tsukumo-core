/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include "memset_matrix.h"

#include <tk/tkernel.h>
#include <unity.h>

#include "memset.h"

enum { CANARY = 8, MAX_OFFSET = 3, MAX_LENGTH = 70 };

static UB g_buf[CANARY + MAX_OFFSET + MAX_LENGTH + CANARY];

// Every access to the buffer except tkmc_memset() itself goes through a
// volatile pointer. A plain loop could be turned into a call to memset(), which
// is tkmc_memset() here, and the test would then use the code it checks to
// prepare and judge its own result.
static volatile UB *view(void) { return g_buf; }

// The background is the complement of the fill value, so it never equals it
// and a byte that was or was not written can always be told apart.
static void prepare(UB background) {
  volatile UB *p = view();
  for (UINT i = 0; i < sizeof(g_buf); ++i) {
    p[i] = background;
  }
}

// Returns the index of the first byte that is not as expected, or -1.
static INT first_wrong_byte(UINT begin, UINT length, UB value, UB background) {
  volatile UB *p = view();
  for (UINT i = 0; i < sizeof(g_buf); ++i) {
    UB want = (i >= begin && i < begin + length) ? value : background;
    if (p[i] != want) {
      return (INT)i;
    }
  }
  return -1;
}

static void report(const char *what, UINT value, UINT length, UINT offset,
                   INT index) {
  UnityPrint(what);
  UnityPrint(": value ");
  UnityPrintNumberUnsigned(value);
  UnityPrint(", length ");
  UnityPrintNumberUnsigned(length);
  UnityPrint(", offset ");
  UnityPrintNumberUnsigned(offset);
  if (index >= 0) {
    UnityPrint(", byte ");
    UnityPrintNumberUnsigned((UINT)index);
  }
  UnityPrint("\n");
}

void memset_matrix_check(void) {
  for (UINT offset = 0; offset <= MAX_OFFSET; ++offset) {
    for (UINT length = 0; length <= MAX_LENGTH; ++length) {
      for (UINT value = 0; value <= 0xff; ++value) {
        UB background = (UB)~value;
        UINT begin = CANARY + offset;

        prepare(background);
        void *result = tkmc_memset(&g_buf[begin], (int)value, length);

        if (result != &g_buf[begin]) {
          report("tkmc_memset did not return its destination", value, length,
                 offset, -1);
          TEST_FAIL_MESSAGE("wrong return value");
        }
        INT bad = first_wrong_byte(begin, length, (UB)value, background);
        if (bad >= 0) {
          report("tkmc_memset wrote the wrong bytes", value, length, offset,
                 bad);
          TEST_FAIL_MESSAGE("wrong bytes");
        }
      }
    }
  }
}
