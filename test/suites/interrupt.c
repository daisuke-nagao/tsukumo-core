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
#include "panic.h"
#include "runner.h"
#include "task.h"

extern void **__real_schedule(void *sp);
extern volatile UINT tkmc_isr_depth;
// The stack of the C parts of the trap entries (linker.ld).
extern unsigned char _isr_stack_start[];
extern unsigned char _isr_stack_end[];

enum {
  UART_IRQ = 10, // The PLIC source of the UART on the QEMU virt board.
  UART_BASE = 0x10000000,
  UART_IER = UART_BASE + 1, // Interrupt enable register, a byte register.
  IER_THRE = 0x02,          // Transmitter holding register empty.
  // The UART raises the interrupt only after the character in flight has left,
  // which takes a few ticks of the guest clock; the case polls for this long.
  WAIT_MS = 400,
  POLL_TICKS = 1, // tk_dly_tsk(1) sleeps two ticks (see test/runner.c).
  CHILD_PRIORITY = 2,
  PLIC_PRIORITIES = 7 // The priorities 1 to 7 of the QEMU virt PLIC.
};
_Static_assert(CHILD_PRIORITY < TEST_CASE_PRIORITY,
               "the child must wait on the semaphore before the case goes on");

static const UW MCAUSE_SOFTWARE = 0x80000003u;
static const UW MCAUSE_EXTERNAL = 0x8000000bu;
static const UW MCAUSE_TIMER = 0x80000007u;
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

static void uart_handler(UINT intno) {
  out_b(UART_IER, 0);
  s_last_intno = intno;
  s_hits += 1;
}

static volatile UINT s_second_hits;

static void second_handler(UINT intno) {
  out_b(UART_IER, 0);
  s_last_intno = intno;
  s_second_hits += 1;
}

// Polls, in task context with the idle task running in between, until the
// condition holds or WAIT_MS has passed.
#define WAIT_UNTIL(cond)                                                       \
  ({                                                                           \
    UW start_ = test_mtime();                                                  \
    BOOL ok_ = TRUE;                                                           \
    while (!(cond)) {                                                          \
      if ((UW)(test_mtime() - start_) > WAIT_MS * TEST_MTIME_PER_MS) {         \
        ok_ = FALSE;                                                           \
        break;                                                                 \
      }                                                                        \
      tk_dly_tsk(POLL_TICKS);                                                  \
    }                                                                          \
    ok_;                                                                       \
  })

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
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &s_uart_dint));
  EnableInt(UART_IRQ, PLIC_PRIORITIES);
  UW priority = in_w(PLIC_PRIORITY_REG(UART_IRQ));
  out_b(UART_IER, IER_THRE);
  BOOL fired = WAIT_UNTIL(s_hits >= 1);
  // A second interrupt is served only if the first was completed.
  out_b(UART_IER, IER_THRE);
  BOOL fired_again = WAIT_UNTIL(s_hits >= 2);
  clean_up();
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(PLIC_PRIORITIES, priority,
                                   "EnableInt() did not set the priority");
  TEST_ASSERT_TRUE_MESSAGE(fired, "the handler did not run");
  TEST_ASSERT_TRUE_MESSAGE(fired_again, "the interrupt was not completed");
  TEST_ASSERT_EQUAL_UINT(2, s_hits);
  TEST_ASSERT_EQUAL_UINT(UART_IRQ, s_last_intno);
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

static void
handler_runs_as_an_isr_and_its_wakeup_switches_at_the_trap_exit(void) {
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
  EnableInt(UART_IRQ, 1);
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

static void a_disabled_source_waits_until_it_is_enabled(void) {
  reset_counters();
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &s_uart_dint));
  EnableInt(UART_IRQ, 1);
  DisableInt(UART_IRQ);
  out_b(UART_IER, IER_THRE);
  // The source is pending but must not be served. A wait that ended only
  // because the UART had not raised it yet would pass for the wrong reason.
  BOOL pending = WAIT_UNTIL(source_is_pending(UART_IRQ));
  tk_dly_tsk(POLL_TICKS);
  UINT hits_while_disabled = s_hits;
  EnableInt(UART_IRQ, 1);
  BOOL fired = WAIT_UNTIL(s_hits >= 1);
  clean_up();
  TEST_ASSERT_TRUE_MESSAGE(pending, "the UART never raised the interrupt");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, hits_while_disabled,
                                 "a disabled source was served");
  TEST_ASSERT_TRUE_MESSAGE(fired, "the enabled source was not served");
}

static void a_new_definition_replaces_the_old_one(void) {
  reset_counters();
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &s_uart_dint));
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &s_second_dint));
  EnableInt(UART_IRQ, 1);
  out_b(UART_IER, IER_THRE);
  BOOL fired = WAIT_UNTIL(s_second_hits >= 1);
  clean_up();
  TEST_ASSERT_TRUE_MESSAGE(fired, "the new handler did not run");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, s_hits, "the old handler ran");
}

static void
without_a_handler_the_source_is_switched_off_and_served_once_defined(void) {
  reset_counters();
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &s_uart_dint));
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, NULL));
  EnableInt(UART_IRQ, 1);
  BOOL was_enabled = source_is_enabled(UART_IRQ);
  out_b(UART_IER, IER_THRE);
  BOOL switched_off = WAIT_UNTIL(!source_is_enabled(UART_IRQ));
  // The default handler completed the source, so it can be served again.
  out_b(UART_IER, 0);
  UINT hits_before = s_hits;
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &s_uart_dint));
  EnableInt(UART_IRQ, 1);
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
static void a_source_a_handler_switched_off_is_served_again_when_enabled(void) {
  reset_counters();
  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = disabling_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &dint));
  EnableInt(UART_IRQ, 1);
  out_b(UART_IER, IER_THRE);
  BOOL fired = WAIT_UNTIL(s_hits >= 1);
  BOOL stayed_off = !source_is_enabled(UART_IRQ);
  EnableInt(UART_IRQ, 1);
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
static void bad_definitions_are_refused(void) {
  reset_counters();
  T_DINT no_handler = {.intatr = TA_HLNG, .inthdr = NULL};
  T_DINT assembler = {.intatr = TA_ASM, .inthdr = second_handler};
  T_DINT unknown_bit = {.intatr = TA_HLNG | 0x2, .inthdr = second_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &s_uart_dint));
  ER er[] = {
      tk_def_int(0, &s_second_dint),
      tk_def_int(CFN_MAX_INTNO, &s_second_dint),
      tk_def_int(0xffffffffu, &s_second_dint),
      tk_def_int(0, NULL),
      tk_def_int(0xffffffffu, NULL),
      tk_def_int(UART_IRQ, &no_handler),
      tk_def_int(UART_IRQ, &assembler),
      tk_def_int(UART_IRQ, &unknown_bit),
  };
  static const ER expected[] = {E_PAR, E_PAR, E_PAR,   E_PAR,
                                E_PAR, E_PAR, E_RSATR, E_RSATR};
  EnableInt(UART_IRQ, 1);
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

static void
a_handler_cannot_be_defined_from_an_isr_or_with_interrupts_off(void) {
  reset_counters();
  s_handler_er = E_OK;
  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = defining_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &dint));
  EnableInt(UART_IRQ, 1);
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
static void a_bad_number_or_level_changes_nothing(void) {
  const UW priority_before = 3;
  out_w(PLIC_PRIORITY_REG(UART_IRQ), priority_before);
  // Source 32 has priority 0 and cannot fire; only its enable bit is watched.
  out_w(PLIC_ENABLE_REG(CFN_MAX_INTNO), 1);
  EnableInt(0, 1);
  EnableInt(CFN_MAX_INTNO, 1);
  EnableInt(CFN_MAX_INTNO + 1, 1);
  EnableInt(0xffffffffu, 1);
  EnableInt(UART_IRQ, 0);
  EnableInt(UART_IRQ, PLIC_PRIORITIES + 1);
  EnableInt(UART_IRQ, -1);
  DisableInt(0);
  DisableInt(CFN_MAX_INTNO);
  DisableInt(0xffffffffu);
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

extern UW test_spin_with_registers(volatile UW *flag, UW deadline,
                                   volatile UB *reg, UB value);

// Spins in the stub of test/spin_with_registers.S, which holds a pattern in the
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
  EnableInt(UART_IRQ, 1);
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

static void a_task_keeps_its_registers_through_an_interrupt(void) {
  UW changed = registers_after_an_interrupt((FP)flag_handler, FALSE);
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, changed,
                                  "a register changed (bit 31: no interrupt)");
}

// The child that the handler wakes ends after it has run, and its end resumes
// the case from the frame of the external trap (__launch_task): the second
// check of that path besides timer_switch.c. Keep it so.
static void a_task_keeps_its_registers_through_a_switch_at_the_trap_exit(void) {
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
static void a_trap_adds_one_to_the_depth_and_takes_it_off(void) {
  enum { DEPTH = 5 };
  reset_counters();
  s_depth_in_handler = 0;
  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = depth_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &dint));
  EnableInt(UART_IRQ, 1);
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

static void the_interrupt_stack_has_room_for_the_deepest_paths(void) {
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
  EnableInt(UART_IRQ, 1);
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
static void schedule_runs_on_the_interrupt_stack_in_every_trap(void) {
  reset_counters();
  reset_trap_records();
  s_sem = tk_cre_sem(
      &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
  TEST_ASSERT_GREATER_THAN(E_OK, s_sem);
  test_spawn(sleeping_child, CHILD_PRIORITY, NULL);
  const T_DINT dint = {.intatr = TA_HLNG, .inthdr = waking_handler};
  TEST_ASSERT_EQUAL(E_OK, tk_def_int(UART_IRQ, &dint));
  EnableInt(UART_IRQ, 1);
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
static void the_timer_handler_runs_on_the_interrupt_stack(void) {
  reset_trap_records();
  BOOL seen = WAIT_UNTIL(s_timer_handler_calls >= 3);
  TEST_ASSERT_TRUE_MESSAGE(seen, "no timer interrupt");
  TEST_ASSERT_TRUE_MESSAGE(s_timer_handler_ok,
                           "the timer handler: wrong stack or depth");
}

// ---- Nested external interrupts -------------------------------------------
//
// A handler that turns interrupts on (EI) is interrupted by a source of a
// higher priority and by no other: the kernel raises the PLIC threshold to the
// priority of the source it serves. The outer source is the UART at a fixed
// level, the inner one the RTC. Like the UART, the RTC is raised by software:
// an alarm at a time that has already passed is due as soon as it is enabled,
// which does not depend on the clock the machine runs on (an alarm in the
// future would, because the RTC counts the host's time).
//
// The handlers record what they find in an event log, which the case checks
// after it has put the machine back: asserts come last, as in the other cases.

enum {
  RTC_IRQ = 11, // The PLIC source of the goldfish RTC on the QEMU virt board.
  RTC_BASE = 0x101000,
  RTC_TIME_LOW = RTC_BASE + 0x00,
  RTC_TIME_HIGH = RTC_BASE + 0x04,
  RTC_ALARM_LOW = RTC_BASE + 0x08,
  RTC_ALARM_HIGH = RTC_BASE + 0x0c,
  RTC_IRQ_ENABLED = RTC_BASE + 0x10,
  RTC_CLEAR_ALARM = RTC_BASE + 0x14,
  RTC_CLEAR_INTERRUPT = RTC_BASE + 0x1c,
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

static void a_source_of_a_higher_priority_interrupts_a_handler(void) {
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

// The same level and a lower one: the RTC is pending but must not be served
// while the handler runs, with interrupts on and with a call of EnableInt()
// that writes the threshold. It is served once the handler returns; without
// that check the wait could end for the wrong reason.
static void
a_source_of_the_same_or_a_lower_priority_waits_for_the_handler(void) {
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
static void
a_task_a_nested_handler_wakes_runs_after_the_outermost_handler(void) {
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
static void the_timer_waits_for_a_handler_that_has_interrupts_on(void) {
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

static void a_task_keeps_its_registers_through_a_nested_interrupt(void) {
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

static void the_interrupt_stack_holds_nested_handlers(void) {
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

static UINT task_state(ID id) {
  UINT intsts;
  DI(intsts);
  UINT state = tkmc_tcbs[id - 1].tskstat;
  EI(intsts);
  return state;
}

static __attribute__((noinline, noreturn)) void
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
  EnableInt(UART_IRQ, 1);
  ER started = tk_sta_tsk(s_exit_task, 0);
  // Here the task has ended: it outranks the case. An exit that switched
  // without taking an interrupt left the three bits of mie off.
  __asm__ volatile("csrs mie, %0" ::"r"(MIE_SOFTWARE_TIMER_EXTERNAL)
                   : "memory");
  BOOL settled =
      WAIT_UNTIL(s_exit_entries >= 1 || s_exit_went_on || s_exit_failure != 0);
  clean_up();
  if (task_state(s_exit_task) == TTS_DMT) {
    tk_sta_tsk(s_exit_task, EXIT_CLEANUP_CODE);
  }
  BOOL ended = WAIT_UNTIL(task_state(s_exit_task) == TTS_NOEXS);
  for (UINT i = 0; i < dummies; ++i) {
    tk_sta_tsk(s_dummies[i], 0);
  }
  BOOL dummies_ended = TRUE;
  for (UINT i = 0; i < dummies; ++i) {
    dummies_ended = dummies_ended && task_state(s_dummies[i]) == TTS_NOEXS;
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

static void
a_task_restarted_by_an_isr_right_after_tk_ext_tsk_starts_anew(void) {
  restart_from_an_isr_right_after_the_exit(FALSE);
}

static void
a_task_created_by_an_isr_in_the_tcb_tk_exd_tsk_freed_starts_anew(void) {
  restart_from_an_isr_right_after_the_exit(TRUE);
}

// The cases above turn interrupts off and leave the runner unable to time them
// out, so a fault or a kernel stop must end the run with a message, not hang
// it.
void tkmc_panic(UW mcause, UW mepc, UW mtval) {
  putstring(":FAIL: unexpected CPU exception mcause=");
  puthex(mcause);
  putstring(" mepc=");
  puthex(mepc);
  putstring(" mtval=");
  puthex(mtval);
  putstring("\n");
  test_exit(1);
}

void tkmc_context_panic(tkmc_context_api api) {
  putstring(":FAIL: a kernel call stopped the system, api=");
  puthex((UW)api);
  putstring("\n");
  test_exit(1);
}

static const test_case_t s_cases[] = {
    {"handler_runs_with_its_interrupt_number",
     handler_runs_with_its_interrupt_number},
    {"handler_runs_as_an_isr_and_its_wakeup_switches_at_the_trap_exit",
     handler_runs_as_an_isr_and_its_wakeup_switches_at_the_trap_exit},
    {"a_disabled_source_waits_until_it_is_enabled",
     a_disabled_source_waits_until_it_is_enabled},
    {"a_new_definition_replaces_the_old_one",
     a_new_definition_replaces_the_old_one},
    {"without_a_handler_the_source_is_switched_off_and_served_once_defined",
     without_a_handler_the_source_is_switched_off_and_served_once_defined},
    {"bad_definitions_are_refused", bad_definitions_are_refused},
    {"a_handler_cannot_be_defined_from_an_isr_or_with_interrupts_off",
     a_handler_cannot_be_defined_from_an_isr_or_with_interrupts_off},
    {"a_source_a_handler_switched_off_is_served_again_when_enabled",
     a_source_a_handler_switched_off_is_served_again_when_enabled},
    {"a_bad_number_or_level_changes_nothing",
     a_bad_number_or_level_changes_nothing},
    {"schedule_runs_on_the_interrupt_stack_in_every_trap",
     schedule_runs_on_the_interrupt_stack_in_every_trap},
    {"the_timer_handler_runs_on_the_interrupt_stack",
     the_timer_handler_runs_on_the_interrupt_stack},
    {"a_task_keeps_its_registers_through_an_interrupt",
     a_task_keeps_its_registers_through_an_interrupt},
    {"a_task_keeps_its_registers_through_a_switch_at_the_trap_exit",
     a_task_keeps_its_registers_through_a_switch_at_the_trap_exit},
    {"a_trap_adds_one_to_the_depth_and_takes_it_off",
     a_trap_adds_one_to_the_depth_and_takes_it_off},
    {"the_interrupt_stack_has_room_for_the_deepest_paths",
     the_interrupt_stack_has_room_for_the_deepest_paths},
    {"the_timer_waits_for_a_handler_that_has_interrupts_on",
     the_timer_waits_for_a_handler_that_has_interrupts_on},
    {"a_source_of_a_higher_priority_interrupts_a_handler",
     a_source_of_a_higher_priority_interrupts_a_handler},
    {"a_source_of_the_same_or_a_lower_priority_waits_for_the_handler",
     a_source_of_the_same_or_a_lower_priority_waits_for_the_handler},
    {"a_task_a_nested_handler_wakes_runs_after_the_outermost_handler",
     a_task_a_nested_handler_wakes_runs_after_the_outermost_handler},
    {"a_task_keeps_its_registers_through_a_nested_interrupt",
     a_task_keeps_its_registers_through_a_nested_interrupt},
    {"the_interrupt_stack_holds_nested_handlers",
     the_interrupt_stack_holds_nested_handlers},
    // Last: they take every TCB for a while, and a failure that left them
    // taken would fail every case after it.
    {"a_task_restarted_by_an_isr_right_after_tk_ext_tsk_starts_anew",
     a_task_restarted_by_an_isr_right_after_tk_ext_tsk_starts_anew},
    {"a_task_created_by_an_isr_in_the_tcb_tk_exd_tsk_freed_starts_anew",
     a_task_created_by_an_isr_in_the_tcb_tk_exd_tsk_freed_starts_anew},
};

TEST_SUITE_MAIN("interrupt", s_cases, 5000)
