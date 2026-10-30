/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// The error code macros of tk/errno.h: a code joined from a main code and a
// sub code by ERCD() comes apart again with MERCD() and SERCD(). MERCD() used
// to divide, which rounds toward zero and gave a main code one too high
// whenever the sub code was not 0 (TK_SUPPORT_SERCD is TRUE).

#include <tk/tkernel.h>
#include <unity.h>

void test_the_main_and_sub_error_codes_come_apart_as_they_were_joined(void) {
  static const struct {
    ER mer;
    H ser;
  } rows[] = {{0, 0},    {-17, 0}, {-17, 1}, {-17, 0x7fff},
              {-17, -1}, {-66, 1}, {5, 1}};
  for (UINT i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
    ER er = ERCD(rows[i].mer, rows[i].ser);
    TEST_ASSERT_EQUAL_INT(rows[i].mer, MERCD(er));
    TEST_ASSERT_EQUAL_INT(rows[i].ser, SERCD(er));
  }
  TEST_ASSERT_EQUAL_INT(E_PAR, ERCD(MERCD(E_PAR), SERCD(E_PAR)));
}
