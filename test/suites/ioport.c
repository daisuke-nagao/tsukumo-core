/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// The port access functions of sys/ioport.h, on a RAM buffer with guard bytes
// around the port: each access has its own width at its own address, little
// endian, and in_d()/out_d() take the low word at the port and the high word
// at port + 4. The order of the two word accesses of in_d()/out_d() cannot be
// seen in RAM; only a device register that reacts to an access could show it.

#include <tk/tkernel.h>
#include <unity.h>

#include "runner.h"

enum { GUARD = 0xa5, PORT_OFFSET = 8, BUFFER_BYTES = 24 };

static volatile _Alignas(8) UB s_buffer[BUFFER_BYTES];

static INT port(void) { return (INT)&s_buffer[PORT_OFFSET]; }

static void fill_with_guard(void) {
  for (UINT i = 0; i < BUFFER_BYTES; ++i) {
    s_buffer[i] = GUARD;
  }
}

// The bytes of the port, from PORT_OFFSET on, hold expected, and every byte
// around them still holds the guard.
static void check_bytes(const UB *expected, UINT size) {
  for (UINT i = 0; i < BUFFER_BYTES; ++i) {
    BOOL in_port = i >= PORT_OFFSET && i < PORT_OFFSET + size;
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(
        in_port ? expected[i - PORT_OFFSET] : GUARD, s_buffer[i],
        in_port ? "a byte of the port" : "a guard byte was written");
  }
}

static void set_bytes(const UB *bytes, UINT size) {
  fill_with_guard();
  for (UINT i = 0; i < size; ++i) {
    s_buffer[PORT_OFFSET + i] = bytes[i];
  }
}

static void out_b_writes_one_byte(void) {
  fill_with_guard();
  out_b(port(), 0x5a);
  check_bytes((const UB[]){0x5a}, 1);
}

static void out_h_writes_two_bytes_low_byte_first(void) {
  fill_with_guard();
  out_h(port(), 0x1234);
  check_bytes((const UB[]){0x34, 0x12}, 2);
}

static void out_d_writes_low_word_at_port_and_high_word_above(void) {
  fill_with_guard();
  out_d(port(), 0x1122334455667788ULL);
  check_bytes((const UB[]){0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11}, 8);
}

static void in_b_reads_one_byte(void) {
  set_bytes((const UB[]){0x5a, 0x11}, 2);
  TEST_ASSERT_EQUAL_HEX8(0x5a, in_b(port()));
}

static void in_h_reads_two_bytes_low_byte_first(void) {
  set_bytes((const UB[]){0x34, 0x12, 0x99}, 3);
  TEST_ASSERT_EQUAL_HEX16(0x1234, in_h(port()));
}

static void in_d_reads_low_word_at_port_and_high_word_above(void) {
  set_bytes((const UB[]){0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11}, 8);
  const UD value = in_d(port());
  // Unity has no 64 bit asserts on RV32.
  TEST_ASSERT_EQUAL_HEX32(0x55667788, (UW)value);
  TEST_ASSERT_EQUAL_HEX32(0x11223344, (UW)(value >> 32));
}

static const test_case_t s_cases[] = {
    CASE(out_b_writes_one_byte),
    CASE(out_h_writes_two_bytes_low_byte_first),
    CASE(out_d_writes_low_word_at_port_and_high_word_above),
    CASE(in_b_reads_one_byte),
    CASE(in_h_reads_two_bytes_low_byte_first),
    CASE(in_d_reads_low_word_at_port_and_high_word_above),
};

TEST_SUITE_MAIN("ioport", s_cases, 5000)
