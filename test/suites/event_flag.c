/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Tests of event flags (tk_cre_flg, tk_wai_flg, tk_set_flg).
//
// Event flags cannot be deleted, so every case that creates one leaks it, and
// the kernel has 16. This suite creates six.
//
// The cases that block run a child at priority 5, below the case (4): the child
// only starts once the case has blocked in tk_wai_flg(), and then sets bits.

#include <tk/tkernel.h>
#include <unity.h>

#include "../runner.h"

enum {
  CASE_WAIT_MS = 1000,
  CHILD_WAIT_MS = 2000,
  TIMEOUT_MS = 50,
  SETTER_PRIORITY = 5
};
_Static_assert(SETTER_PRIORITY > TEST_CASE_PRIORITY,
               "the child must not run until the case blocks");

static ID create_flag(ATR attributes, UINT initial) {
  ID flgid = tk_cre_flg(
      &(T_CFLG){.exinf = 0, .flgatr = attributes, .iflgptn = initial});
  TEST_ASSERT_GREATER_THAN(E_OK, flgid);
  return flgid;
}

// Waits on a flag without blocking (TMO_POL) in every wait mode, and checks
// which bits each mode clears and which pattern it reports.
static void poll_wait_modes(void) {
  ID flgid = create_flag(TA_TFIFO | TA_WSGL, 0x00FFFFFF);
  UINT flgptn = 0;

  // All requested bits are set, and TWF_BITCLR clears just those bits.
  TEST_ASSERT_EQUAL(E_OK, tk_wai_flg(flgid, 0x00a5a5a5, TWF_ANDW | TWF_BITCLR,
                                     &flgptn, TMO_POL));
  TEST_ASSERT_EQUAL_HEX32(0x00FFFFFF, flgptn);
  // The same request now fails, because the bits are gone.
  TEST_ASSERT_EQUAL(
      E_TMOUT,
      tk_wai_flg(flgid, 0x00a5a5a5, TWF_ANDW | TWF_BITCLR, &flgptn, TMO_POL));

  // One bit of an OR request is enough, and TWF_CLR clears the whole pattern.
  // The pattern reported is the one before it was cleared.
  TEST_ASSERT_EQUAL(
      E_OK, tk_wai_flg(flgid, 0x00a5a5a7, TWF_ORW | TWF_CLR, &flgptn, TMO_POL));
  TEST_ASSERT_EQUAL_HEX32(0x005a5a5a, flgptn);
  TEST_ASSERT_EQUAL(E_TMOUT,
                    tk_wai_flg(flgid, 0xFFFFFFFF, TWF_ORW, &flgptn, TMO_POL));

  // An AND request needs every bit it names: one of two is not enough.
  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(flgid, 0x00000001));
  TEST_ASSERT_EQUAL(E_TMOUT,
                    tk_wai_flg(flgid, 0x00000003, TWF_ANDW, &flgptn, TMO_POL));
  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(flgid, 0x00000002));
  TEST_ASSERT_EQUAL(E_OK, tk_wai_flg(flgid, 0x00000003, TWF_ANDW | TWF_CLR,
                                     &flgptn, TMO_POL));

  // A bit set later satisfies an AND request for it, and TWF_CLR consumes it.
  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(flgid, 0x00000008));
  TEST_ASSERT_EQUAL(E_OK, tk_wai_flg(flgid, 0x00000008, TWF_ANDW | TWF_CLR,
                                     &flgptn, TMO_POL));
  TEST_ASSERT_EQUAL(E_TMOUT,
                    tk_wai_flg(flgid, 0x00000008, TWF_ANDW, &flgptn, TMO_POL));
}

// A wait with a time limit that nobody satisfies returns E_TMOUT, and not
// before the limit. Only a lower bound is checked: a stalled host makes the
// wait longer, never shorter.
static void wait_times_out(void) {
  ID flgid = create_flag(TA_TFIFO | TA_WSGL, 0);
  UINT flgptn = 0;

  UW start = test_mtime_mid_tick();
  TEST_ASSERT_EQUAL(
      E_TMOUT, tk_wai_flg(flgid, 0x00000001, TWF_ORW, &flgptn, TIMEOUT_MS));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(TIMEOUT_MS * TEST_MTIME_PER_MS,
                                      (UW)(test_mtime() - start));
}

// What a bit-setting child does after the case has blocked.
typedef struct {
  ID flgid;
  UINT first;  // Set first.
  UINT second; // Set after pause_ms; 0 sets nothing.
  TMO pause_ms;
} setter_t;

static void set_bits(void *arg) {
  setter_t *setter = arg;
  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(setter->flgid, setter->first));
  if (setter->second != 0) {
    tk_dly_tsk(setter->pause_ms);
    TEST_ASSERT_EQUAL(E_OK, tk_set_flg(setter->flgid, setter->second));
  }
  test_checkpoint();
}

// A blocked OR wait wakes on one of its bits, reports the pattern at that
// moment, and TWF_BITCLR then clears only the bits it asked for.
static setter_t g_or_setter;

static void blocked_or_wait_wakes(void) {
  ID flgid = create_flag(TA_TFIFO | TA_WSGL, 0);
  g_or_setter = (setter_t){flgid, 0x00000006, 0, 0};
  UINT flgptn = 0;

  test_spawn(set_bits, SETTER_PRIORITY, &g_or_setter);
  TEST_ASSERT_EQUAL(E_OK, tk_wai_flg(flgid, 0x00000012, TWF_ORW | TWF_BITCLR,
                                     &flgptn, CASE_WAIT_MS));
  test_join_expect(1);

  TEST_ASSERT_EQUAL_HEX32(0x00000006, flgptn);
  // Bit 1 was asked for and is gone; bit 2 was not and is still set.
  TEST_ASSERT_EQUAL(E_TMOUT,
                    tk_wai_flg(flgid, 0x00000002, TWF_ORW, &flgptn, TMO_POL));
  TEST_ASSERT_EQUAL(E_OK,
                    tk_wai_flg(flgid, 0x00000004, TWF_ANDW, &flgptn, TMO_POL));
}

// A blocked AND wait stays blocked while only some of its bits are set: the
// child sets one bit, pauses, then sets the other, and the case must not wake
// before the pause is over. TWF_CLR then clears the whole pattern.
static setter_t g_and_setter;

static void blocked_and_wait_needs_all_bits(void) {
  ID flgid = create_flag(TA_TFIFO | TA_WSGL, 0);
  g_and_setter = (setter_t){flgid, 0x00000001, 0x00000002, TIMEOUT_MS};
  UINT flgptn = 0;

  UW start = test_mtime_mid_tick();
  test_spawn(set_bits, SETTER_PRIORITY, &g_and_setter);
  TEST_ASSERT_EQUAL(E_OK, tk_wai_flg(flgid, 0x00000003, TWF_ANDW | TWF_CLR,
                                     &flgptn, CASE_WAIT_MS));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(TIMEOUT_MS * TEST_MTIME_PER_MS,
                                      (UW)(test_mtime() - start));
  test_join_expect(1);

  TEST_ASSERT_EQUAL_HEX32(0x00000003, flgptn);
  TEST_ASSERT_EQUAL(E_TMOUT,
                    tk_wai_flg(flgid, 0xFFFFFFFF, TWF_ORW, &flgptn, TMO_POL));
}

// A flag created with TA_WSGL accepts one waiter. The case waits first; the
// child runs below the case, so it can only start once the case is blocked, and
// its own wait must be refused. Its tk_set_flg() then wakes the case.
static void second_waiter_child(void *arg) {
  ID flgid = (ID)(INT)arg;
  UINT flgptn = 0;
  TEST_ASSERT_EQUAL(
      E_OBJ, tk_wai_flg(flgid, 0xFFFFFFFF, TWF_ORW, &flgptn, CHILD_WAIT_MS));
  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(flgid, 0xFFFFFFFF));
  test_checkpoint();
}

static void single_waiter_is_enforced(void) {
  ID flgid = create_flag(TA_TFIFO | TA_WSGL, 0);

  test_spawn(second_waiter_child, SETTER_PRIORITY, (void *)(INT)flgid);

  UINT flgptn = 0;
  TEST_ASSERT_EQUAL(E_OK, tk_wai_flg(flgid, 0x00000008, TWF_ANDW | TWF_CLR,
                                     &flgptn, CASE_WAIT_MS));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, flgptn);
  test_join_expect(1);

  // TWF_CLR cleared the whole pattern on the wake path too.
  TEST_ASSERT_EQUAL(E_TMOUT,
                    tk_wai_flg(flgid, 0xFFFFFFFF, TWF_ORW, &flgptn, TMO_POL));
}

// A wait that times out while a task of higher priority is running: the waiter
// is ready at once, but it cannot run, and a set that comes in the meantime
// must not treat it as a waiter any more. It asked for TWF_CLR, so a waiter
// that was woken wrongly would clear the pattern the setter left.
enum { SPIN_MS = 100 };

typedef struct {
  ID flgid;
  volatile ER result;
} timed_wait_t;

static timed_wait_t g_timed;

static void timed_waiter(void *arg) {
  timed_wait_t *timed = arg;
  UINT flgptn = 0;
  timed->result = tk_wai_flg(timed->flgid, 0x00000001, TWF_ANDW | TWF_CLR,
                             &flgptn, TIMEOUT_MS);
  test_checkpoint();
}

static void set_after_timeout_keeps_the_pattern(void) {
  g_timed.flgid = create_flag(TA_TFIFO | TA_WSGL, 0);
  g_timed.result = E_SYS;

  test_spawn(timed_waiter, SETTER_PRIORITY, &g_timed);
  tk_dly_tsk(10); // The waiter is below the case: it runs now, and blocks.

  // Spin without blocking, past the waiter's time limit. The waiter is ready
  // by then and is starved by the case.
  UW start = test_mtime();
  while ((UW)(test_mtime() - start) < SPIN_MS * TEST_MTIME_PER_MS) {
  }

  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(g_timed.flgid, 0x00000001));
  test_join_expect(1);
  TEST_ASSERT_EQUAL(E_TMOUT, g_timed.result);

  UINT flgptn = 0;
  TEST_ASSERT_EQUAL(E_OK, tk_wai_flg(g_timed.flgid, 0x00000001,
                                     TWF_ANDW | TWF_CLR, &flgptn, TMO_POL));
  TEST_ASSERT_EQUAL_HEX32(0x00000001, flgptn);
}

static const test_case_t s_cases[] = {
    {"poll_wait_modes", poll_wait_modes},
    {"wait_times_out", wait_times_out},
    {"blocked_or_wait_wakes", blocked_or_wait_wakes},
    {"blocked_and_wait_needs_all_bits", blocked_and_wait_needs_all_bits},
    {"single_waiter_is_enforced", single_waiter_is_enforced},
    {"set_after_timeout_keeps_the_pattern",
     set_after_timeout_keeps_the_pattern},
};

TEST_SUITE_MAIN("event-flag", s_cases, 5000)
