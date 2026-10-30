/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Tests of tkmc_memcpy() and, for objects of other types, tkmc_memset(). Unlike
// the memset suite, this ELF uses the kernel library's own functions, built as
// the kernel is.
//
// The typed cases write objects of several types, copy or fill them, and read
// them back through their own types at once. If a word access were typed as
// UW, an optimizer that inlines the function could keep the old values of a
// float or a struct (see kernel/memcpy.c). Where this was checked (clang 20),
// no preset inlined the functions, not even with LTO: the calls stay opaque and
// the cases check the bytes only. The word-access-tbaa test checks the access
// types themselves.
//
// The cases prepare and judge buffers through volatile pointers, so no loop of
// theirs becomes a call to the functions they test.

#include <tk/tkernel.h>
#include <unity.h>

#include "../runner.h"
#include "memcpy.h"
#include "memset.h"

enum { CANARY = 8, MAX_OFFSET = 3, MAX_LENGTH = 70 };
enum { BUF_SIZE = CANARY + MAX_OFFSET + MAX_LENGTH + CANARY };
enum { BACKGROUND = 0x00, FILL = 0x3f };

static _Alignas(4) UB g_src[BUF_SIZE];
static _Alignas(4) UB g_dst[BUF_SIZE];

// Every source byte differs from its neighbours and from the background, so a
// shifted, repeated or missing byte shows.
static UB source_byte(UINT i) { return (UB)(0x80 | (i & 0x7f)); }

static void report(const char *what, UINT length, UINT src_offset,
                   UINT dst_offset, UINT index) {
  UnityPrint(what);
  UnityPrint(": length ");
  UnityPrintNumberUnsigned(length);
  UnityPrint(", source offset ");
  UnityPrintNumberUnsigned(src_offset);
  UnityPrint(", destination offset ");
  UnityPrintNumberUnsigned(dst_offset);
  UnityPrint(", byte ");
  UnityPrintNumberUnsigned(index);
  UnityPrint("\n");
}

// Copies length bytes from offset src_offset to offset dst_offset, then checks
// every byte of both buffers, canaries included.
static void check_copy(UINT length, UINT src_offset, UINT dst_offset) {
  volatile UB *src = g_src;
  volatile UB *dst = g_dst;
  for (UINT i = 0; i < BUF_SIZE; ++i) {
    src[i] = source_byte(i);
    dst[i] = BACKGROUND;
  }
  UINT from = CANARY + src_offset;
  UINT to = CANARY + dst_offset;

  void *result = tkmc_memcpy(&g_dst[to], &g_src[from], length);
  TEST_ASSERT_EQUAL_PTR(&g_dst[to], result);

  for (UINT i = 0; i < BUF_SIZE; ++i) {
    UB want = (i >= to && i < to + length) ? source_byte(from + (i - to))
                                           : (UB)BACKGROUND;
    if (dst[i] != want) {
      report("wrong destination byte", length, src_offset, dst_offset, i);
      TEST_FAIL_MESSAGE("wrong bytes");
    }
    if (src[i] != source_byte(i)) {
      report("source changed", length, src_offset, dst_offset, i);
      TEST_FAIL_MESSAGE("source changed");
    }
  }
}

// The byte and word paths, aligned or not, and every length up to past two
// 32 byte blocks.
static void copies_exactly_the_requested_range(void) {
  for (UINT src_offset = 0; src_offset <= MAX_OFFSET; ++src_offset) {
    for (UINT dst_offset = 0; dst_offset <= MAX_OFFSET; ++dst_offset) {
      for (UINT length = 0; length <= MAX_LENGTH; ++length) {
        check_copy(length, src_offset, dst_offset);
      }
    }
  }
}

// 32 bytes, so that the word loops run, and no padding, so that copying the
// struct copies every byte that is compared.
typedef struct {
  UW count;
  UW flags;
  UH low;
  UH high;
  void *link;
  UW words[4];
} record_t;
_Static_assert(sizeof(record_t) == 4 + 4 + 2 + 2 + sizeof(void *) + 16,
               "record_t has no padding");
_Static_assert(sizeof(record_t) >= 32, "record_t takes the word path");

static void pattern(void *object, UINT size, UB seed) {
  volatile UB *p = object;
  for (UINT i = 0; i < size; ++i) {
    p[i] = (UB)(seed + i);
  }
}

static void fill_pattern(void *object, UINT size) {
  volatile UB *p = object;
  for (UINT i = 0; i < size; ++i) {
    p[i] = FILL;
  }
}

static void check_same_bytes(const void *observed, const void *expected,
                             UINT size) {
  const volatile UB *o = observed;
  const volatile UB *e = expected;
  for (UINT i = 0; i < size; ++i) {
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(e[i], o[i], "a typed read saw old bytes");
  }
}

// Writes the old values of an array through its type, copies or fills it, and
// reads it back through its type at once.
#define CHECK_ARRAY(type, count, call)                                         \
  do {                                                                         \
    type target[count], source[count], expected[count], observed[count];       \
    pattern(source, sizeof source, 0x41);                                      \
    for (UINT i = 0; i < (count); ++i) {                                       \
      target[i] = (type)7; /* a constant: no soft-float library is linked */   \
    }                                                                          \
    call;                                                                      \
    for (UINT i = 0; i < (count); ++i) {                                       \
      observed[i] = target[i];                                                 \
    }                                                                          \
    check_same_bytes(observed, expected, sizeof observed);                     \
  } while (0)

#define COPY_ARRAY(type, count)                                                \
  CHECK_ARRAY(type, count,                                                     \
              (pattern(expected, sizeof expected, 0x41),                       \
               tkmc_memcpy(target, source, sizeof target)))
#define FILL_ARRAY(type, count)                                                \
  CHECK_ARRAY(type, count,                                                     \
              (fill_pattern(expected, sizeof expected),                        \
               tkmc_memset(target, FILL, sizeof target)))

static void write_old_record(record_t *r) {
  r->count = 1;
  r->flags = 2;
  r->low = 3;
  r->high = 4;
  r->link = r;
  for (UINT i = 0; i < 4; ++i) {
    r->words[i] = 5 + i;
  }
}

// Member by member: a struct assignment is a char-typed copy, maybe even a call
// to memcpy(), which is tkmc_memcpy() here, and would read no member through
// its type.
static void read_record(record_t *observed, const record_t *target) {
  observed->count = target->count;
  observed->flags = target->flags;
  observed->low = target->low;
  observed->high = target->high;
  observed->link = target->link;
  for (UINT i = 0; i < 4; ++i) {
    observed->words[i] = target->words[i];
  }
}

static void copy_reaches_objects_of_every_type(void) {
  COPY_ARRAY(UW, 8);
  COPY_ARRAY(float, 8);
  COPY_ARRAY(double, 4);

  record_t target, source, expected, observed;
  pattern(&source, sizeof source, 0x41);
  pattern(&expected, sizeof expected, 0x41);
  write_old_record(&target);
  tkmc_memcpy(&target, &source, sizeof target);
  read_record(&observed, &target);
  check_same_bytes(&observed, &expected, sizeof observed);
}

static void fill_reaches_objects_of_every_type(void) {
  FILL_ARRAY(UW, 8);
  FILL_ARRAY(float, 8);
  FILL_ARRAY(double, 4);

  record_t target, expected, observed;
  fill_pattern(&expected, sizeof expected);
  write_old_record(&target);
  tkmc_memset(&target, FILL, sizeof target);
  read_record(&observed, &target);
  check_same_bytes(&observed, &expected, sizeof observed);
}

static const test_case_t s_cases[] = {
    {"copies_exactly_the_requested_range", copies_exactly_the_requested_range},
    {"copy_reaches_objects_of_every_type", copy_reaches_objects_of_every_type},
    {"fill_reaches_objects_of_every_type", fill_reaches_objects_of_every_type},
};

TEST_SUITE_MAIN("memcpy", s_cases, 5000)
