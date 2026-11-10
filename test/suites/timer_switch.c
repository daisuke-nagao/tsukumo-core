/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Which trap switches tasks. A task that a timer interrupt wakes must run after
// the switch made inside that timer trap, not after a second trap that the
// timer trap only requests (the machine software interrupt, MSIP). A task that
// wakes another by a call, or blocks, still switches in the software
// interrupt.
//
// schedule() is the one function that every task switch of the RV32 trap
// entries goes through. It is wrapped at link time (--wrap=schedule), and the
// wrapper keeps the mcause of the trap it runs in. A task that a trap switches
// to reads the value left by that switch, since no other switch can come
// between that one and the task's first instruction. A task that the end of
// another resumes is switched to without schedule() and reads an older value;
// none of the tasks that read it here is resumed that way.

#include <tk/tkernel.h>
#include <unity.h>

#include "runner.h"

extern void **__real_schedule(void *sp);

enum {
  CHILD_PRIORITY = 2,
  SLEEPS = 4,
  // An odd number of ticks (40 ms is 4, plus one tick for the timer edge, see
  // tkmc_ms_to_ticks() in kernel/timer.h). The runner polls every other tick
  // (tk_dly_tsk(1) in test/runner.c), so two wake-ups an odd number of ticks
  // apart cannot both fall on a tick where the runner wakes too. If either
  // number changes so that every wake-up shares a tick, case 1 fails.
  SLEEP_MS = 40,
  LONG_SLEEP_MS = 1000, // The longest the child of case 2 may sleep.
  SPIN_MS = 2000        // The longest case 1 waits for its child.
};
_Static_assert(CHILD_PRIORITY < TEST_CASE_PRIORITY,
               "the child must preempt the case, to sleep before it goes on");

static const UW MCAUSE_SOFTWARE = 0x80000003u; // Machine software interrupt
static const UW MCAUSE_TIMER = 0x80000007u;    // Machine timer interrupt

static volatile UW s_last_mcause;

void **__wrap_schedule(void *sp) {
  UW cause;
  __asm__ volatile("csrr %0, mcause" : "=r"(cause));
  s_last_mcause = cause;
  return __real_schedule(sp);
}

static volatile BOOL s_done;
static volatile UW s_wake_cause[SLEEPS];

static BOOL wait_until_done(void) {
  UW start = test_mtime();
  while (!s_done) {
    if ((UW)(test_mtime() - start) > SPIN_MS * TEST_MTIME_PER_MS) {
      return FALSE;
    }
  }
  return TRUE;
}

static void timer_sleeping_child(void *arg) {
  (void)arg;
  for (UINT i = 0; i < SLEEPS; ++i) {
    TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(SLEEP_MS));
    s_wake_cause[i] = s_last_mcause;
  }
  s_done = TRUE;
  test_checkpoint();
}

// The case spins, so the timer trap that ends a sleep interrupts a task that
// the child outranks. The runner can wake in the same trap and run first, so
// not every wake-up has to be switched to in the timer trap; the sleeps are
// spaced so that it cannot be all of them.
static void timer_wake_switches_inside_the_timer_trap(void) {
  s_done = FALSE;
  test_spawn(timer_sleeping_child, CHILD_PRIORITY, NULL);
  TEST_ASSERT_TRUE_MESSAGE(wait_until_done(), "the child never finished");
  test_join_expect(1);

  UINT in_timer_trap = 0;
  for (UINT i = 0; i < SLEEPS; ++i) {
    TEST_ASSERT_TRUE_MESSAGE(s_wake_cause[i] == MCAUSE_TIMER ||
                                 s_wake_cause[i] == MCAUSE_SOFTWARE,
                             "a switch came from a trap other than the timer "
                             "or software interrupt");
    if (s_wake_cause[i] == MCAUSE_TIMER) {
      in_timer_trap += 1;
    }
  }
  TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(
      1, in_timer_trap, "no timer wake-up switched inside the timer trap");
}

static volatile UW s_cause_after_wakeup;

static void wakeup_sleeping_child(void *arg) {
  (void)arg;
  TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(LONG_SLEEP_MS));
  s_cause_after_wakeup = s_last_mcause;
  test_checkpoint();
}

// The child is asleep when test_spawn() returns, since it outranks the case.
// tk_wup_tsk() asks for the switch with dispatch(), and the software interrupt
// takes it as soon as the call enables interrupts again.
static void task_dispatch_still_switches_in_the_software_interrupt(void) {
  s_cause_after_wakeup = 0;
  ID child = test_spawn(wakeup_sleeping_child, CHILD_PRIORITY, NULL);
  TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(child));
  test_join_expect(1);
  TEST_ASSERT_EQUAL_HEX32(MCAUSE_SOFTWARE, s_cause_after_wakeup);
}

// Every register that the trap frame holds, except ra, s0, a0 and a1, holds a
// value of its own (0xa5000000 plus its number) for as long as a task spins.
// a0 holds the address of the flag, then the count of the registers that differ
// at the end, and a1 is a scratch.
#define PATTERN_REGS(X)                                                        \
  X(t0, 0xa5000005)                                                            \
  X(t1, 0xa5000006)                                                            \
  X(t2, 0xa5000007)                                                            \
  X(s1, 0xa5000009)                                                            \
  X(a2, 0xa500000c)                                                            \
  X(a3, 0xa500000d)                                                            \
  X(a4, 0xa500000e)                                                            \
  X(a5, 0xa500000f)                                                            \
  X(a6, 0xa5000010)                                                            \
  X(a7, 0xa5000011)                                                            \
  X(s2, 0xa5000012)                                                            \
  X(s3, 0xa5000013)                                                            \
  X(s4, 0xa5000014)                                                            \
  X(s5, 0xa5000015)                                                            \
  X(s6, 0xa5000016)                                                            \
  X(s7, 0xa5000017)                                                            \
  X(s8, 0xa5000018)                                                            \
  X(s9, 0xa5000019)                                                            \
  X(s10, 0xa500001a)                                                           \
  X(s11, 0xa500001b)                                                           \
  X(t3, 0xa500001c)                                                            \
  X(t4, 0xa500001d)                                                            \
  X(t5, 0xa500001e)                                                            \
  X(t6, 0xa500001f)

#define SET_PATTERN(reg, value) "li " #reg ", " #value "\n"
#define COUNT_IF_CHANGED(reg, value)                                           \
  "li a1, " #value "\n"                                                        \
  "xor a1, a1, " #reg "\n"                                                     \
  "snez a1, a1\n"                                                              \
  "add a0, a0, a1\n"
#define CLOBBER(reg, value) #reg,

// The memory that the spinning task shares: the flag it waits for, and two
// counters that every pass of its loop raises one after the other. A resume at
// the wrong instruction repeats or skips a raise, and the counters then differ
// by the time the loop ends; a loop of two idempotent instructions would not
// show a resume address that was off by one.
enum { STATE_FLAG, STATE_COUNT_A, STATE_COUNT_B, STATE_WORDS };
static volatile UW s_state[STATE_WORDS];

// Returns the number of registers that changed while the task waited for the
// flag.
static UW spin_holding_registers(volatile UW *state) {
  register volatile UW *address __asm__("a0") = state;
  __asm__ volatile(
      PATTERN_REGS(SET_PATTERN) "1: lw a1, 4(a0)\n"
                                "addi a1, a1, 1\n"
                                "sw a1, 4(a0)\n"
                                "lw a1, 8(a0)\n"
                                "addi a1, a1, 1\n"
                                "sw a1, 8(a0)\n"
                                "lw a1, 0(a0)\n"
                                "beqz a1, 1b\n"
                                "li a0, 0\n" PATTERN_REGS(COUNT_IF_CHANGED)
      : "+r"(address)
      :
      : PATTERN_REGS(CLOBBER) "a1", "memory");
  return (UW)address;
}

static void flag_setting_child(void *arg) {
  (void)arg;
  ER ercd = tk_dly_tsk(SLEEP_MS);
  // Before the assert, so that a failure here ends the spin of the case.
  s_state[STATE_FLAG] = 1;
  TEST_ASSERT_EQUAL(E_OK, ercd);
  test_checkpoint();
}

// A timer trap that switches away must save the registers and the resume
// address of the task it interrupts, as the software interrupt does. The case
// is resumed by the end of the child (__launch_task, not a trap exit): the
// child outranks it and the runner cannot be ready while the child runs, so
// this is also the check of that path. Keep it so.
static void a_task_preempted_by_the_timer_resumes_intact(void) {
  for (UINT i = 0; i < STATE_WORDS; ++i) {
    s_state[i] = 0;
  }
  test_spawn(flag_setting_child, CHILD_PRIORITY, NULL);
  UW changed = spin_holding_registers(s_state);
  test_join_expect(1);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, changed,
                                 "registers changed across a switch");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      s_state[STATE_COUNT_A], s_state[STATE_COUNT_B],
      "an instruction ran twice or not at all after a resume");
}

static const test_case_t s_cases[] = {
    {"timer_wake_switches_inside_the_timer_trap",
     timer_wake_switches_inside_the_timer_trap},
    {"task_dispatch_still_switches_in_the_software_interrupt",
     task_dispatch_still_switches_in_the_software_interrupt},
    {"a_task_preempted_by_the_timer_resumes_intact",
     a_task_preempted_by_the_timer_resumes_intact},
};

TEST_SUITE_MAIN("timer-switch", s_cases, 5000)
