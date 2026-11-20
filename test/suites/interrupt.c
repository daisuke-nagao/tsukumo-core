/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// External interrupts through the PLIC: tk_def_int(), EnableInt() and
// DisableInt(), and the trap that calls the handler. The source is the
// transmitter-empty interrupt of the UART: it is raised by software (set the
// bit in the interrupt enable register) and cleared by software (clear it
// again), so no device has to be driven from outside.
// ClearInt cases acknowledge the source device first (UART IER=0, or the RTC's
// IRQ-enable/alarm/interrupt clear registers) so a still-asserted level cannot
// immediately become pending again after the PLIC claim is completed.
// QEMU virt keeps the PLIC pending bit latched after either device line falls;
// the simultaneous-source case checks the RTC request survives the UART claim.
//
// The handler of every case turns the UART interrupt off first, which is what
// a driver does to acknowledge it. Without that the source would stay asserted
// and the PLIC would raise it again as soon as the handler completes it.
//
// The cases run in one boot and share the UART interrupt, so each ends with
// clean_up() before its asserts: a failed assert ends the case task and would
// otherwise leave the source armed for the next case.
//
// schedule() is wrapped at link time to keep the mcause of the trap it runs in,
// as in timer_switch.c: a task that an interrupt handler wakes must be switched
// to at the exit of that same trap, not in a software interrupt that the
// handler only requests.

#include <tk/tkernel.h>
#include <unity.h>

#include "../finish.h"
#include "../putstring.h"
#include "asm/rv32/address.h"
#include "intctl.h"
#include "panic.h"
#include "qemu_virt.h"
#include "runner.h"
#include "spin_with_registers.h"
#include "task.h"
#include <tk/asm/rv32/interrupt.h>

extern void **__real_schedule(void *sp);
extern volatile UINT tkmc_isr_depth;
// The stack of the C parts of the trap entries (linker.ld).
extern unsigned char _isr_stack_start[];
extern unsigned char _isr_stack_end[];

enum {
  // The UART raises the interrupt only after the character in flight has left,
  // which takes a few ticks of the guest clock; the case polls for this long.
  WAIT_MS = 400,
  POLL_TICKS = 1, // tk_dly_tsk(1) sleeps two ticks (see test/runner.c).
  CHILD_PRIORITY = 2,
  // The lowest PLIC priority, for a case that needs the source raised but not
  // a particular level.
  LOWEST_LEVEL = 1,
  // Where the trap entry saves s0 and s2 in its frame, in words
  // (SAVE_FRAME in kernel/asm/rv32/start.S).
  FRAME_S0 = 4,
  FRAME_S2 = 14,
};
_Static_assert(CHILD_PRIORITY < TEST_CASE_PRIORITY,
               "the child must wait on the semaphore before the case goes on");

static const UW MSTATUS_MIE_BIT = 1u << 3;

static volatile UW s_last_mcause;

// Where the C part of every trap ran, for each call of schedule() (by trap) and
// of tkmc_timer_handler(): on the interrupt stack (the stack pointer is in it,
// the frame handed to schedule() is not) and inside a trap of depth 1. A flag
// stays false once one call is wrong, so that a later good call does not hide
// it.
enum { TRAP_EXTERNAL, TRAP_TIMER, TRAP_SOFTWARE, TRAPS };
static volatile UINT s_schedule_calls[TRAPS];
static volatile BOOL s_schedule_ok[TRAPS] = {TRUE, TRUE, TRUE};
static volatile UINT s_timer_handler_calls;
static volatile BOOL s_timer_handler_ok = TRUE;

static BOOL on_the_interrupt_stack(UW sp) {
  return sp > (UW)_isr_stack_start && sp <= (UW)_isr_stack_end;
}

static void reset_trap_records(void) {
  for (UINT trap = 0; trap < TRAPS; ++trap) {
    s_schedule_calls[trap] = 0;
    s_schedule_ok[trap] = TRUE;
  }
  s_timer_handler_calls = 0;
  s_timer_handler_ok = TRUE;
}

void **__wrap_schedule(void *frame) {
  UW cause, sp;
  __asm__ volatile("csrr %0, mcause" : "=r"(cause));
  __asm__ volatile("mv %0, sp" : "=r"(sp));
  if (s_last_mcause == 0) {
    s_last_mcause = cause; // The first trap since a handler cleared it.
  }
  UINT trap = cause == MCAUSE_EXTERNAL   ? TRAP_EXTERNAL
              : cause == MCAUSE_TIMER    ? TRAP_TIMER
              : cause == MCAUSE_SOFTWARE ? TRAP_SOFTWARE
                                         : TRAPS;
  if (trap < TRAPS) {
    s_schedule_calls[trap] += 1;
    // The closed interval: with the stack pointer passed instead of the frame,
    // the argument is _isr_stack_end when the call is the first of the trap.
    BOOL frame_on_the_stack =
        (UW)frame >= (UW)_isr_stack_start && (UW)frame <= (UW)_isr_stack_end;
    if (!on_the_interrupt_stack(sp) || frame_on_the_stack ||
        tkmc_isr_depth != 1) {
      s_schedule_ok[trap] = FALSE;
    }
  }
  return __real_schedule(frame);
}

extern void __real_tkmc_timer_handler(void);

void __wrap_tkmc_timer_handler(void) {
  UW sp;
  __asm__ volatile("mv %0, sp" : "=r"(sp));
  s_timer_handler_calls += 1;
  if (!on_the_interrupt_stack(sp) || tkmc_isr_depth != 1) {
    s_timer_handler_ok = FALSE;
  }
  __real_tkmc_timer_handler();
}

// The state of mstatus.MIE on entry to and exit from the kernel's completion of
// an external interrupt, counted only while a case has armed the record. Only
// the call from the trap entry (start.S) is wrapped; the call that
// tkmc_ret_int_from_sp makes in its own object file is not, and that assembly
// path is out of scope here.
extern void __real_tkmc_finish_external_interrupt(tkmc_interrupt_context *ctx);

static volatile BOOL s_finish_armed;
static volatile UINT s_finish_calls;
static volatile UINT s_finish_entries_with_mie;
static volatile UINT s_finish_exits_with_mie;

static BOOL mstatus_mie_is_on(void) {
  UW mstatus;
  __asm__ volatile("csrr %0, mstatus" : "=r"(mstatus) : : "memory");
  return (mstatus & MSTATUS_MIE_BIT) != 0;
}

void __wrap_tkmc_finish_external_interrupt(tkmc_interrupt_context *ctx) {
  if (!s_finish_armed) {
    __real_tkmc_finish_external_interrupt(ctx);
    return;
  }
  s_finish_calls += 1;
  if (mstatus_mie_is_on()) {
    s_finish_entries_with_mie += 1;
  }
  __real_tkmc_finish_external_interrupt(ctx);
  if (mstatus_mie_is_on()) {
    s_finish_exits_with_mie += 1;
  }
}

static void arm_finish_record(void) {
  s_finish_calls = 0;
  s_finish_entries_with_mie = 0;
  s_finish_exits_with_mie = 0;
  s_finish_armed = TRUE;
}

#define PLIC_PRIORITY_REG(src)                                                 \
  (PLIC_BASE_ADDRESS + PLIC_PRIORITY_OFFSET + 4 * (src))
#define PLIC_ENABLE_REG(src)                                                   \
  (PLIC_BASE_ADDRESS + PLIC_ENABLE_OFFSET + 4 * ((src) / 32))
#define PLIC_PENDING_REG(src)                                                  \
  (PLIC_BASE_ADDRESS + PLIC_PENDING_OFFSET + 4 * ((src) / 32))

static BOOL source_is_enabled(UINT src) {
  return (in_w(PLIC_ENABLE_REG(src)) >> (src % 32)) & 1u;
}

static BOOL source_is_pending(UINT src) {
  return (in_w(PLIC_PENDING_REG(src)) >> (src % 32)) & 1u;
}

static volatile UINT s_hits;
static volatile UINT s_last_intno;
static volatile BOOL s_handler_pending;
static volatile UINT s_endint_hlng_hits;
static volatile UINT s_endint_hlng_calls;
static volatile BOOL s_endint_hlng_retrigger;
static UW read_mie(void);

static void uart_handler(UINT intno) {
  s_handler_pending = CheckInt(intno);
  out_b(UART_IER, 0);
  s_last_intno = intno;
  s_hits += 1;
}

static volatile UINT s_second_hits;

static void second_handler(UINT intno) {
  s_handler_pending = CheckInt(intno);
  out_b(UART_IER, 0);
  s_last_intno = intno;
  s_second_hits += 1;
}

static void endint_hlng_handler(UINT intno) {
  ++s_endint_hlng_hits;
  if (s_endint_hlng_retrigger) {
    if (s_endint_hlng_hits == 1) {
      EndOfInt(intno);
      ++s_endint_hlng_calls;
      out_b(UART_IER, 0);
      out_b(UART_IER, IER_THRE);
    } else {
      out_b(UART_IER, 0);
    }
    return;
  } else {
    DisableInt(intno);
    EndOfInt(intno);
    ++s_endint_hlng_calls;
    EndOfInt(intno);
    ++s_endint_hlng_calls;
  }
  out_b(UART_IER, 0);
  s_last_intno = intno;
}

// Polls, in task context with the idle task running in between, until the
// condition holds or WAIT_MS has passed.
#define WAIT_UNTIL(cond) TEST_WAIT_UNTIL(cond, WAIT_MS)

// Puts the interrupt back as the next case expects it: nothing raised, nothing
// enabled, no handler. The order matters: the source goes quiet first.
static void clean_up(void) {
  out_b(UART_IER, 0);
  DisableInt(UART_IRQ);
  tk_def_int(UART_IRQ, NULL);
}

static void reset_counters(void) {
  s_hits = 0;
  s_second_hits = 0;
  s_last_intno = 0;
}

static const T_DINT s_uart_dint = {.intatr = TA_HLNG, .inthdr = uart_handler};
static const T_DINT s_second_dint = {.intatr = TA_HLNG,
                                     .inthdr = second_handler};

static void handler_runs_with_its_interrupt_number(void) {
  reset_counters();
  s_handler_pending = TRUE;
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &s_uart_dint));
  EnableInt(UART_IRQ, PLIC_MAX_PRIORITY);
  UW priority = in_w(PLIC_PRIORITY_REG(UART_IRQ));
  out_b(UART_IER, IER_THRE);
  BOOL fired = WAIT_UNTIL(s_hits >= 1);
  // A second interrupt is served only if the first was completed.
  out_b(UART_IER, IER_THRE);
  BOOL fired_again = WAIT_UNTIL(s_hits >= 2);
  clean_up();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(PLIC_MAX_PRIORITY, priority,
                                   "EnableInt() did not set the priority");
  TEST_ASSERT_TRUE_MESSAGE(fired, "the handler did not run");
  TEST_ASSERT_TRUE_MESSAGE(fired_again, "the interrupt was not completed");
  TEST_ASSERT_EQUAL_UINT(2, s_hits);
  TEST_ASSERT_EQUAL_UINT(UART_IRQ, s_last_intno);
  TEST_ASSERT_FALSE_MESSAGE(s_handler_pending,
                            "a claimed source was still pending");
}

static void end_of_int_is_idempotent_for_hlng_with_disabled_source(void) {
  s_endint_hlng_hits = 0;
  s_endint_hlng_calls = 0;
  s_endint_hlng_retrigger = FALSE;
  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = endint_hlng_handler};
  ER defined = tk_def_int(UART_IRQ, &dint);
  EnableInt(UART_IRQ, 3);
  out_b(UART_IER, IER_THRE);
  BOOL fired = WAIT_UNTIL(s_endint_hlng_hits == 1);
  BOOL disabled_after_end = !source_is_enabled(UART_IRQ);
  UINT depth_after_end = tkmc_isr_depth;
  UINT threshold_after_end = in_w(PLIC_BASE_ADDRESS + PLIC_THRESHOLD_OFFSET);
  UW mie_after_end = read_mie();
  clean_up();
  s_endint_hlng_retrigger = FALSE;

  TEST_ASSERT_EQUAL(E_OK, defined);
  TEST_ASSERT_TRUE_MESSAGE(fired,
                           "the explicit-completion handler did not run");
  TEST_ASSERT_EQUAL_UINT(2, s_endint_hlng_calls);
  TEST_ASSERT_TRUE(disabled_after_end);
  TEST_ASSERT_EQUAL_UINT(0, depth_after_end);
  TEST_ASSERT_EQUAL_UINT(0, threshold_after_end);
  TEST_ASSERT_EQUAL_HEX32((1u << 7) | (1u << 3),
                          mie_after_end & ((1u << 7) | (1u << 3)));
}

static void end_of_int_lets_still_asserted_source_be_raised_again(void) {
  s_endint_hlng_hits = 0;
  s_endint_hlng_calls = 0;
  s_endint_hlng_retrigger = TRUE;
  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = endint_hlng_handler};
  ER defined = tk_def_int(UART_IRQ, &dint);
  EnableInt(UART_IRQ, 3);
  out_b(UART_IER, IER_THRE);
  BOOL fired_twice = WAIT_UNTIL(s_endint_hlng_hits >= 2);
  UINT hits = s_endint_hlng_hits;
  UINT depth_after_end = tkmc_isr_depth;
  clean_up();
  s_endint_hlng_retrigger = FALSE;

  TEST_ASSERT_EQUAL(E_OK, defined);
  TEST_ASSERT_TRUE_MESSAGE(fired_twice,
                           "completion did not let the asserted source retry");
  TEST_ASSERT_EQUAL_UINT(2, hits);
  TEST_ASSERT_EQUAL_UINT(1, s_endint_hlng_calls);
  TEST_ASSERT_EQUAL_UINT(0, depth_after_end);
}

static void
check_int_observes_disabled_pending_source_without_consuming_it(void) {
  reset_counters();
  s_handler_pending = TRUE;
  DisableInt(UART_IRQ);
  ER defined = tk_def_int(UART_IRQ, &s_uart_dint);
  out_b(UART_IER, IER_THRE);
  BOOL raised = WAIT_UNTIL(source_is_pending(UART_IRQ));
  BOOL pending = CheckInt(UART_IRQ);
  BOOL pending_again = CheckInt(UART_IRQ);
  UW mstatus_before;
  __asm__ volatile("csrr %0, mstatus" : "=r"(mstatus_before));
  __asm__ volatile("csrc mstatus, %0" : : "r"(MSTATUS_MIE_BIT) : "memory");
  BOOL pending_with_mie_off = CheckInt(UART_IRQ);
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  BOOL enabled_pending_with_mie_off = CheckInt(UART_IRQ);
  UW mstatus_after_check;
  __asm__ volatile("csrr %0, mstatus" : "=r"(mstatus_after_check));
  __asm__ volatile("csrs mstatus, %0" : : "r"(MSTATUS_MIE_BIT) : "memory");
  BOOL fired = WAIT_UNTIL(s_hits == 1);
  if ((mstatus_before & MSTATUS_MIE_BIT) == 0) {
    __asm__ volatile("csrc mstatus, %0" : : "r"(MSTATUS_MIE_BIT) : "memory");
  }
  clean_up();

  TEST_ASSERT_EQUAL(E_OK, defined);
  TEST_ASSERT_TRUE_MESSAGE(raised, "the disabled UART never became pending");
  TEST_ASSERT_TRUE(pending);
  TEST_ASSERT_TRUE(pending_again);
  TEST_ASSERT_TRUE(pending_with_mie_off);
  TEST_ASSERT_TRUE(enabled_pending_with_mie_off);
  TEST_ASSERT_EQUAL_HEX32(0, mstatus_after_check & MSTATUS_MIE_BIT);
  TEST_ASSERT_TRUE_MESSAGE(fired, "the enabled pending source was not served");
  TEST_ASSERT_EQUAL_UINT(1, s_hits);
  TEST_ASSERT_FALSE_MESSAGE(s_handler_pending,
                            "a claimed source was still pending");
}

// What the handler found when it ran, and what the child saw when it woke.
static volatile UINT s_isr_flag;
static volatile UW s_handler_mstatus;
static volatile UW s_child_cause;
static volatile UW s_handler_sp;
static ID s_sem;

static void waking_handler(UINT intno) {
  out_b(UART_IER, 0);
  UW mstatus;
  __asm__ volatile("csrr %0, mstatus" : "=r"(mstatus));
  s_handler_mstatus = mstatus;
  UW sp;
  __asm__ volatile("mv %0, sp" : "=r"(sp));
  s_handler_sp = sp;
  s_isr_flag = tkmc_isr_depth;
  s_last_intno = intno;
  s_last_mcause = 0;
  s_hits += 1;
  tk_sig_sem(s_sem, 1);
}

static void sleeping_child(void *arg) {
  (void)arg;
  TEST_ASSERT_EQUAL(E_OK, tk_wai_sem(s_sem, 1, TMO_FEVR));
  s_child_cause = s_last_mcause;
  test_checkpoint();
}

static void handler_runs_as_isr_and_its_wakeup_switches_at_trap_exit(void) {
  reset_counters();
  s_isr_flag = 0;
  s_handler_mstatus = MSTATUS_MIE_BIT;
  s_handler_sp = 0;
  s_child_cause = 0;
  s_sem = tk_cre_sem(
      &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
  TEST_ASSERT_GREATER_THAN(E_OK, s_sem);
  test_spawn(sleeping_child, CHILD_PRIORITY, NULL);

  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = waking_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &dint));
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  out_b(UART_IER, IER_THRE);
  BOOL fired = WAIT_UNTIL(s_hits >= 1);
  clean_up();
  ER deleted = tk_del_sem(s_sem);
  test_join_expect(1);
  TEST_ASSERT_EQUAL(E_OK, deleted);
  TEST_ASSERT_TRUE_MESSAGE(fired, "the handler did not run");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, s_isr_flag,
                                 "the handler did not run as an ISR");
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, s_handler_mstatus & MSTATUS_MIE_BIT,
                                  "interrupts were on in the handler");
  TEST_ASSERT_TRUE_MESSAGE(s_handler_sp > (UW)_isr_stack_start &&
                               s_handler_sp <= (UW)_isr_stack_end,
                           "the handler did not run on the interrupt stack");
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, s_handler_sp % 16,
                                  "the interrupt stack is not aligned");
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(
      MCAUSE_EXTERNAL, s_child_cause,
      MCAUSE_SOFTWARE == s_child_cause
          ? "the task was switched to in a software interrupt"
          : "the task was switched to in another trap");
}

static void enable_int_delivers_request_that_waited_while_disabled(void) {
  reset_counters();
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &s_uart_dint));
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  DisableInt(UART_IRQ);
  out_b(UART_IER, IER_THRE);
  // The source is pending but must not be served. A wait that ended only
  // because the UART had not raised it yet would pass for the wrong reason.
  BOOL pending = WAIT_UNTIL(source_is_pending(UART_IRQ));
  tk_dly_tsk(POLL_TICKS);
  UINT hits_while_disabled = s_hits;
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  BOOL fired = WAIT_UNTIL(s_hits >= 1);
  clean_up();
  TEST_ASSERT_TRUE_MESSAGE(pending, "the UART never raised the interrupt");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, hits_while_disabled,
                                 "a disabled source was served");
  TEST_ASSERT_TRUE_MESSAGE(fired, "the enabled source was not served");
}

static void tk_def_int_replaces_old_definition(void) {
  reset_counters();
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &s_uart_dint));
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &s_second_dint));
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  out_b(UART_IER, IER_THRE);
  BOOL fired = WAIT_UNTIL(s_second_hits >= 1);
  clean_up();
  TEST_ASSERT_TRUE_MESSAGE(fired, "the new handler did not run");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, s_hits, "the old handler ran");
}

static void tk_def_int_serves_source_that_waited_without_handler(void) {
  reset_counters();
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &s_uart_dint));
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, NULL));
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  BOOL was_enabled = source_is_enabled(UART_IRQ);
  out_b(UART_IER, IER_THRE);
  BOOL switched_off = WAIT_UNTIL(!source_is_enabled(UART_IRQ));
  // The default handler completed the source, so it can be served again.
  out_b(UART_IER, 0);
  UINT hits_before = s_hits;
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &s_uart_dint));
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  out_b(UART_IER, IER_THRE);
  BOOL fired = WAIT_UNTIL(s_hits >= 1);
  clean_up();
  TEST_ASSERT_TRUE_MESSAGE(was_enabled,
                           "EnableInt() did not enable the source");
  TEST_ASSERT_TRUE_MESSAGE(switched_off,
                           "the source is still enabled: the default handler "
                           "did not switch it off");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, hits_before, "a cancelled handler ran");
  TEST_ASSERT_TRUE_MESSAGE(fired, "the source was not served once defined");
}

static void disabling_handler(UINT intno) {
  out_b(UART_IER, 0);
  DisableInt(intno);
  s_last_intno = intno;
  s_hits += 1;
}

// A driver masks its source until the deferred work is done. The kernel still
// completes the source, and it is served again once it is enabled.
static void enable_int_serves_again_source_that_handler_switched_off(void) {
  reset_counters();
  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = disabling_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &dint));
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  out_b(UART_IER, IER_THRE);
  BOOL fired = WAIT_UNTIL(s_hits >= 1);
  BOOL stayed_off = !source_is_enabled(UART_IRQ);
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  out_b(UART_IER, IER_THRE);
  BOOL fired_again = WAIT_UNTIL(s_hits >= 2);
  clean_up();
  TEST_ASSERT_TRUE_MESSAGE(fired, "the handler did not run");
  TEST_ASSERT_TRUE_MESSAGE(stayed_off, "the handler's DisableInt() was undone");
  TEST_ASSERT_TRUE_MESSAGE(fired_again,
                           "the source was not served after it was enabled");
}

static volatile ER s_handler_er;

static void defining_handler(UINT intno) {
  (void)intno;
  out_b(UART_IER, 0);
  s_handler_er = tk_def_int(UART_IRQ, &s_uart_dint);
  s_hits += 1;
}

// The refused calls also leave the definition made before them in place.
static void tk_def_int_refuses_bad_definitions_and_keeps_old_one(void) {
  reset_counters();
  T_DINT no_handler = {.intatr = TA_HLNG, .inthdr = NULL};
  T_DINT unknown_bit = {.intatr = TA_HLNG | 0x2, .inthdr = second_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &s_uart_dint));
  ER er[] = {
      tk_def_int(0, &s_second_dint),
      tk_def_int(CFN_MAX_INTNO, &s_second_dint),
      tk_def_int(0xffffffffu, &s_second_dint),
      tk_def_int(0, NULL),
      tk_def_int(0xffffffffu, NULL),
      tk_def_int(UART_IRQ, &no_handler),
      tk_def_int(UART_IRQ, &unknown_bit),
  };
  static const ER expected[] = {E_PAR, E_PAR, E_PAR,  E_PAR,
                                E_PAR, E_PAR, E_RSATR};
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  out_b(UART_IER, IER_THRE);
  BOOL fired = WAIT_UNTIL(s_hits >= 1);
  clean_up();
  for (UINT i = 0; i < sizeof(er) / sizeof(er[0]); ++i) {
    TEST_ASSERT_EQUAL_MESSAGE(expected[i], er[i],
                              "a bad definition was not refused");
  }
  TEST_ASSERT_TRUE_MESSAGE(fired, "a refused call removed the old handler");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, s_second_hits, "a refused handler ran");
}

static void tk_def_int_refuses_calls_from_isr_or_with_interrupts_off(void) {
  reset_counters();
  s_handler_er = E_OK;
  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = defining_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &dint));
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  out_b(UART_IER, IER_THRE);
  BOOL fired = WAIT_UNTIL(s_hits >= 1);
  clean_up();
  TEST_ASSERT_TRUE_MESSAGE(fired, "the handler did not run");
  TEST_ASSERT_EQUAL_MESSAGE(E_CTX, s_handler_er,
                            "tk_def_int() worked in an ISR");

  UINT intsts;
  DI(intsts);
  ER with_interrupts_off = tk_def_int(UART_IRQ, &s_uart_dint);
  EI(intsts);
  tk_def_int(UART_IRQ, NULL);
  TEST_ASSERT_EQUAL_MESSAGE(E_CTX, with_interrupts_off,
                            "tk_def_int() worked with interrupts off");
}

// EnableInt() has no error code, and the spec leaves a bad number open. It must
// not write registers it does not own: the PLIC has more sources than the
// handler table, and a priority of 0 or above 7 is no priority.
static void enable_int_with_bad_number_or_level_changes_nothing(void) {
  const UW priority_before = 3;
  out_w(PLIC_PRIORITY_REG(UART_IRQ), priority_before);
  // Source 32 has priority 0 and cannot fire; only its enable bit is watched.
  out_w(PLIC_ENABLE_REG(CFN_MAX_INTNO), 1);
  EnableInt(0, LOWEST_LEVEL);
  EnableInt(CFN_MAX_INTNO, LOWEST_LEVEL);
  EnableInt(CFN_MAX_INTNO + 1, LOWEST_LEVEL);
  EnableInt(0xffffffffu, LOWEST_LEVEL);
  EnableInt(UART_IRQ, 0);
  EnableInt(UART_IRQ, PLIC_MAX_PRIORITY + 1);
  EnableInt(UART_IRQ, -1);
  DisableInt(0);
  DisableInt(CFN_MAX_INTNO);
  DisableInt(0xffffffffu);
  ClearInt(0);
  ClearInt(CFN_MAX_INTNO);
  ClearInt(0xffffffffu);
  UW priority = in_w(PLIC_PRIORITY_REG(UART_IRQ));
  BOOL enabled = source_is_enabled(UART_IRQ);
  UW priority_32 = in_w(PLIC_PRIORITY_REG(CFN_MAX_INTNO));
  UW priority_0 = in_w(PLIC_PRIORITY_REG(0));
  UW enable_32 = in_w(PLIC_ENABLE_REG(CFN_MAX_INTNO));
  out_w(PLIC_PRIORITY_REG(UART_IRQ), 0);
  out_w(PLIC_ENABLE_REG(CFN_MAX_INTNO), 0);
  TEST_ASSERT_EQUAL_UINT32(priority_before, priority);
  TEST_ASSERT_FALSE(enabled);
  TEST_ASSERT_EQUAL_UINT32(0, priority_32);
  TEST_ASSERT_EQUAL_UINT32(0, priority_0);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      1, enable_32, "DisableInt() cleared a source it does not own");
}

// A handler that does nothing but raise a flag, for the cases that watch what
// an interrupt does to the task it interrupts.
static volatile UW s_flag;

static void flag_handler(UINT intno) {
  out_b(UART_IER, 0);
  s_last_intno = intno;
  s_flag = 1;
}

static void waking_flag_handler(UINT intno) {
  flag_handler(intno);
  tk_sig_sem(s_sem, 1);
}

// Spins in the stub of test/spin_with_registers.h, which holds a pattern in the
// registers that a trap entry could change, through one UART interrupt that it
// arms itself once the patterns are loaded, and returns what the stub returns.
// With a_task_is_woken the handler wakes a task that outranks this one, so the
// trap ends in a task switch and this task is resumed from its frame later.
static UW registers_after_an_interrupt(FP handler, BOOL a_task_is_woken) {
  s_flag = 0;
  if (a_task_is_woken) {
    s_sem = tk_cre_sem(
        &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
    TEST_ASSERT_GREATER_THAN(E_OK, s_sem);
    test_spawn(sleeping_child, CHILD_PRIORITY, NULL);
  }
  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &dint));
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  reset_trap_records();
  UW changed = test_spin_with_registers(
      &s_flag, test_mtime() + WAIT_MS * TEST_MTIME_PER_MS,
      (volatile UB *)UART_IER, IER_THRE);
  clean_up();
  if (a_task_is_woken) {
    BOOL switched = s_schedule_calls[TRAP_EXTERNAL] >= 1;
    ER deleted = tk_del_sem(s_sem);
    test_join_expect(1);
    // The trap that hit the stub ended in a task switch, not in a return:
    // the external entry calls schedule() only when the handler raised MSIP.
    TEST_ASSERT_TRUE_MESSAGE(switched, "the external trap did not switch");
    TEST_ASSERT_EQUAL(E_OK, deleted);
  }
  return changed;
}

static void interrupt_keeps_task_registers(void) {
  UW changed = registers_after_an_interrupt((FP)flag_handler, FALSE);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, changed,
                                  "a register changed (bit 31: no interrupt)");
}

// The child that the handler wakes ends after it has run, and its end resumes
// the case from the frame of the external trap (__launch_task): the second
// check of that path besides timer_switch.c. Keep it so.
static void trap_exit_switch_keeps_task_registers(void) {
  UW changed = registers_after_an_interrupt((FP)waking_flag_handler, TRUE);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, changed,
                                  "a register changed (bit 31: no interrupt)");
}

static volatile UINT s_depth_in_handler;

static void depth_handler(UINT intno) {
  out_b(UART_IER, 0);
  s_last_intno = intno;
  s_depth_in_handler = tkmc_isr_depth;
  s_hits += 1;
}

// The trap adds one to the depth it finds and takes it off again, whatever the
// value was: a trap that wrote 1 and then 0 would pass the cases that start
// from 0. Only the external source is on and the task calls no kernel
// function while the depth is not 0, so nothing else enters the kernel.
static void isr_depth_rises_by_one_per_trap_and_drops_back(void) {
  enum { DEPTH = 5 };
  reset_counters();
  s_depth_in_handler = 0;
  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = depth_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &dint));
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  UW saved_mie;
  __asm__ volatile("csrrw %0, mie, %1"
                   : "=r"(saved_mie)
                   : "r"(1u << 11)
                   : "memory");
  tkmc_isr_depth = DEPTH;
  out_b(UART_IER, IER_THRE);
  UW start = test_mtime();
  while (s_hits == 0 &&
         (UW)(test_mtime() - start) < WAIT_MS * TEST_MTIME_PER_MS) {
  }
  UINT after = tkmc_isr_depth;
  tkmc_isr_depth = 0;
  __asm__ volatile("csrw mie, %0" ::"r"(saved_mie) : "memory");
  clean_up();
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, s_hits, "the handler did not run");
  TEST_ASSERT_EQUAL_UINT(DEPTH + 1, s_depth_in_handler);
  TEST_ASSERT_EQUAL_UINT(DEPTH, after);
}

// The deepest paths that the cases of this suite can start: a handler that
// sends a message to a task that outranks the case, the end of a timed wait
// and a task switch of the software interrupt. The stack is painted first, and
// the words that are still painted afterwards were never used.
enum {
  PAINT = 0x5a5a5a5a,
  HIGH_WATER_MIN = 8,
  HIGH_WATER_MAX = 768,
  NESTED_HIGH_WATER_MIN = 160,
  NESTED_HIGH_WATER_MAX = 1024
};

static void paint_the_interrupt_stack(void) {
  for (volatile UW *word = (volatile UW *)_isr_stack_start;
       word < (volatile UW *)_isr_stack_end; ++word) {
    *word = PAINT;
  }
}

static UINT interrupt_stack_high_water(void) {
  const volatile UW *word = (const volatile UW *)_isr_stack_start;
  while (word < (const volatile UW *)_isr_stack_end && *word == PAINT) {
    ++word;
  }
  return (UINT)((UW)_isr_stack_end - (UW)word);
}

static ID s_mbx;
static T_MSG s_message;
static volatile BOOL s_timed_waiter_done;

static void mailbox_handler(UINT intno) {
  out_b(UART_IER, 0);
  s_last_intno = intno;
  s_hits += 1;
  tk_snd_mbx(s_mbx, &s_message);
}

static void mailbox_receiver(void *arg) {
  (void)arg;
  T_MSG *received;
  TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(s_mbx, &received, TMO_FEVR));
  test_checkpoint();
}

static void timed_waiter(void *arg) {
  (void)arg;
  ER result = tk_wai_sem(s_sem, 1, 30);
  s_timed_waiter_done = TRUE;
  TEST_ASSERT_EQUAL(E_TMOUT, result);
  test_checkpoint();
}

static void interrupt_stack_has_room_for_deepest_paths(void) {
  reset_counters();
  s_mbx = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TFIFO});
  s_sem = tk_cre_sem(
      &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
  TEST_ASSERT_GREATER_THAN(E_OK, s_mbx);
  TEST_ASSERT_GREATER_THAN(E_OK, s_sem);
  paint_the_interrupt_stack();
  s_timed_waiter_done = FALSE;
  test_spawn(mailbox_receiver, CHILD_PRIORITY, NULL);
  test_spawn(timed_waiter, CHILD_PRIORITY, NULL);
  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = mailbox_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &dint));
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  out_b(UART_IER, IER_THRE);
  BOOL fired = WAIT_UNTIL(s_hits >= 1);
  clean_up();
  // Let the timed child exercise the timer path before recovery deletes its
  // semaphore. The mailbox child must be freed even when notification failed.
  BOOL timed_done = WAIT_UNTIL(s_timed_waiter_done);
  UINT used = interrupt_stack_high_water();
  ER mailbox_deleted = tk_del_mbx(s_mbx);
  ER semaphore_deleted = tk_del_sem(s_sem);
  test_join_expect(2);
  TEST_ASSERT_EQUAL(E_OK, mailbox_deleted);
  TEST_ASSERT_EQUAL(E_OK, semaphore_deleted);
  TEST_ASSERT_TRUE_MESSAGE(fired, "the handler did not run");
  TEST_ASSERT_TRUE_MESSAGE(timed_done, "the timed waiter did not finish");
  TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(
      HIGH_WATER_MIN, used, "the painted stack was not used: nothing measured");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(HIGH_WATER_MAX, used,
                                         "the interrupt stack is nearly full");
}

static BOOL every_trap_has_run_schedule(void) {
  return s_schedule_calls[TRAP_EXTERNAL] >= 1 &&
         s_schedule_calls[TRAP_TIMER] >= 1 &&
         s_schedule_calls[TRAP_SOFTWARE] >= 1;
}

// The external trap is made to switch tasks by a handler that wakes one; the
// timer trap by the wake-up of the polling case, and the software interrupt by
// the child that waits for the semaphore.
static void interrupt_stack_runs_schedule_in_every_trap(void) {
  reset_counters();
  reset_trap_records();
  s_sem = tk_cre_sem(
      &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
  TEST_ASSERT_GREATER_THAN(E_OK, s_sem);
  test_spawn(sleeping_child, CHILD_PRIORITY, NULL);
  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = waking_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &dint));
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  out_b(UART_IER, IER_THRE);
  BOOL seen = WAIT_UNTIL(every_trap_has_run_schedule());
  clean_up();
  UINT calls[TRAPS];
  BOOL ok[TRAPS];
  for (UINT trap = 0; trap < TRAPS; ++trap) {
    calls[trap] = s_schedule_calls[trap];
    ok[trap] = s_schedule_ok[trap];
  }
  ER deleted = tk_del_sem(s_sem);
  test_join_expect(1);
  TEST_ASSERT_EQUAL(E_OK, deleted);
  TEST_ASSERT_TRUE_MESSAGE(calls[TRAP_EXTERNAL] >= 1,
                           "no task switch in the external interrupt");
  TEST_ASSERT_TRUE_MESSAGE(calls[TRAP_TIMER] >= 1,
                           "no task switch in the timer interrupt");
  TEST_ASSERT_TRUE_MESSAGE(calls[TRAP_SOFTWARE] >= 1,
                           "no task switch in the software interrupt");
  TEST_ASSERT_TRUE(seen);
  TEST_ASSERT_TRUE_MESSAGE(ok[TRAP_EXTERNAL],
                           "schedule() in the external interrupt: wrong stack "
                           "or depth");
  TEST_ASSERT_TRUE_MESSAGE(ok[TRAP_TIMER],
                           "schedule() in the timer interrupt: wrong stack or "
                           "depth");
  TEST_ASSERT_TRUE_MESSAGE(ok[TRAP_SOFTWARE],
                           "schedule() in the software interrupt: wrong stack "
                           "or depth");
}

// Most timer interrupts switch no task, so schedule() says nothing about them.
static void interrupt_stack_runs_timer_handler(void) {
  reset_trap_records();
  BOOL seen = WAIT_UNTIL(s_timer_handler_calls >= 3);
  TEST_ASSERT_TRUE_MESSAGE(seen, "no timer interrupt");
  TEST_ASSERT_TRUE_MESSAGE(s_timer_handler_ok,
                           "the timer handler: wrong stack or depth");
}

// ---- A second source, and ClearInt -----------------------------------------
//
// The RTC is the second external source. Like the UART, it is raised by
// software: an alarm at a time that has already passed is due as soon as it is
// enabled, which does not depend on the clock the machine runs on (an alarm in
// the future would, because the RTC counts the host's time).

enum {
  OUTER_LEVEL = 2,
  HIGHER_LEVEL = 5,
  MIE_TIMER_AND_SOFTWARE = (1u << 7) | (1u << 3),
  MIE_EXTERNAL = 1u << 11,
  MIP_TIMER = 1u << 7
};

#define PLIC_THRESHOLD_REG (PLIC_BASE_ADDRESS + PLIC_THRESHOLD_OFFSET)

static void rtc_raise(void) {
  UW low = in_w(RTC_TIME_LOW);
  UW high = in_w(RTC_TIME_HIGH);
  out_w(RTC_ALARM_HIGH, high);
  out_w(RTC_ALARM_LOW, low);
  out_w(RTC_IRQ_ENABLED, 1);
}

static void rtc_quiet(void) {
  out_w(RTC_IRQ_ENABLED, 0);
  out_w(RTC_CLEAR_ALARM, 1);
  out_w(RTC_CLEAR_INTERRUPT, 1);
}

static UW read_mstatus(void) {
  UW mstatus;
  __asm__ volatile("csrr %0, mstatus" : "=r"(mstatus));
  return mstatus;
}

static void set_mstatus_mie(BOOL enabled) {
  if (enabled) {
    __asm__ volatile("csrs mstatus, %0" : : "r"(MSTATUS_MIE_BIT) : "memory");
  } else {
    __asm__ volatile("csrc mstatus, %0" : : "r"(MSTATUS_MIE_BIT) : "memory");
  }
}

// Busy-poll because these fixtures deliberately keep CPU interrupts off while
// hardware latches the PLIC request.
static BOOL wait_for_pending_without_switch(UINT intno) {
  UW start = test_mtime();
  while (!source_is_pending(intno) &&
         (UW)(test_mtime() - start) < WAIT_MS * TEST_MTIME_PER_MS) {
  }
  return source_is_pending(intno);
}

static void clear_int_cleanup(void) {
  out_b(UART_IER, 0);
  rtc_quiet();
  DisableInt(UART_IRQ);
  DisableInt(RTC_IRQ);
  if (source_is_pending(UART_IRQ)) {
    ClearInt(UART_IRQ);
  }
  if (source_is_pending(RTC_IRQ)) {
    ClearInt(RTC_IRQ);
  }
  tk_def_int(UART_IRQ, NULL);
  tk_def_int(RTC_IRQ, NULL);
}

static volatile UINT s_clear_uart_hits;
static volatile UINT s_clear_uart_depth_before, s_clear_uart_depth_after;
static volatile BOOL s_clear_uart_pending_before, s_clear_uart_pending_after;
static volatile UW s_clear_uart_mstatus_before, s_clear_uart_mstatus_after;
static volatile UINT s_clear_completed_hits, s_clear_completed_end_calls;
static volatile UINT s_clear_completed_last_intno, s_clear_completed_last_depth;
static volatile UINT s_clear_completed_depth_before,
    s_clear_completed_depth_after;
static volatile BOOL s_clear_completed_pending_before;
static volatile BOOL s_clear_completed_pending_after;
static volatile UW s_clear_completed_mstatus_before,
    s_clear_completed_mstatus_after;
static volatile BOOL s_clear_outer_from_inner;
static volatile UINT s_clear_outer_depth;
static volatile BOOL s_clear_outer_pending_before, s_clear_outer_pending_after;
static volatile UW s_clear_outer_threshold;

static void clear_int_uart_handler(UINT intno) {
  (void)intno;
  ++s_clear_uart_hits;
  s_clear_uart_depth_before = tkmc_isr_depth;
  s_clear_uart_pending_before = CheckInt(UART_IRQ);
  s_clear_uart_mstatus_before = read_mstatus();
  ClearInt(UART_IRQ);
  s_clear_uart_depth_after = tkmc_isr_depth;
  s_clear_uart_pending_after = CheckInt(UART_IRQ);
  s_clear_uart_mstatus_after = read_mstatus();
  out_b(UART_IER, 0);
}

static void clear_int_completed_uart_handler(UINT intno) {
  ++s_clear_completed_hits;
  s_clear_completed_last_intno = intno;
  s_clear_completed_last_depth = tkmc_isr_depth;
  if (s_clear_completed_hits != 1) {
    out_b(UART_IER, 0);
    return;
  }

  out_b(UART_IER, 0);
  EndOfInt(intno);
  ++s_clear_completed_end_calls;
  out_b(UART_IER, IER_THRE);
  s_clear_completed_pending_before = wait_for_pending_without_switch(intno);
  out_b(UART_IER, 0);
  s_clear_completed_depth_before = tkmc_isr_depth;
  s_clear_completed_mstatus_before = read_mstatus();
  ClearInt(intno);
  s_clear_completed_depth_after = tkmc_isr_depth;
  s_clear_completed_pending_after = source_is_pending(intno);
  s_clear_completed_mstatus_after = read_mstatus();
}

static void clear_int_rtc_handler(UINT intno) {
  s_handler_pending = CheckInt(intno);
  rtc_quiet();
  s_last_intno = intno;
  ++s_second_hits;
}

static const T_DINT s_clear_int_rtc_dint = {.intatr = TA_HLNG,
                                            .inthdr = clear_int_rtc_handler};

// What ClearInt(UART_IRQ) left. clear_the_pending_uart() makes the UART
// interrupt pending with MIE off (the caller turned it off), quiets the UART
// again, calls ClearInt() and samples the PLIC and mstatus at once.
typedef struct {
  BOOL pending_before;
  BOOL pending_after;
  BOOL enabled_after;
  UW priority_after;
  UW threshold_after;
  UW mstatus_after;
} uart_clear_t;

static uart_clear_t clear_the_pending_uart(void) {
  uart_clear_t c;
  out_b(UART_IER, IER_THRE);
  c.pending_before = wait_for_pending_without_switch(UART_IRQ);
  out_b(UART_IER, 0);
  ClearInt(UART_IRQ);
  c.pending_after = source_is_pending(UART_IRQ);
  c.enabled_after = source_is_enabled(UART_IRQ);
  c.priority_after = in_w(PLIC_PRIORITY_REG(UART_IRQ));
  c.threshold_after = in_w(PLIC_THRESHOLD_REG);
  c.mstatus_after = read_mstatus();
  return c;
}

static void clear_int_discards_disabled_pending_uart(void) {
  reset_counters();
  ER defined = tk_def_int(UART_IRQ, &s_uart_dint);
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  DisableInt(UART_IRQ);
  UW priority_before = in_w(PLIC_PRIORITY_REG(UART_IRQ));
  UW mstatus_before = read_mstatus();
  set_mstatus_mie(FALSE);
  const uart_clear_t c = clear_the_pending_uart();
  DisableInt(UART_IRQ);
  set_mstatus_mie((mstatus_before & MSTATUS_MIE_BIT) != 0);
  clear_int_cleanup();

  TEST_ASSERT_EQUAL(E_OK, defined);
  TEST_ASSERT_TRUE_MESSAGE(c.pending_before,
                           "the disabled UART never became pending");
  TEST_ASSERT_FALSE_MESSAGE(c.pending_after,
                            "ClearInt() left the UART pending");
  TEST_ASSERT_FALSE_MESSAGE(c.enabled_after,
                            "ClearInt() enabled the disabled UART");
  TEST_ASSERT_EQUAL_UINT32(priority_before, c.priority_after);
  TEST_ASSERT_EQUAL_HEX32(0, c.mstatus_after & MSTATUS_MIE_BIT);
  TEST_ASSERT_EQUAL_UINT(0, s_hits);
}

static void clear_int_discards_enabled_pending_uart(void) {
  reset_counters();
  ER defined = tk_def_int(UART_IRQ, &s_uart_dint);
  UW mstatus_before = read_mstatus();
  set_mstatus_mie(FALSE);
  EnableInt(UART_IRQ, 3);
  const uart_clear_t c = clear_the_pending_uart();
  set_mstatus_mie(TRUE);
  tk_dly_tsk(POLL_TICKS);
  UINT hits_after_restore = s_hits;
  clear_int_cleanup();
  set_mstatus_mie((mstatus_before & MSTATUS_MIE_BIT) != 0);

  TEST_ASSERT_EQUAL(E_OK, defined);
  TEST_ASSERT_TRUE_MESSAGE(c.pending_before,
                           "the enabled UART never became pending");
  TEST_ASSERT_FALSE_MESSAGE(c.pending_after,
                            "ClearInt() left the UART pending");
  TEST_ASSERT_TRUE_MESSAGE(c.enabled_after,
                           "ClearInt() disabled the enabled UART");
  TEST_ASSERT_EQUAL_UINT32(3, c.priority_after);
  TEST_ASSERT_EQUAL_HEX32(0, c.mstatus_after & MSTATUS_MIE_BIT);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, hits_after_restore,
                                 "ClearInt() let the UART handler run");
}

static void clear_int_claims_pending_source_below_threshold(void) {
  reset_counters();
  ER defined = tk_def_int(UART_IRQ, &s_uart_dint);
  UW mstatus_before = read_mstatus();
  UW threshold_before = in_w(PLIC_THRESHOLD_REG);
  set_mstatus_mie(FALSE);
  out_w(PLIC_THRESHOLD_REG, 0);
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  out_w(PLIC_THRESHOLD_REG, PLIC_MAX_PRIORITY);
  const uart_clear_t c = clear_the_pending_uart();
  UINT hits_before_restore = s_hits;
  DisableInt(UART_IRQ);
  out_w(PLIC_THRESHOLD_REG, threshold_before);
  set_mstatus_mie((mstatus_before & MSTATUS_MIE_BIT) != 0);
  clear_int_cleanup();

  TEST_ASSERT_EQUAL(E_OK, defined);
  TEST_ASSERT_TRUE_MESSAGE(c.pending_before,
                           "the below-threshold UART never became pending");
  TEST_ASSERT_FALSE_MESSAGE(c.pending_after,
                            "ClearInt() left the below-threshold UART pending");
  TEST_ASSERT_TRUE(c.enabled_after);
  TEST_ASSERT_EQUAL_UINT32(PLIC_MAX_PRIORITY, c.threshold_after);
  TEST_ASSERT_EQUAL_UINT(0, hits_before_restore);
}

static void clear_int_discards_pending_uart_with_zero_priority(void) {
  reset_counters();
  ER defined = tk_def_int(UART_IRQ, &s_uart_dint);
  UW mstatus_before = read_mstatus();
  set_mstatus_mie(FALSE);
  EnableInt(UART_IRQ, 3);
  out_w(PLIC_PRIORITY_REG(UART_IRQ), 0);
  const uart_clear_t c = clear_the_pending_uart();
  set_mstatus_mie(TRUE);
  tk_dly_tsk(POLL_TICKS);
  UINT hits_after_restore = s_hits;
  clear_int_cleanup();
  set_mstatus_mie((mstatus_before & MSTATUS_MIE_BIT) != 0);

  TEST_ASSERT_EQUAL(E_OK, defined);
  TEST_ASSERT_TRUE_MESSAGE(c.pending_before,
                           "the priority-0 UART never became pending");
  TEST_ASSERT_FALSE_MESSAGE(c.pending_after,
                            "ClearInt() left the UART pending");
  TEST_ASSERT_TRUE_MESSAGE(c.enabled_after,
                           "ClearInt() disabled the enabled UART");
  TEST_ASSERT_EQUAL_UINT32(0, c.priority_after);
  TEST_ASSERT_EQUAL_HEX32(0, c.mstatus_after & MSTATUS_MIE_BIT);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, hits_after_restore,
                                 "ClearInt() let the UART handler run");
}

static void clear_int_preserves_simultaneous_non_target_request(void) {
  reset_counters();
  s_handler_pending = TRUE;
  ER uart_defined = tk_def_int(UART_IRQ, &s_uart_dint);
  ER rtc_defined = tk_def_int(RTC_IRQ, &s_clear_int_rtc_dint);
  UW mstatus_before = read_mstatus();
  set_mstatus_mie(FALSE);
  EnableInt(UART_IRQ, 3);
  EnableInt(RTC_IRQ, 4);
  UW threshold_before = in_w(PLIC_THRESHOLD_REG);
  out_b(UART_IER, IER_THRE);
  rtc_raise();
  BOOL uart_pending_before = wait_for_pending_without_switch(UART_IRQ);
  BOOL rtc_pending_before = wait_for_pending_without_switch(RTC_IRQ);
  out_b(UART_IER, 0);
  rtc_quiet();
  ClearInt(UART_IRQ);
  BOOL uart_pending_after = source_is_pending(UART_IRQ);
  BOOL rtc_pending_after = source_is_pending(RTC_IRQ);
  BOOL uart_enabled_after = source_is_enabled(UART_IRQ);
  BOOL rtc_enabled_after = source_is_enabled(RTC_IRQ);
  UW uart_priority_after = in_w(PLIC_PRIORITY_REG(UART_IRQ));
  UW rtc_priority_after = in_w(PLIC_PRIORITY_REG(RTC_IRQ));
  UW threshold_after = in_w(PLIC_THRESHOLD_REG);
  UW mstatus_after = read_mstatus();
  UINT uart_hits_before_restore = s_hits;
  set_mstatus_mie(TRUE);
  BOOL rtc_fired = WAIT_UNTIL(s_second_hits == 1);
  UINT uart_hits_after_restore = s_hits;
  UINT rtc_hits = s_second_hits;
  UINT last_intno = s_last_intno;
  BOOL handler_pending = s_handler_pending;
  clear_int_cleanup();
  set_mstatus_mie((mstatus_before & MSTATUS_MIE_BIT) != 0);

  TEST_ASSERT_EQUAL(E_OK, uart_defined);
  TEST_ASSERT_EQUAL(E_OK, rtc_defined);
  TEST_ASSERT_TRUE_MESSAGE(uart_pending_before,
                           "the UART never became pending");
  TEST_ASSERT_TRUE_MESSAGE(rtc_pending_before, "the RTC never became pending");
  TEST_ASSERT_FALSE_MESSAGE(uart_pending_after,
                            "ClearInt() left its target pending");
  TEST_ASSERT_TRUE_MESSAGE(rtc_pending_after,
                           "ClearInt() consumed the non-target RTC request");
  TEST_ASSERT_TRUE(uart_enabled_after);
  TEST_ASSERT_TRUE(rtc_enabled_after);
  TEST_ASSERT_EQUAL_UINT32(3, uart_priority_after);
  TEST_ASSERT_EQUAL_UINT32(4, rtc_priority_after);
  TEST_ASSERT_EQUAL_UINT32(threshold_before, threshold_after);
  TEST_ASSERT_EQUAL_HEX32(0, mstatus_after & MSTATUS_MIE_BIT);
  TEST_ASSERT_EQUAL_UINT(0, uart_hits_before_restore);
  TEST_ASSERT_TRUE_MESSAGE(rtc_fired,
                           "the restored RTC request was not served");
  TEST_ASSERT_EQUAL_UINT(0, uart_hits_after_restore);
  TEST_ASSERT_EQUAL_UINT(1, rtc_hits);
  TEST_ASSERT_EQUAL_UINT(RTC_IRQ, last_intno);
  TEST_ASSERT_FALSE_MESSAGE(handler_pending,
                            "the RTC handler saw its source still pending");
}

static void clear_int_with_no_pending_source_has_no_completion(void) {
  reset_counters();
  out_b(UART_IER, 0);
  rtc_quiet();
  DisableInt(UART_IRQ);
  DisableInt(RTC_IRQ);
  BOOL pending_before = source_is_pending(UART_IRQ);
  ClearInt(UART_IRQ);
  BOOL pending_after = source_is_pending(UART_IRQ);
  clear_int_cleanup();

  TEST_ASSERT_FALSE_MESSAGE(pending_before,
                            "the UART was pending before ClearInt()");
  TEST_ASSERT_FALSE_MESSAGE(pending_after, "ClearInt() created a pending UART");
  TEST_ASSERT_EQUAL_UINT(0, s_hits);
}

static void clear_int_does_not_complete_active_uncompleted_claim(void) {
  reset_counters();
  s_clear_uart_hits = 0;
  ER defined = tk_def_int(
      UART_IRQ, &(T_DINT){.intatr = TA_HLNG, .inthdr = clear_int_uart_handler});
  EnableInt(UART_IRQ, 3);
  out_b(UART_IER, IER_THRE);
  BOOL fired = WAIT_UNTIL(s_clear_uart_hits == 1);
  UINT hits = s_clear_uart_hits;
  UINT depth_before = s_clear_uart_depth_before;
  UINT depth_after = s_clear_uart_depth_after;
  BOOL pending_before = s_clear_uart_pending_before;
  BOOL pending_after = s_clear_uart_pending_after;
  UW mstatus_before = s_clear_uart_mstatus_before;
  UW mstatus_after = s_clear_uart_mstatus_after;
  clear_int_cleanup();

  TEST_ASSERT_EQUAL(E_OK, defined);
  TEST_ASSERT_TRUE_MESSAGE(fired, "the active UART handler did not run");
  TEST_ASSERT_EQUAL_UINT(1, hits);
  TEST_ASSERT_EQUAL_UINT(1, depth_before);
  TEST_ASSERT_EQUAL_UINT(1, depth_after);
  TEST_ASSERT_FALSE(pending_before);
  TEST_ASSERT_FALSE(pending_after);
  TEST_ASSERT_EQUAL_HEX32(0, mstatus_before & MSTATUS_MIE_BIT);
  TEST_ASSERT_EQUAL_HEX32(0, mstatus_after & MSTATUS_MIE_BIT);
}

static void clear_int_handles_new_pending_request_after_end_of_int(void) {
  reset_counters();
  s_clear_completed_hits = 0;
  s_clear_completed_end_calls = 0;
  s_clear_completed_last_intno = 0;
  s_clear_completed_last_depth = 0;
  ER defined = tk_def_int(
      UART_IRQ,
      &(T_DINT){.intatr = TA_HLNG, .inthdr = clear_int_completed_uart_handler});
  EnableInt(UART_IRQ, 3);
  out_b(UART_IER, IER_THRE);
  BOOL fired = WAIT_UNTIL(s_clear_completed_hits >= 1);
  UINT hits = s_clear_completed_hits;
  UINT end_calls = s_clear_completed_end_calls;
  UINT last_intno = s_clear_completed_last_intno;
  UINT last_depth = s_clear_completed_last_depth;
  UINT depth_before = s_clear_completed_depth_before;
  UINT depth_after = s_clear_completed_depth_after;
  BOOL pending_before = s_clear_completed_pending_before;
  BOOL pending_after = s_clear_completed_pending_after;
  UW mstatus_before = s_clear_completed_mstatus_before;
  UW mstatus_after = s_clear_completed_mstatus_after;
  clear_int_cleanup();

  TEST_ASSERT_EQUAL(E_OK, defined);
  TEST_ASSERT_TRUE_MESSAGE(fired, "the completed UART handler did not run");
  TEST_ASSERT_EQUAL_UINT(1, hits);
  TEST_ASSERT_EQUAL_UINT(1, end_calls);
  TEST_ASSERT_EQUAL_UINT(UART_IRQ, last_intno);
  TEST_ASSERT_EQUAL_UINT(1, last_depth);
  TEST_ASSERT_TRUE_MESSAGE(pending_before,
                           "the UART did not repend after EndOfInt()");
  TEST_ASSERT_FALSE_MESSAGE(pending_after,
                            "ClearInt() left the new UART request pending");
  TEST_ASSERT_EQUAL_UINT(1, depth_before);
  TEST_ASSERT_EQUAL_UINT(1, depth_after);
  TEST_ASSERT_EQUAL_HEX32(0, mstatus_before & MSTATUS_MIE_BIT);
  TEST_ASSERT_EQUAL_HEX32(0, mstatus_after & MSTATUS_MIE_BIT);
}

// ---- Nested external interrupts -------------------------------------------
//
// A handler that turns interrupts on (EI) is interrupted by a source of a
// higher priority and by no other: the kernel raises the PLIC threshold to the
// priority of the source it serves. The outer source is the UART at a fixed
// level, the inner one the RTC.
//
// The handlers record what they find in an event log, which the case checks
// after it has put the machine back: asserts come last, as in the other cases.

enum { EV_OUTER_START = 1, EV_INNER, EV_OUTER_END, EV_CHILD, EV_MAX = 6 };
static volatile UINT s_events[EV_MAX];
static volatile UINT s_event_count;

static void event(UINT what) {
  if (s_event_count < EV_MAX) {
    s_events[s_event_count++] = what;
  }
}

// What the two handlers found.
static volatile UINT s_outer_depth, s_inner_depth;
static volatile UW s_outer_sp, s_inner_sp;
static volatile UW s_outer_mie, s_outer_threshold, s_inner_threshold;
static volatile UINT s_timer_calls_in, s_timer_calls_out;
static volatile BOOL s_inner_ran;
static BOOL s_track_mixed_cause;
static BOOL s_endint_outer_on_inner;
static UINT s_mixed_external_start, s_mixed_software_start;
// Seen from the outer handler while it waits: the RTC pending in the PLIC, and
// the timer's pending bit in mip after the wait.
static volatile BOOL s_rtc_pending_seen, s_timer_pending_at_end;

// How the outer handler waits with interrupts on: until the inner handler has
// run, or for a fixed time (so that a source that must not get in is seen not
// to), the level it gives the RTC in its EnableInt() call, and whether the
// inner handler wakes a task.
static volatile BOOL s_wait_for_inner;
static volatile UINT s_spin_ms;
static volatile INT s_rtc_level;
static volatile BOOL s_inner_wakes;

static UW current_sp(void) {
  UW sp;
  __asm__ volatile("mv %0, sp" : "=r"(sp));
  return sp;
}

static UW read_mie(void) {
  UW mie;
  __asm__ volatile("csrr %0, mie" : "=r"(mie));
  return mie;
}

static UW read_mip(void) {
  UW mip;
  __asm__ volatile("csrr %0, mip" : "=r"(mip));
  return mip;
}

static void nested_inner_handler(UINT intno) {
  (void)intno;
  rtc_quiet();
  if (s_clear_outer_from_inner) {
    s_clear_outer_pending_before = CheckInt(UART_IRQ);
    ClearInt(UART_IRQ);
    s_clear_outer_depth = tkmc_isr_depth;
    s_clear_outer_pending_after = CheckInt(UART_IRQ);
    s_clear_outer_threshold = in_w(PLIC_THRESHOLD_REG);
  }
  if (s_endint_outer_on_inner) {
    EndOfInt(UART_IRQ);
  }
  event(EV_INNER);
  s_inner_depth = tkmc_isr_depth;
  s_inner_sp = current_sp();
  s_inner_threshold = in_w(PLIC_THRESHOLD_REG);
  s_inner_ran = TRUE;
  if (s_inner_wakes) {
    tk_sig_sem(s_sem, 1);
  }
}

static void nested_outer_handler(UINT intno) {
  (void)intno;
  if (s_track_mixed_cause) {
    s_last_mcause = 0;
    s_mixed_external_start = s_schedule_calls[TRAP_EXTERNAL];
    s_mixed_software_start = s_schedule_calls[TRAP_SOFTWARE];
  }
  out_b(UART_IER, 0);
  event(EV_OUTER_START);
  s_outer_depth = tkmc_isr_depth;
  s_outer_sp = current_sp();
  s_outer_threshold = in_w(PLIC_THRESHOLD_REG);
  s_outer_mie = read_mie();
  if ((s_outer_mie & MIE_TIMER_AND_SOFTWARE) != 0) {
    // The kernel left the timer and software interrupts on. They do not nest,
    // so a handler that turned interrupts on would stop the machine; the case
    // reports the mie it saw instead.
    s_flag = 1;
    return;
  }
  EI(0);
  s_timer_calls_in = s_timer_handler_calls;
  // A call that writes the threshold, from a handler that has interrupts on.
  EnableInt(RTC_IRQ, s_rtc_level);
  rtc_raise();
  UW start = test_mtime();
  UW limit = (s_wait_for_inner ? WAIT_MS : s_spin_ms) * TEST_MTIME_PER_MS;
  while ((UW)(test_mtime() - start) < limit &&
         !(s_wait_for_inner && s_inner_ran)) {
    if (source_is_pending(RTC_IRQ)) {
      s_rtc_pending_seen = TRUE;
    }
  }
  s_timer_pending_at_end = (read_mip() & MIP_TIMER) != 0;
  s_timer_calls_out = s_timer_handler_calls;
  UINT intsts;
  DI(intsts);
  event(EV_OUTER_END);
  s_flag = 1;
}

static void nested_child(void *arg) {
  (void)arg;
  TEST_ASSERT_EQUAL(E_OK, tk_wai_sem(s_sem, 1, TMO_FEVR));
  event(EV_CHILD);
  test_checkpoint();
}

typedef struct {
  INT rtc_level;
  BOOL wait_for_inner;
  UINT spin_ms;
  BOOL inner_wakes;
} nest_t;

static void nested_set_up(const nest_t *nest) {
  reset_counters();
  reset_trap_records();
  s_event_count = 0;
  s_flag = 0;
  s_inner_ran = FALSE;
  s_rtc_pending_seen = FALSE;
  s_timer_pending_at_end = FALSE;
  s_rtc_level = nest->rtc_level;
  s_wait_for_inner = nest->wait_for_inner;
  s_spin_ms = nest->spin_ms;
  s_inner_wakes = nest->inner_wakes;
  s_sem = tk_cre_sem(
      &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
  TEST_ASSERT_GREATER_THAN(E_OK, s_sem);
  if (nest->inner_wakes) {
    test_spawn(nested_child, CHILD_PRIORITY, NULL);
  }
  const T_DINT inner = {.intatr = TA_HLNG, .inthdr = nested_inner_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(RTC_IRQ, &inner));
  EnableInt(RTC_IRQ, nest->rtc_level);
  const T_DINT outer = {.intatr = TA_HLNG, .inthdr = nested_outer_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &outer));
  EnableInt(UART_IRQ, OUTER_LEVEL);
}

// Puts the machine back, whether the case went well or not.
static void nested_finish(BOOL a_task_was_woken) {
  rtc_quiet();
  DisableInt(RTC_IRQ);
  tk_def_int(RTC_IRQ, NULL);
  clean_up();
  ER deleted = tk_del_sem(s_sem);
  if (a_task_was_woken) {
    test_join_expect(1);
  }
  TEST_ASSERT_EQUAL(E_OK, deleted);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, tkmc_isr_depth,
                                 "a nested run left the trap depth up");
}

// Raises the UART and waits for as many events as the case expects. Returns
// FALSE if they did not all come.
static BOOL nested_run(UINT events) {
  out_b(UART_IER, IER_THRE);
  return WAIT_UNTIL(s_event_count >= events);
}

// The same, without a kernel call: it does not depend on the timer, so a case
// that looks at mie afterwards is not stopped by a mie that is wrong.
static BOOL nested_run_without_the_timer(UINT events) {
  out_b(UART_IER, IER_THRE);
  UW start = test_mtime();
  while (s_event_count < events &&
         (UW)(test_mtime() - start) < WAIT_MS * TEST_MTIME_PER_MS) {
  }
  return s_event_count >= events;
}

// The inner handler ran one level down, on the interrupt stack below the outer
// handler: the case saw a nested trap, not two traps one after the other.
static void assert_the_trap_nested(void) {
  TEST_ASSERT_EQUAL_UINT_MESSAGE(2, s_inner_depth,
                                 "the depth of the inner trap");
  TEST_ASSERT_TRUE_MESSAGE(
      s_inner_sp < s_outer_sp,
      "the inner trap did not go on below the outer handler");
}

static void assert_events(const UINT *expected, UINT count) {
  TEST_ASSERT_EQUAL_UINT_MESSAGE(count, s_event_count, "number of events");
  for (UINT i = 0; i < count; ++i) {
    TEST_ASSERT_EQUAL_UINT_MESSAGE(expected[i], s_events[i],
                                   "the events came in another order");
  }
}

static void nested_interrupt_higher_priority_source_interrupts_handler(void) {
  const nest_t nest = {.rtc_level = HIGHER_LEVEL, .wait_for_inner = TRUE};
  nested_set_up(&nest);
  BOOL done = nested_run(3);
  UW threshold_after = in_w(PLIC_THRESHOLD_REG);
  nested_finish(FALSE);
  TEST_ASSERT_TRUE_MESSAGE(done, "the handlers did not both run");
  const UINT expected[] = {EV_OUTER_START, EV_INNER, EV_OUTER_END};
  assert_events(expected, 3);
  TEST_ASSERT_EQUAL_UINT(1, s_outer_depth);
  assert_the_trap_nested();
  TEST_ASSERT_TRUE_MESSAGE(s_inner_sp > (UW)_isr_stack_start &&
                               s_inner_sp <= (UW)_isr_stack_end,
                           "the inner handler left the interrupt stack");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(OUTER_LEVEL, s_outer_threshold,
                                 "the threshold in the outer handler");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(HIGHER_LEVEL, s_inner_threshold,
                                 "the threshold in the inner handler");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, threshold_after,
                                 "the threshold was not given back");
}

static void clear_int_from_inner_handler_preserves_active_outer_claim(void) {
  const nest_t nest = {
      .rtc_level = HIGHER_LEVEL, .wait_for_inner = TRUE, .inner_wakes = FALSE};
  nested_set_up(&nest);
  paint_the_interrupt_stack();
  s_clear_outer_from_inner = TRUE;
  BOOL done = nested_run(3);
  UINT used = interrupt_stack_high_water();
  UINT threshold_after = in_w(PLIC_THRESHOLD_REG);
  s_clear_outer_from_inner = FALSE;
  nested_finish(FALSE);

  TEST_ASSERT_TRUE_MESSAGE(done, "the nested handlers did not both run");
  assert_the_trap_nested();
  const UINT expected[] = {EV_OUTER_START, EV_INNER, EV_OUTER_END};
  assert_events(expected, 3);
  TEST_ASSERT_FALSE(s_clear_outer_pending_before);
  TEST_ASSERT_FALSE(s_clear_outer_pending_after);
  TEST_ASSERT_EQUAL_UINT(2, s_clear_outer_depth);
  TEST_ASSERT_EQUAL_UINT(HIGHER_LEVEL, s_clear_outer_threshold);
  TEST_ASSERT_EQUAL_UINT(0, threshold_after);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(
      NESTED_HIGH_WATER_MIN, used,
      "ClearInt's nested trap frame was not measured");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      NESTED_HIGH_WATER_MAX, used,
      "ClearInt's nested path used too much stack");
}

// The same level and a lower one: the RTC is pending but must not be served
// while the handler runs, with interrupts on and with a call of EnableInt()
// that writes the threshold. It is served once the handler returns; without
// that check the wait could end for the wrong reason.
static void
nested_interrupt_same_or_lower_priority_source_waits_for_handler(void) {
  for (INT level = OUTER_LEVEL; level >= 1; --level) {
    const nest_t nest = {.rtc_level = level, .spin_ms = 15};
    nested_set_up(&nest);
    BOOL done = nested_run(3);
    nested_finish(FALSE);
    TEST_ASSERT_TRUE_MESSAGE(done, "the RTC was never served");
    TEST_ASSERT_TRUE_MESSAGE(s_rtc_pending_seen,
                             "the RTC was not pending during the wait");
    const UINT expected[] = {EV_OUTER_START, EV_OUTER_END, EV_INNER};
    assert_events(expected, 3);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, s_inner_depth,
                                   "the RTC was served inside the handler");
  }
}

// A task that a nested handler wakes is switched to once, at the exit of the
// outermost trap, after the outer handler has returned: the nested trap leaves
// the switch to the outer one.
static void nested_interrupt_woken_task_runs_after_outermost_handler(void) {
  const nest_t nest = {
      .rtc_level = HIGHER_LEVEL, .wait_for_inner = TRUE, .inner_wakes = TRUE};
  nested_set_up(&nest);
  BOOL done = nested_run(4);
  UINT external_calls = s_schedule_calls[TRAP_EXTERNAL];
  BOOL external_ok = s_schedule_ok[TRAP_EXTERNAL];
  nested_finish(TRUE);
  TEST_ASSERT_TRUE_MESSAGE(done, "the woken task did not run");
  const UINT expected[] = {EV_OUTER_START, EV_INNER, EV_OUTER_END, EV_CHILD};
  assert_events(expected, 4);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, external_calls,
                                 "schedule() runs once, at the outermost exit");
  TEST_ASSERT_TRUE_MESSAGE(external_ok,
                           "schedule() ran on the wrong stack or depth");
}

// The timer and the software interrupt are masked in mie while a handler has
// interrupts on, so the kernel's own traps do not run under the outer handler.
// They are served once it has returned, and mie is as it was.
static void timer_waits_for_handler_that_has_interrupts_on(void) {
  const nest_t nest = {.rtc_level = HIGHER_LEVEL, .spin_ms = 15};
  UW mie_before = read_mie();
  nested_set_up(&nest);
  BOOL done = nested_run_without_the_timer(3);
  UW mie_after = read_mie();
  // Whatever mie is, the case goes on with the timer and software interrupts
  // on, so that a wrong mie fails the asserts below and does not stop the run.
  __asm__ volatile("csrs mie, %0" ::"r"(MIE_TIMER_AND_SOFTWARE) : "memory");
  BOOL timer_served = WAIT_UNTIL(s_timer_handler_calls > s_timer_calls_out);
  nested_finish(FALSE);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(MIE_EXTERNAL, s_outer_mie,
                                  "mie while the handler has interrupts on");
  TEST_ASSERT_TRUE_MESSAGE(done, "the handlers did not run");
  TEST_ASSERT_TRUE_MESSAGE(s_timer_pending_at_end,
                           "no tick fell due during the handler");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(s_timer_calls_in, s_timer_calls_out,
                                 "the timer was served inside the handler");
  TEST_ASSERT_TRUE_MESSAGE(timer_served, "the timer was never served again");
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(mie_before, mie_after,
                                  "mie was not given back");
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(MIE_TIMER_AND_SOFTWARE,
                                  mie_after & MIE_TIMER_AND_SOFTWARE,
                                  "the timer and software bits are off");
}

static void nested_interrupt_keeps_task_registers(void) {
  const nest_t nest = {.rtc_level = HIGHER_LEVEL, .wait_for_inner = TRUE};
  nested_set_up(&nest);
  UW changed = test_spin_with_registers(
      &s_flag, test_mtime() + WAIT_MS * TEST_MTIME_PER_MS,
      (volatile UB *)UART_IER, IER_THRE);
  UINT events = s_event_count;
  nested_finish(FALSE);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(3, events, "the nested trap did not run");
  assert_the_trap_nested();
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, changed,
                                  "a register changed (bit 31: no interrupt)");
}

static void interrupt_stack_holds_nested_handlers(void) {
  const nest_t nest = {
      .rtc_level = HIGHER_LEVEL, .wait_for_inner = TRUE, .inner_wakes = TRUE};
  nested_set_up(&nest);
  paint_the_interrupt_stack();
  BOOL done = nested_run(4);
  UINT used = interrupt_stack_high_water();
  nested_finish(TRUE);
  TEST_ASSERT_TRUE_MESSAGE(done, "the handlers did not run");
  assert_the_trap_nested();
  // Two levels: the nested trap's frame of 128 bytes is on this stack as well
  // as what the handlers call. The lower bound is that frame and a little
  // more, so that a run that never nested is not taken for one that did; the
  // upper one is half of the stack.
  TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(
      NESTED_HIGH_WATER_MIN, used, "the nested trap's frame was not measured");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      NESTED_HIGH_WATER_MAX, used, "two levels use too much of the stack");
}

extern void asm_irq_ret_entry(void);
extern void asm_irq_direct_entry(void);

volatile UW asm_irq_seen_intno;
volatile UW asm_irq_seen_frame;
volatile UW asm_irq_seen_sp;
volatile UW asm_irq_seen_mstatus;
volatile UW asm_irq_seen_mie;
volatile UW asm_irq_seen_s0;
volatile UW asm_irq_seen_s2;
volatile UW asm_irq_seen_pending;
static volatile UINT s_asm_irq_hits;
static volatile UINT s_asm_irq_manual_eoi_calls;
static volatile UINT s_asm_irq_eoi_api_calls;
static volatile ER s_asm_irq_sig_er;
static volatile ER s_asm_irq_child_er;
static BOOL s_asm_irq_disable_source;
static BOOL s_asm_irq_wake_child;
static BOOL s_asm_nested_outer;

void asm_irq_callback(UINT intno, void *frame) {
  ++s_asm_irq_hits;
  if (intno == RTC_IRQ) {
    rtc_quiet();
    if (s_endint_outer_on_inner) {
      EndOfInt(UART_IRQ);
    }
    event(EV_INNER);
    s_inner_depth = tkmc_isr_depth;
    s_inner_sp = current_sp();
    s_inner_threshold = in_w(PLIC_THRESHOLD_REG);
    s_inner_ran = TRUE;
    if (s_inner_wakes) {
      s_asm_irq_sig_er = tk_sig_sem(s_sem, 1);
    }
    return;
  }

  out_b(UART_IER, 0);
  asm_irq_seen_pending = CheckInt(intno);
  const UW *saved_frame = frame;
  asm_irq_seen_s0 = saved_frame[FRAME_S0];
  asm_irq_seen_s2 = saved_frame[FRAME_S2];
  if (s_asm_nested_outer) {
    s_last_mcause = 0;
    s_mixed_external_start = s_schedule_calls[TRAP_EXTERNAL];
    s_mixed_software_start = s_schedule_calls[TRAP_SOFTWARE];
    event(EV_OUTER_START);
    s_outer_depth = tkmc_isr_depth;
    s_outer_sp = current_sp();
    s_outer_threshold = in_w(PLIC_THRESHOLD_REG);
    s_outer_mie = read_mie();
    EI(0);
    s_timer_calls_in = s_timer_handler_calls;
    EnableInt(RTC_IRQ, s_rtc_level);
    rtc_raise();
    UW start = test_mtime();
    while (!s_inner_ran &&
           (UW)(test_mtime() - start) < WAIT_MS * TEST_MTIME_PER_MS) {
      if (source_is_pending(RTC_IRQ)) {
        s_rtc_pending_seen = TRUE;
      }
    }
    s_timer_pending_at_end = (read_mip() & MIP_TIMER) != 0;
    s_timer_calls_out = s_timer_handler_calls;
    UINT intsts;
    DI(intsts);
    event(EV_OUTER_END);
    s_flag = 1;
    return;
  }

  if (s_asm_irq_disable_source) {
    DisableInt(intno);
  }
  for (UINT i = 0; i < s_asm_irq_manual_eoi_calls; ++i) {
    EndOfInt(intno);
    ++s_asm_irq_eoi_api_calls;
  }
  if (s_asm_irq_wake_child) {
    s_asm_irq_sig_er = tk_sig_sem(s_sem, 1);
  }
  s_flag = 1;
}

static void asm_irq_child_with_timeout(void *arg) {
  (void)arg;
  s_asm_irq_child_er = tk_wai_sem(s_sem, 1, 100);
  s_child_cause = s_last_mcause;
  test_checkpoint();
}

static void run_asm_irq_exit(BOOL direct, UINT manual_eoi_calls) {
  reset_counters();
  s_flag = 0;
  s_asm_irq_hits = 0;
  s_asm_irq_manual_eoi_calls = manual_eoi_calls;
  s_asm_irq_eoi_api_calls = 0;
  s_asm_irq_sig_er = E_SYS;
  s_asm_irq_child_er = E_SYS;
  s_child_cause = 0;
  s_asm_irq_disable_source = TRUE;
  s_asm_irq_wake_child = TRUE;
  asm_irq_seen_intno = 0;
  asm_irq_seen_frame = 0;
  asm_irq_seen_sp = 0;
  asm_irq_seen_mstatus = 0;
  asm_irq_seen_mie = 0;
  asm_irq_seen_s0 = 0;
  asm_irq_seen_s2 = 0;
  asm_irq_seen_pending = 1;
  s_sem = tk_cre_sem(
      &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
  TEST_ASSERT_GREATER_THAN(E_OK, s_sem);
  test_spawn(asm_irq_child_with_timeout, CHILD_PRIORITY, NULL);

  const T_DINT dint = {
      .intatr = TA_ASM,
      .inthdr = (FP)(direct ? asm_irq_direct_entry : asm_irq_ret_entry)};
  ER defined = tk_def_int(UART_IRQ, &dint);
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  reset_trap_records();
  s_last_mcause = 0;
  UW mie_before = read_mie();
  UW changed = test_spin_with_registers(
      &s_flag, test_mtime() + WAIT_MS * TEST_MTIME_PER_MS,
      (volatile UB *)UART_IER, IER_THRE);
  UW mie_after = read_mie();
  test_join();
  UINT external_schedules = s_schedule_calls[TRAP_EXTERNAL];
  UINT software_schedules = s_schedule_calls[TRAP_SOFTWARE];
  BOOL external_schedule_ok = s_schedule_ok[TRAP_EXTERNAL];
  BOOL disabled = !source_is_enabled(UART_IRQ);

  // Prove a second claim of the same source can run after the first complete.
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  s_flag = 0;
  s_asm_irq_disable_source = FALSE;
  s_asm_irq_wake_child = FALSE;
  reset_trap_records();
  UW mie_before_again = read_mie();
  UW changed_again = test_spin_with_registers(
      &s_flag, test_mtime() + WAIT_MS * TEST_MTIME_PER_MS,
      (volatile UB *)UART_IER, IER_THRE);
  UW mie_after_again = read_mie();
  clean_up();
  ER deleted = tk_del_sem(s_sem);

  TEST_ASSERT_EQUAL(E_OK, defined);
  TEST_ASSERT_EQUAL(E_OK, deleted);
  TEST_ASSERT_EQUAL_UINT(2, s_asm_irq_hits);
  TEST_ASSERT_EQUAL_UINT(2 * manual_eoi_calls, s_asm_irq_eoi_api_calls);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, changed,
                                 "the interrupted task's registers changed");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      0, changed_again, "the second interrupted task's registers changed");
  TEST_ASSERT_EQUAL_UINT(UART_IRQ, asm_irq_seen_intno);
  TEST_ASSERT_EQUAL_UINT(0, asm_irq_seen_pending);
  TEST_ASSERT_NOT_EQUAL(0, asm_irq_seen_frame);
  TEST_ASSERT_EQUAL_HEX32(0, asm_irq_seen_frame % 16);
  TEST_ASSERT_EQUAL_HEX32(0, asm_irq_seen_sp % 16);
  TEST_ASSERT_TRUE_MESSAGE(
      asm_irq_seen_sp >= (UW)_isr_stack_start &&
          asm_irq_seen_sp + TKMC_IRQCTX_SIZE <= (UW)_isr_stack_end,
      "the raw entry context was outside the interrupt stack");
  TEST_ASSERT_EQUAL_HEX32(TEST_SPIN_PATTERN(11), asm_irq_seen_s0);
  TEST_ASSERT_EQUAL_HEX32(TEST_SPIN_PATTERN(13), asm_irq_seen_s2);
  TEST_ASSERT_EQUAL_HEX32(mie_before, mie_after);
  TEST_ASSERT_EQUAL_HEX32(mie_before_again, mie_after_again);
  TEST_ASSERT_EQUAL_UINT(0, tkmc_isr_depth);
  TEST_ASSERT_EQUAL_HEX32(0, asm_irq_seen_mstatus & MSTATUS_MIE_BIT);
  TEST_ASSERT_EQUAL_HEX32(0, asm_irq_seen_mie & MIE_TIMER_AND_SOFTWARE);
  TEST_ASSERT_TRUE_MESSAGE(disabled,
                           "the raw handler's DisableInt() was undone");
  TEST_ASSERT_EQUAL(E_OK, s_asm_irq_sig_er);
  TEST_ASSERT_EQUAL(E_OK, s_asm_irq_child_er);
  TEST_ASSERT_TRUE_MESSAGE(external_schedule_ok,
                           "external dispatch used an invalid stack or depth");
  if (direct) {
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        0, external_schedules, "direct return scheduled in the external trap");
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        0, software_schedules,
        "direct return did not dispatch in the software trap");
    TEST_ASSERT_EQUAL(MCAUSE_SOFTWARE, s_child_cause);
  } else {
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        0, external_schedules,
        "tk_ret_int did not dispatch in the external trap");
    TEST_ASSERT_EQUAL(MCAUSE_EXTERNAL, s_child_cause);
  }
}

static void run_mixed_nested_entries(BOOL asm_outer, BOOL direct_asm_inner) {
  const nest_t nest = {
      .rtc_level = HIGHER_LEVEL, .wait_for_inner = TRUE, .inner_wakes = TRUE};
  nested_set_up(&nest);
  s_asm_irq_hits = 0;
  s_asm_irq_sig_er = E_SYS;
  s_asm_irq_disable_source = FALSE;
  s_asm_irq_wake_child = FALSE;
  s_asm_nested_outer = asm_outer;
  s_track_mixed_cause = TRUE;

  if (asm_outer) {
    const T_DINT outer = {.intatr = TA_ASM, .inthdr = (FP)asm_irq_ret_entry};
    TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &outer));
  }
  if (!asm_outer) {
    const T_DINT inner = {.intatr = TA_ASM,
                          .inthdr = (FP)(direct_asm_inner ? asm_irq_direct_entry
                                                          : asm_irq_ret_entry)};
    TEST_ASSERT_EQUAL(E_OK, tk_def_int(RTC_IRQ, &inner));
  } else {
    const T_DINT inner = {.intatr = TA_HLNG, .inthdr = nested_inner_handler};
    TEST_ASSERT_EQUAL(E_OK, tk_def_int(RTC_IRQ, &inner));
  }

  paint_the_interrupt_stack();
  reset_trap_records();
  s_last_mcause = 0;
  BOOL done = nested_run(4);
  UINT used = interrupt_stack_high_water();
  UINT external_schedules =
      s_schedule_calls[TRAP_EXTERNAL] - s_mixed_external_start;
  UINT software_schedules =
      s_schedule_calls[TRAP_SOFTWARE] - s_mixed_software_start;
  BOOL external_schedule_ok = s_schedule_ok[TRAP_EXTERNAL];
  nested_finish(TRUE);
  s_track_mixed_cause = FALSE;
  s_endint_outer_on_inner = FALSE;

  TEST_ASSERT_TRUE_MESSAGE(done, "the mixed handlers and child did not run");
  TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(
      NESTED_HIGH_WATER_MIN, used, "the mixed nested trap was not measured");
  TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
      NESTED_HIGH_WATER_MAX, used, "the mixed nested trap used too much stack");
  assert_the_trap_nested();
  const UINT expected[] = {EV_OUTER_START, EV_INNER, EV_OUTER_END, EV_CHILD};
  assert_events(expected, 4);
  TEST_ASSERT_EQUAL_UINT(1, s_outer_depth);
  TEST_ASSERT_EQUAL_UINT(HIGHER_LEVEL, s_inner_threshold);
  TEST_ASSERT_EQUAL_UINT(OUTER_LEVEL, s_outer_threshold);
  TEST_ASSERT_EQUAL_UINT(0, s_timer_calls_out - s_timer_calls_in);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      1, external_schedules,
      "the outer external trap must dispatch exactly once");
  TEST_ASSERT_TRUE_MESSAGE(external_schedule_ok,
                           "schedule() ran at the wrong stack or depth");
  if (!asm_outer && direct_asm_inner) {
    // The nested direct return cannot schedule while the outer handler is
    // active; the outer external exit services the pending request.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, software_schedules,
                                   "a nested direct return dispatched early");
  }
  s_asm_nested_outer = FALSE;
}

static void
nested_interrupt_asm_outer_hlng_inner_dispatches_at_outer_exit(void) {
  s_endint_outer_on_inner = FALSE;
  run_mixed_nested_entries(TRUE, FALSE);
}

static void
nested_interrupt_hlng_outer_asm_inner_direct_return_defers_dispatch(void) {
  s_endint_outer_on_inner = FALSE;
  run_mixed_nested_entries(FALSE, TRUE);
}

static void
nested_interrupt_hlng_outer_asm_inner_tk_ret_int_defers_dispatch(void) {
  s_endint_outer_on_inner = FALSE;
  run_mixed_nested_entries(FALSE, FALSE);
}

static void
nested_interrupt_asm_outer_hlng_inner_end_outer_before_inner_exit(void) {
  s_endint_outer_on_inner = TRUE;
  run_mixed_nested_entries(TRUE, FALSE);
}

static void
nested_interrupt_hlng_outer_asm_inner_end_outer_before_inner_exit(void) {
  s_endint_outer_on_inner = TRUE;
  run_mixed_nested_entries(FALSE, TRUE);
}

static void
nested_interrupt_hlng_outer_asm_inner_tk_ret_int_end_outer_before_inner_exit(
    void) {
  s_endint_outer_on_inner = TRUE;
  run_mixed_nested_entries(FALSE, FALSE);
}

static void end_of_int_from_tk_ret_int_is_idempotent(void) {
  run_asm_irq_exit(FALSE, 2);
}

static void end_of_int_from_direct_return_is_idempotent(void) {
  run_asm_irq_exit(TRUE, 2);
}

static void end_of_int_from_tk_ret_int_completes_with_single_call(void) {
  run_asm_irq_exit(FALSE, 1);
}

static void end_of_int_from_direct_return_completes_with_single_call(void) {
  run_asm_irq_exit(TRUE, 1);
}

static void tk_ret_int_restores_frame_and_dispatches_in_external_trap(void) {
  run_asm_irq_exit(FALSE, 0);
}

static void
asm_handler_direct_return_restores_frame_and_leaves_dispatch_pending(void) {
  run_asm_irq_exit(TRUE, 0);
}

// ---- A TA_HLNG handler that returns with interrupts on ----------------------
//
// The kernel, not the handler, owns mstatus.MIE across the completion of the
// interrupt: it must turn MIE off as soon as the handler returns, so that the
// completion and the trap epilogue run with interrupts off and mret restores
// MIE of the interrupted context. A handler that returns without DI must not
// leave them on.
//
// The cases that look at the interrupted task wait by a busy-poll with no
// kernel call, so the trap lands in the case task (in the idle task the check
// of the task's MIE could not fail), and put the machine back before any
// kernel call, with CPU interrupts on again, so that a kernel that returns with
// MIE off does not stop the cleanup.

enum {
  HLNG_SPIN_MS = 25,    // More than two ticks, with margin.
  NOT_WRITTEN = 0xDEAD, // A depth or threshold the kernel never produces.
};

static volatile UINT s_hlng_runs;
static volatile BOOL s_hlng_second_entered;
static volatile UINT s_hlng_inner_entries, s_hlng_inner_depth;
static volatile BOOL s_hlng_rerequest_pending;
static volatile UW s_hlng_outer_mstatus;
static volatile UINT s_hlng_outer_depth;
static volatile UW s_hlng_outer_threshold;
static volatile UINT s_hlng_timer_calls_before;

static BOOL hlng_poll_until(volatile UINT *what, UINT count) {
  UW start = test_mtime();
  while (*what < count &&
         (UW)(test_mtime() - start) < WAIT_MS * TEST_MTIME_PER_MS) {
  }
  return *what >= count;
}

static void hlng_returning_on_handler(UINT intno) {
  (void)intno;
  out_b(UART_IER, 0);
  EI(0);
  s_hlng_runs += 1; // Returns without DI.
}

static void
hlng_handler_returning_with_interrupts_on_completes_with_them_off(void) {
  reset_counters();
  s_hlng_runs = 0;
  TEST_ASSERT_EQUAL(E_OK, tk_dly_tsk(1)); // Right after a tick edge.
  arm_finish_record();
  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = hlng_returning_on_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &dint));
  EnableInt(UART_IRQ, OUTER_LEVEL);
  out_b(UART_IER, IER_THRE);
  BOOL ran = hlng_poll_until(&s_hlng_runs, 1);
  UW task_mstatus = read_mstatus();
  s_finish_armed = FALSE;
  UINT calls = s_finish_calls;
  UINT entries = s_finish_entries_with_mie;
  UINT exits = s_finish_exits_with_mie;
  UINT depth = tkmc_isr_depth;
  out_b(UART_IER, 0);
  DisableInt(UART_IRQ);
  set_mstatus_mie(TRUE);
  clean_up();
  ER sleep = tk_slp_tsk(TMO_POL);
  TEST_ASSERT_TRUE_MESSAGE(ran, "the handler did not run");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, calls, "completions of the interrupt");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      0, entries, "the completion ran with interrupts on (MIE=1 at entry)");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      0, exits, "the completion returned with interrupts on (MIE=1 at exit)");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, depth, "the trap depth after the return");
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(MSTATUS_MIE_BIT,
                                  task_mstatus & MSTATUS_MIE_BIT,
                                  "the interrupted task resumed with MIE off");
  TEST_ASSERT_EQUAL_MESSAGE(E_TMOUT, sleep,
                            "a wait call from the task was refused");
}

// The inner handler returns with interrupts on while the threshold still hides
// a new request of its own source. The completion restores the threshold of
// the outer handler, so with MIE on it takes the request one level too deep
// (depth 3). With MIE off it is taken after mret, in the outer handler (2).
static void hlng_nested_inner_handler(UINT intno) {
  (void)intno;
  s_hlng_inner_entries += 1;
  if (s_hlng_inner_entries == 1) {
    EndOfInt(RTC_IRQ);
    rtc_quiet();
    rtc_raise();
    UW start = test_mtime();
    while (!CheckInt(RTC_IRQ) &&
           (UW)(test_mtime() - start) < WAIT_MS * TEST_MTIME_PER_MS) {
    }
    s_hlng_rerequest_pending = CheckInt(RTC_IRQ);
    EI(0); // Returns without DI.
  } else {
    rtc_quiet();
    s_hlng_inner_depth = tkmc_isr_depth;
    s_hlng_second_entered = TRUE;
  }
}

static void hlng_nested_outer_handler(UINT intno) {
  (void)intno;
  out_b(UART_IER, 0);
  EI(0);
  EnableInt(RTC_IRQ, HIGHER_LEVEL);
  rtc_raise();
  UW start = test_mtime();
  while (!s_hlng_second_entered &&
         (UW)(test_mtime() - start) < WAIT_MS * TEST_MTIME_PER_MS) {
  }
  s_hlng_outer_mstatus = read_mstatus();
  s_hlng_outer_depth = tkmc_isr_depth;
  s_hlng_outer_threshold = in_w(PLIC_THRESHOLD_REG);
  UINT intsts;
  DI(intsts);
  s_flag = 1;
}

static void
nested_hlng_handler_returning_with_interrupts_on_completes_with_them_off(void) {
  reset_counters();
  s_flag = 0;
  s_hlng_inner_entries = 0;
  s_hlng_inner_depth = NOT_WRITTEN;
  s_hlng_second_entered = FALSE;
  s_hlng_rerequest_pending = FALSE;
  s_hlng_outer_mstatus = 0;
  s_hlng_outer_depth = NOT_WRITTEN;
  s_hlng_outer_threshold = NOT_WRITTEN;
  const T_DINT inner = {.intatr = TA_HLNG, .inthdr = hlng_nested_inner_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(RTC_IRQ, &inner));
  EnableInt(RTC_IRQ, HIGHER_LEVEL);
  const T_DINT outer = {.intatr = TA_HLNG, .inthdr = hlng_nested_outer_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &outer));
  EnableInt(UART_IRQ, OUTER_LEVEL);
  arm_finish_record();
  out_b(UART_IER, IER_THRE);
  UW start = test_mtime();
  while (!s_flag && (UW)(test_mtime() - start) < WAIT_MS * TEST_MTIME_PER_MS) {
  }
  BOOL done = s_flag != 0;
  s_finish_armed = FALSE;
  UINT calls = s_finish_calls;
  UINT entries = s_finish_entries_with_mie;
  UINT exits = s_finish_exits_with_mie;
  UINT depth = tkmc_isr_depth;
  out_b(UART_IER, 0);
  rtc_quiet();
  DisableInt(UART_IRQ);
  DisableInt(RTC_IRQ);
  set_mstatus_mie(TRUE);
  tk_def_int(RTC_IRQ, NULL);
  clean_up();
  TEST_ASSERT_TRUE_MESSAGE(done, "the outer handler did not finish");
  TEST_ASSERT_TRUE_MESSAGE(s_hlng_rerequest_pending,
                           "the new RTC request was not pending");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(2, s_hlng_inner_entries,
                                 "entries of the inner handler");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      2, s_hlng_inner_depth,
      "the new request was taken inside the completion, not after mret");
  // Two inner completions and the outer one.
  TEST_ASSERT_EQUAL_UINT_MESSAGE(3, calls, "completions of the interrupts");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      0, entries, "a completion ran with interrupts on (MIE=1 at entry)");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      0, exits, "a completion returned with interrupts on (MIE=1 at exit)");
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(MSTATUS_MIE_BIT,
                                  s_hlng_outer_mstatus & MSTATUS_MIE_BIT,
                                  "the outer handler resumed with MIE off");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, s_hlng_outer_depth,
                                 "the outer handler resumed at another depth");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      OUTER_LEVEL, s_hlng_outer_threshold,
      "the threshold was not restored to the outer level");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, depth, "the trap depth after the return");
}

// The same handler, seen from the timer and the dispatcher: the task the
// handler woke is switched to at the exit of the same trap, and the tick that
// fell due during the handler is taken as soon as mret enters that task. The
// task reads the timer count before it makes any call that could sleep, so a
// tick lost or taken too late fails the case instead of hanging it in a timed
// wait.
static volatile UINT s_hlng_child_timer_calls;

static void hlng_dispatch_child(void *arg) {
  (void)arg;
  TEST_ASSERT_EQUAL(E_OK, tk_wai_sem(s_sem, 1, TMO_FEVR));
  s_hlng_child_timer_calls = s_timer_handler_calls;
  event(EV_CHILD);
  test_checkpoint();
}

static void hlng_spinning_handler(UINT intno) {
  (void)intno;
  out_b(UART_IER, 0);
  EI(0);
  UW start = test_mtime();
  while ((UW)(test_mtime() - start) < HLNG_SPIN_MS * TEST_MTIME_PER_MS) {
  }
  s_timer_pending_at_end = (read_mip() & MIP_TIMER) != 0;
  s_hlng_timer_calls_before = s_timer_handler_calls;
  tk_sig_sem(s_sem, 1);
  s_flag = 1; // Returns without DI.
}

static void
hlng_handler_returning_with_interrupts_on_lets_timer_and_dispatch_run_after_exit(
    void) {
  reset_counters();
  reset_trap_records();
  s_event_count = 0;
  s_flag = 0;
  s_timer_pending_at_end = FALSE;
  s_hlng_timer_calls_before = NOT_WRITTEN;
  s_hlng_child_timer_calls = 0;
  s_sem = tk_cre_sem(
      &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
  TEST_ASSERT_GREATER_THAN(E_OK, s_sem);
  test_spawn(hlng_dispatch_child, CHILD_PRIORITY, NULL);
  arm_finish_record();
  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = hlng_spinning_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &dint));
  EnableInt(UART_IRQ, OUTER_LEVEL);
  out_b(UART_IER, IER_THRE);
  BOOL child_ran = hlng_poll_until(&s_event_count, 1);
  BOOL timer_served = s_hlng_child_timer_calls > s_hlng_timer_calls_before;
  s_finish_armed = FALSE;
  UINT calls = s_finish_calls;
  UINT entries = s_finish_entries_with_mie;
  UINT exits = s_finish_exits_with_mie;
  UINT external_calls = s_schedule_calls[TRAP_EXTERNAL];
  BOOL external_ok = s_schedule_ok[TRAP_EXTERNAL];
  BOOL timer_ok = s_timer_handler_ok;
  clean_up();
  ER deleted = tk_del_sem(s_sem);
  if (child_ran) {
    test_join_expect(1);
  }
  TEST_ASSERT_TRUE_MESSAGE(s_timer_pending_at_end,
                           "no tick fell due during the handler");
  TEST_ASSERT_TRUE_MESSAGE(timer_served,
                           "the tick was not taken when the woken task ran");
  TEST_ASSERT_TRUE_MESSAGE(timer_ok,
                           "the timer ran on the wrong stack or depth");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, external_calls,
                                 "schedule() runs once, at the exit");
  TEST_ASSERT_TRUE_MESSAGE(external_ok,
                           "schedule() ran on the wrong stack or depth");
  TEST_ASSERT_TRUE_MESSAGE(child_ran, "the woken task did not run");
  TEST_ASSERT_EQUAL(E_OK, deleted);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, calls, "completions of the interrupt");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      0, entries, "the completion ran with interrupts on (MIE=1 at entry)");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(
      0, exits, "the completion returned with interrupts on (MIE=1 at exit)");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, tkmc_isr_depth,
                                 "the trap depth at the end");
}

// ---- A task that ends while an external interrupt is pending ----------------
//
// tk_ext_tsk() and tk_exd_tsk() make the task dormant (or free its TCB) and
// then switch away. A handler that runs after the state change and before the
// switch can start the task again (or create a task in the freed TCB and start
// it), and the new run must begin at its entry with the new start code. The old
// run must never go on.
//
// The window is made wide and certain without help from the kernel: the task
// turns off the software, timer and external bits of mie, waits until the UART
// interrupt is pending in mip, and only then ends. The exit must switch tasks
// without taking an interrupt; the case it switches to turns the bits back on,
// and the handler runs then. Code that returns from the exit instead turns on
// only MEIE, so the external trap is the one trap that can come, whatever order
// the machine gives to pending interrupts, and it comes with the software
// interrupt that the exit raised still pending.
//
// The exiting run waits on a deep stack: the trap frame it leaves behind must
// lie below the frame that tk_sta_tsk() builds at the top of the same stack, or
// the old run would resume from a frame of 0xdeadbeef words and stop in a fatal
// trap instead of failing an assert. After an exit that returned, the code
// reads no local and never returns: what the frames above it held may be gone.
//
// The tasks here are not the runner's: the restarted task must see its start
// code, which test_spawn() does not pass on. The case ends all of them itself,
// before its asserts, so that a failure leaves no task behind.

enum {
  EXIT_TASK_PRIORITY = 2,
  DUMMY_PRIORITY = 3,
  EXIT_RESTART_CODE = 7,
  EXIT_CLEANUP_CODE = -1,
  EXIT_STACK_WORDS = 256,
  DUMMY_STACK_WORDS = 128,
  // What the exiting run reports instead of exiting.
  EXIT_NO_PENDING_INTERRUPT = 1,
  EXIT_SHALLOW_STACK,
  MIE_SOFTWARE_TIMER_EXTERNAL = (1u << 3) | (1u << 7) | (1u << 11),
  MIP_EXTERNAL = 1u << 11
};
_Static_assert(TEST_RUNNER_PRIORITY < EXIT_TASK_PRIORITY &&
                   EXIT_TASK_PRIORITY < DUMMY_PRIORITY &&
                   DUMMY_PRIORITY < TEST_CASE_PRIORITY,
               "the exiting task must preempt the case and the dummies must "
               "run as soon as they start, and the runner outranks them all");

static _Alignas(16) UW s_exit_stack[EXIT_STACK_WORDS];
static _Alignas(16) UW s_restarted_stack[EXIT_STACK_WORDS];
static _Alignas(16) UW s_dummy_stack[DUMMY_STACK_WORDS];
static ID s_dummies[CFN_MAX_TSKID];
static ID s_exit_task;
static volatile BOOL s_exit_deletes;
static volatile UINT s_exit_entries;
static volatile BOOL s_exit_went_on;
static volatile UINT s_exit_failure;
static volatile UINT s_exit_handler_runs;
static volatile ER s_exit_handler_er;
static volatile ID s_exit_handler_id;

static __attribute__((noinline, __noreturn__)) void
exit_with_an_external_interrupt_pending(void) {
  // The pad's address escapes into the asm, so the whole of it is on the stack
  // at every optimization level (a volatile array that is never read is not).
  UW pad[64];
  __asm__ volatile("" ::"r"(pad) : "memory");
  UW sp;
  __asm__ volatile("mv %0, sp" : "=r"(sp));
  if (sp > (UW)&s_exit_stack[EXIT_STACK_WORDS] - 128) {
    s_exit_failure = EXIT_SHALLOW_STACK;
    tk_exd_tsk();
  }
  UW mie;
  __asm__ volatile("csrrc %0, mie, %1"
                   : "=r"(mie)
                   : "r"(MIE_SOFTWARE_TIMER_EXTERNAL)
                   : "memory");
  out_b(UART_IER, IER_THRE);
  UW start = test_mtime();
  while ((read_mip() & MIP_EXTERNAL) == 0) {
    if ((UW)(test_mtime() - start) > WAIT_MS * TEST_MTIME_PER_MS) {
      out_b(UART_IER, 0);
      __asm__ volatile("csrs mie, %0" ::"r"(mie) : "memory");
      s_exit_failure = EXIT_NO_PENDING_INTERRUPT;
      tk_exd_tsk();
    }
  }
  if (s_exit_deletes) {
    tk_exd_tsk();
  } else {
    tk_ext_tsk();
  }
  __asm__ volatile("csrs mie, %0" ::"r"(MIE_EXTERNAL) : "memory");
  s_exit_went_on = TRUE;
  __asm__ volatile("csrs mie, %0" ::"r"(MIE_TIMER_AND_SOFTWARE) : "memory");
  tk_exd_tsk();
  for (;;) {
  }
}

static void exiting_task(INT stacd, void *exinf) {
  (void)exinf;
  if (stacd == EXIT_RESTART_CODE) {
    s_exit_entries += 1;
    tk_exd_tsk();
  }
  if (stacd == EXIT_CLEANUP_CODE) {
    tk_exd_tsk();
  }
  exit_with_an_external_interrupt_pending();
}

static void dummy_task(INT stacd, void *exinf) {
  (void)stacd;
  (void)exinf;
  tk_exd_tsk();
}

static const T_CTSK s_exiting_packet = {.exinf = NULL,
                                        .tskatr = TA_HLNG | TA_USERBUF,
                                        .task = (FP)exiting_task,
                                        .itskpri = EXIT_TASK_PRIORITY,
                                        .stksz = sizeof(s_exit_stack),
                                        .bufptr = s_exit_stack};

// tk_cre_tsk() is one of the calls a handler may make (docs/kernel/
// interrupts.md, "Calls from a handler"). The new task gets another stack: the
// old run's frames on the first one must stay as they are.
static const T_CTSK s_restarted_packet = {.exinf = NULL,
                                          .tskatr = TA_HLNG | TA_USERBUF,
                                          .task = (FP)exiting_task,
                                          .itskpri = EXIT_TASK_PRIORITY,
                                          .stksz = sizeof(s_restarted_stack),
                                          .bufptr = s_restarted_stack};

static const T_CTSK s_dummy_packet = {.exinf = NULL,
                                      .tskatr = TA_HLNG | TA_USERBUF,
                                      .task = (FP)dummy_task,
                                      .itskpri = DUMMY_PRIORITY,
                                      .stksz = sizeof(s_dummy_stack),
                                      .bufptr = s_dummy_stack};

static void restarting_handler(UINT intno) {
  (void)intno;
  out_b(UART_IER, 0);
  if (s_exit_handler_runs++ != 0) {
    return;
  }
  if (s_exit_deletes) {
    // Starts whatever ID it gets, so that no dormant task is left behind; the
    // case checks that it was the freed one.
    ID id = tk_cre_tsk(&s_restarted_packet);
    s_exit_handler_id = id;
    s_exit_handler_er = id > 0 ? tk_sta_tsk(id, EXIT_RESTART_CODE) : id;
  } else {
    s_exit_handler_er = tk_sta_tsk(s_exit_task, EXIT_RESTART_CODE);
  }
}

static void restart_from_an_isr_right_after_the_exit(BOOL deletes) {
  s_exit_deletes = deletes;
  s_exit_entries = 0;
  s_exit_went_on = FALSE;
  s_exit_failure = 0;
  s_exit_handler_runs = 0;
  s_exit_handler_er = E_SYS;
  s_exit_handler_id = 0;
  s_exit_task = tk_cre_tsk(&s_exiting_packet);
  TEST_ASSERT_GREATER_THAN(0, s_exit_task);
  // With every other TCB taken, the one the exit frees is the one the handler
  // gets. No assert from here until every task is ended.
  UINT dummies = 0;
  while (deletes && dummies < CFN_MAX_TSKID) {
    ID id = tk_cre_tsk(&s_dummy_packet);
    if (id <= 0) {
      break;
    }
    s_dummies[dummies++] = id;
  }
  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = restarting_handler};
  ER defined = tk_def_int(UART_IRQ, &dint);
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  ER started = tk_sta_tsk(s_exit_task, 0);
  // Here the task has ended: it outranks the case. An exit that switched
  // without taking an interrupt left the three bits of mie off.
  __asm__ volatile("csrs mie, %0" ::"r"(MIE_SOFTWARE_TIMER_EXTERNAL)
                   : "memory");
  BOOL settled =
      WAIT_UNTIL(s_exit_entries >= 1 || s_exit_went_on || s_exit_failure != 0);
  clean_up();
  if (test_task_state(s_exit_task) == TTS_DMT) {
    tk_sta_tsk(s_exit_task, EXIT_CLEANUP_CODE);
  }
  BOOL ended = WAIT_UNTIL(test_task_state(s_exit_task) == TTS_NOEXS);
  for (UINT i = 0; i < dummies; ++i) {
    tk_sta_tsk(s_dummies[i], 0);
  }
  BOOL dummies_ended = TRUE;
  for (UINT i = 0; i < dummies; ++i) {
    dummies_ended = dummies_ended && test_task_state(s_dummies[i]) == TTS_NOEXS;
  }
  TEST_ASSERT_EQUAL(E_OK, defined);
  TEST_ASSERT_EQUAL(E_OK, started);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, s_exit_failure,
                                 "1: no interrupt was pending, 2: the exiting "
                                 "run was not deep in its stack");
  TEST_ASSERT_TRUE_MESSAGE(ended, "the task did not end");
  TEST_ASSERT_TRUE_MESSAGE(dummies_ended, "a dummy task did not end");
  TEST_ASSERT_TRUE_MESSAGE(settled, "the handler did not run");
  TEST_ASSERT_EQUAL_UINT(1, s_exit_handler_runs);
  TEST_ASSERT_EQUAL_MESSAGE(E_OK, s_exit_handler_er,
                            "the handler could not start the task");
  if (deletes) {
    TEST_ASSERT_EQUAL_MESSAGE(s_exit_task, s_exit_handler_id,
                              "the handler did not get the freed TCB");
  }
  TEST_ASSERT_FALSE_MESSAGE(s_exit_went_on, "the run that ended went on");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(1, s_exit_entries,
                                 "the new run did not start at its entry");
}

static void tk_sta_tsk_by_isr_right_after_tk_ext_tsk_starts_task_anew(void) {
  restart_from_an_isr_right_after_the_exit(FALSE);
}

static void
tk_cre_tsk_by_isr_in_tcb_freed_by_tk_exd_tsk_starts_task_anew(void) {
  restart_from_an_isr_right_after_the_exit(TRUE);
}

// The cases above turn interrupts off and leave the runner unable to time them
// out, so a kernel stop must end the run with a message, not hang it (a fault
// does too: test/panic_fail.c).
__attribute__((__noreturn__)) void tkmc_context_panic(tkmc_context_api api) {
  putstring(":FAIL: a kernel call stopped the system, api=");
  puthex((UW)api);
  putstring("\n");
  test_exit(1);
}

static const test_case_t s_cases[] = {
    CASE(handler_runs_with_its_interrupt_number),
    CASE(end_of_int_is_idempotent_for_hlng_with_disabled_source),
    CASE(end_of_int_lets_still_asserted_source_be_raised_again),
    CASE(handler_runs_as_isr_and_its_wakeup_switches_at_trap_exit),
    CASE(enable_int_delivers_request_that_waited_while_disabled),
    CASE(check_int_observes_disabled_pending_source_without_consuming_it),
    CASE(clear_int_discards_disabled_pending_uart),
    CASE(clear_int_discards_enabled_pending_uart),
    CASE(clear_int_claims_pending_source_below_threshold),
    CASE(clear_int_discards_pending_uart_with_zero_priority),
    CASE(clear_int_preserves_simultaneous_non_target_request),
    CASE(clear_int_with_no_pending_source_has_no_completion),
    CASE(clear_int_does_not_complete_active_uncompleted_claim),
    CASE(clear_int_handles_new_pending_request_after_end_of_int),
    CASE(tk_def_int_replaces_old_definition),
    CASE(tk_def_int_serves_source_that_waited_without_handler),
    CASE(tk_def_int_refuses_bad_definitions_and_keeps_old_one),
    CASE(tk_def_int_refuses_calls_from_isr_or_with_interrupts_off),
    CASE(enable_int_serves_again_source_that_handler_switched_off),
    CASE(enable_int_with_bad_number_or_level_changes_nothing),
    CASE(interrupt_stack_runs_schedule_in_every_trap),
    CASE(interrupt_stack_runs_timer_handler),
    CASE(interrupt_keeps_task_registers),
    CASE(trap_exit_switch_keeps_task_registers),
    CASE(isr_depth_rises_by_one_per_trap_and_drops_back),
    CASE(interrupt_stack_has_room_for_deepest_paths),
    CASE(timer_waits_for_handler_that_has_interrupts_on),
    CASE(nested_interrupt_higher_priority_source_interrupts_handler),
    CASE(clear_int_from_inner_handler_preserves_active_outer_claim),
    CASE(nested_interrupt_same_or_lower_priority_source_waits_for_handler),
    CASE(nested_interrupt_woken_task_runs_after_outermost_handler),
    CASE(nested_interrupt_keeps_task_registers),
    CASE(interrupt_stack_holds_nested_handlers),
    CASE(tk_ret_int_restores_frame_and_dispatches_in_external_trap),
    CASE(asm_handler_direct_return_restores_frame_and_leaves_dispatch_pending),
    CASE(nested_interrupt_asm_outer_hlng_inner_dispatches_at_outer_exit),
    CASE(nested_interrupt_hlng_outer_asm_inner_direct_return_defers_dispatch),
    CASE(nested_interrupt_hlng_outer_asm_inner_tk_ret_int_defers_dispatch),
    CASE(nested_interrupt_asm_outer_hlng_inner_end_outer_before_inner_exit),
    CASE(nested_interrupt_hlng_outer_asm_inner_end_outer_before_inner_exit),
    CASE(
        nested_interrupt_hlng_outer_asm_inner_tk_ret_int_end_outer_before_inner_exit),
    CASE(end_of_int_from_tk_ret_int_is_idempotent),
    CASE(end_of_int_from_direct_return_is_idempotent),
    CASE(end_of_int_from_tk_ret_int_completes_with_single_call),
    CASE(end_of_int_from_direct_return_completes_with_single_call),
    CASE(hlng_handler_returning_with_interrupts_on_completes_with_them_off),
    CASE(
        nested_hlng_handler_returning_with_interrupts_on_completes_with_them_off),
    CASE(
        hlng_handler_returning_with_interrupts_on_lets_timer_and_dispatch_run_after_exit),
    // Last: they take every TCB for a while, and a failure that left them
    // taken would fail every case after it.
    CASE(tk_sta_tsk_by_isr_right_after_tk_ext_tsk_starts_task_anew),
    CASE(tk_cre_tsk_by_isr_in_tcb_freed_by_tk_exd_tsk_starts_task_anew),
};

TEST_SUITE_MAIN("interrupt", s_cases, 5000)
