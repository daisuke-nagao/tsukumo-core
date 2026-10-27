/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// The kernel must clear .bss at boot. test/bss_dirty_entry.S fills it with 0xa5
// before the kernel starts; this suite checks that no stretch of 0xa5 is left
// (the two ends, any run of 4 bytes), and that a block that nothing writes is
// zero. It has no test runner: the runner keeps its state in .bss, which is
// what is being tested, so a failure would show as a hang instead of as a line
// of output. The result is "BSS OK", or a line that says what is wrong, on the
// UART, and the exit status of the run.
//
// What it does not check, and why:
// - The bytes before _bss_start. A canary there would have to sit in the
//   alignment gap before .sbss, which is up to 15 bytes and not under the
//   suite's control, so no test can put one in a reliable place.
// - A missed stretch of 1 to 3 bytes in the middle of .bss, outside the probe.
//   Shorter than a run, it is not told from a stray 0xa5 that the kernel may
//   hold; and a longer one needs only a clear that skips a whole block.
// - The bytes after _bss_end. That is the boot stack, in use; a clear that
//   reached it would crash the kernel, which shows as a hang.
// - The first and last byte of .bss are compared with 0xa5 while the kernel is
//   running, so a variable that the kernel writes there (one that is 0xa5 by
//   chance, or one written before this check) would hide an error at the edge,
//   or look like one. Neither happens with the current layout; if the suite
//   ever fails only at an edge, look at what the linker put there first.

#include <tk/tkernel.h>

#include "../finish.h"
#include "../putstring.h"

extern unsigned char _bss_start[];
extern unsigned char _bss_end[];
extern volatile UW bss_dirty_ran;

enum { PATTERN = 0xa5, RUN_LENGTH = 4, PROBE_SIZE = 512 };

// Nothing writes this, so after a boot it must be all zero. It tells a clear
// that wrote the wrong value, or missed a short stretch inside the block, from
// one that worked: the 0xa5 checks only see what is left of the 0xa5. volatile,
// because the compiler knows that a static that is never written is zero, and
// would remove the check.
static volatile unsigned char g_zero_probe[PROBE_SIZE];

static __attribute__((noreturn)) void fail(const char *what,
                                           const unsigned char *at) {
  putstring("BSS NOT CLEARED: ");
  putstring(what);
  putstring(" at ");
  puthex((UW)at);
  putstring("\n");
  test_exit(1);
}

// The first task of the system. It runs on a small stack and keeps to simple
// loops.
void usermain(int a0) {
  (void)a0;

  if (bss_dirty_ran != 1) {
    putstring("BSS DIRTY ENTRY DID NOT RUN\n");
    test_exit(1);
  }

  // The ends are checked on their own: a clear that stops one byte early or
  // starts one byte late leaves a single 0xa5 that no run would show.
  if (_bss_start[0] == PATTERN) {
    fail("first byte", _bss_start);
  }
  if (_bss_end[-1] == PATTERN) {
    fail("last byte", &_bss_end[-1]);
  }

  for (UINT i = 0; i < PROBE_SIZE; ++i) {
    if (g_zero_probe[i] != 0) {
      fail("a byte that nothing writes is not zero",
           (const unsigned char *)&g_zero_probe[i]);
    }
  }

  // The kernel has used .bss since it booted, so not every byte is zero. A run
  // of 0xa5 bytes is what an uncleared stretch looks like, and nothing the
  // kernel writes is likely to look like it.
  UINT run = 0;
  for (const unsigned char *p = _bss_start; p < _bss_end; ++p) {
    run = (*p == PATTERN) ? run + 1 : 0;
    if (run >= RUN_LENGTH) {
      fail("a run of 0xa5", p - (RUN_LENGTH - 1));
    }
  }

  putstring("BSS OK\n");
  test_exit(0);
}
