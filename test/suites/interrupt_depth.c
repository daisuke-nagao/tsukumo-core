/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// External interrupts nested NEST_DEPTH levels deep, and the interrupt stack
// they take. Built once for each depth from 3 to 7 (CMakeLists.txt), so a depth
// that overflows the stack cannot take the other depths down with it.
//
// QEMU virt has two sources that software can raise: the UART's
// transmitter-empty interrupt (source 10) and the goldfish RTC's alarm (source
// 11). The chain alternates them: level k is served at priority k, source 10 on
// the odd levels and source 11 on the even ones. Level k starts level k+1 by
// completing the claim of the level above it on the source it is about to
// reuse (EndOfInt), raising that source's priority to k+1, and raising it with
// interrupts on. The deepest level starts nothing. Every level quiets its own
// device first, so no source is raised again while the chain unwinds.
//
// Handlers record what they see in per-level arrays and print nothing; the
// case checks after it has put the machine back. The interrupt stack and the
// boot stack below it are painted before each case: the boot stack is unused
// after boot, so any word of it that changed is an overflow of the interrupt
// stack.

#include <tk/tkernel.h>
#include <unity.h>

#include "../putstring.h"
#include "asm/rv32/address.h"
#include "intctl.h"
#include "qemu_virt.h"
#include "runner.h"
#include "task.h"

#ifndef NEST_DEPTH
#error "NEST_DEPTH selects the depth of the chain (CMakeLists.txt)"
#endif
_Static_assert(NEST_DEPTH >= 3 && NEST_DEPTH <= PLIC_MAX_PRIORITY,
               "the chain is 3 to 7 levels deep: one priority per level");

extern void **__real_schedule(void *sp);
extern volatile UINT tkmc_isr_depth;
extern unsigned char _isr_stack_start[];
extern unsigned char _isr_stack_end[];
extern unsigned char _stack_start[];
extern unsigned char _stack_end[];

extern UW depth_spin_arm_byte(volatile UW *flag, UW deadline, volatile UB *reg,
                              UB value);
extern UW depth_paint_task_stack(volatile UW *bottom, UW canary, UW paint,
                                 UW *sp_out, UW guard);
extern void depth_asm_entry(void);
extern UW depth_spin_arm_word(volatile UW *flag, UW deadline, volatile UW *reg,
                              UW value);

enum {
  LOWEST_LEVEL = 1, // The lowest PLIC priority.
  WAIT_MS = 400,
  CHILD_PRIORITY = 2,
  PAINT = 0x5a5a5a5a,
  ISR_STACK_SIZE = 2048,
  // shortcut: keep 256 bytes below the live task SP unpainted; increase it if
  // the task's pre-interrupt frames grow.
  TASK_STACK_GUARD = 256,
  TASK_STACK_CANARY = 0x54a5c0de,
  TASK_INITIAL_FRAME_SIZE = 32 * sizeof(UW),
  TASK_STACK_SIZE = TEST_STACK_WORDS * sizeof(UW),
  SPIN_TIMED_OUT = 0x80000000u,
  MIE_TIMER_AND_SOFTWARE = (1u << 7) | (1u << 3),
  // The level between the outermost and the deepest one that wakes the task.
  MIDDLE_LEVEL = (NEST_DEPTH + 1) / 2
};
_Static_assert(CHILD_PRIORITY < TEST_CASE_PRIORITY,
               "the woken task outranks the case");

#define PLIC_THRESHOLD_REG (PLIC_BASE_ADDRESS + PLIC_THRESHOLD_OFFSET)

// The schedule() wrap keeps the mcause of the first trap that switches tasks
// after the chain of a case has started, and whether every switch ran on the
// interrupt stack at depth 1: no level of a chain may switch tasks but the
// outermost exit (or the software trap after a direct return).
static volatile UW s_last_mcause;
// Counts level entries and exits; 0 until level 1 enters.
static volatile UINT s_order;
static volatile UINT s_external_schedules, s_software_schedules;
static volatile BOOL s_schedule_ok = TRUE;

static BOOL on_the_interrupt_stack(UW address) {
  return address > (UW)_isr_stack_start && address <= (UW)_isr_stack_end;
}

void **__wrap_schedule(void *frame) {
  UW cause, sp;
  __asm__ volatile("csrr %0, mcause" : "=r"(cause));
  __asm__ volatile("mv %0, sp" : "=r"(sp));
  // Gated on the chain: before level 1 enters, a tick can wake the polling
  // runner, and its timer-trap switch and the software-trap switch of its
  // next tk_dly_tsk would be latched instead of the exit dispatch.
  if (s_last_mcause == 0 && s_order != 0) {
    s_last_mcause = cause;
  }
  if (cause == MCAUSE_EXTERNAL) {
    s_external_schedules += 1;
  } else if (cause == MCAUSE_SOFTWARE) {
    s_software_schedules += 1;
  }
  if ((cause == MCAUSE_EXTERNAL || cause == MCAUSE_SOFTWARE) &&
      (!on_the_interrupt_stack(sp) || tkmc_isr_depth != 1)) {
    s_schedule_ok = FALSE;
  }
  return __real_schedule(frame);
}

// Not forced inline: at -O0 every inlined copy would add to the frame of each
// level and so to the stack figures. At -O0 it is a call that reads the stack
// pointer inside its own 16-byte frame, 16 bytes below the caller's.
static UW current_sp(void) {
  UW sp;
  __asm__ volatile("mv %0, sp" : "=r"(sp));
  return sp;
}

static UW read_mstatus(void) {
  UW value;
  __asm__ volatile("csrr %0, mstatus" : "=r"(value));
  return value;
}

static UW read_mie(void) {
  UW value;
  __asm__ volatile("csrr %0, mie" : "=r"(value));
  return value;
}

static UW threshold(void) { return in_w(PLIC_THRESHOLD_REG); }

static UINT source_of(UINT level) { return (level % 2) ? UART_IRQ : RTC_IRQ; }

// Sets the alarm to a time that has passed, so the RTC raises its interrupt as
// soon as the IRQ-enable register is written, whatever clock QEMU runs on.
static void rtc_set_alarm(void) {
  UW low = in_w(RTC_TIME_LOW);
  UW high = in_w(RTC_TIME_HIGH);
  out_w(RTC_ALARM_HIGH, high);
  out_w(RTC_ALARM_LOW, low);
}

static void quiet(UINT src) {
  if (src == UART_IRQ) {
    out_b(UART_IER, 0);
  } else {
    out_w(RTC_IRQ_ENABLED, 0);
    out_w(RTC_CLEAR_ALARM, 1);
    out_w(RTC_CLEAR_INTERRUPT, 1);
  }
}

// Raises src with the patterned register wait of test/interrupt_depth_entries.S
// and returns its result once *flag is set or the wait ran out.
// Inlined so that it adds no frame to each level of the chain, which the
// stack measurement would otherwise count as part of a handler.
static inline __attribute__((always_inline)) UW
raise_and_wait(UINT src, volatile UW *flag) {
  UW deadline = test_mtime() + WAIT_MS * TEST_MTIME_PER_MS;
  if (src == UART_IRQ) {
    return depth_spin_arm_byte(flag, deadline, (volatile UB *)UART_IER,
                               IER_THRE);
  }
  rtc_set_alarm();
  return depth_spin_arm_word(flag, deadline, (volatile UW *)RTC_IRQ_ENABLED, 1);
}

// What one level of the chain saw. order counts the entries and exits of the
// chain levels together: level k enters at k and leaves at 2*NEST_DEPTH+1-k.
typedef struct {
  UINT entry_order, exit_order;
  UINT intno, depth;
  UW sp_entry, sp_before_child, sp_after_child, sp_exit;
  UW mstatus_entry, mstatus_after_child, mie_entry, mie_after_child;
  UW threshold_entry, threshold_after_child;
  const tkmc_interrupt_context *ctx, *ctx_previous;
  const void *ctx_frame;
  UINT ctx_depth, ctx_intno;
  UW probe;
  ER wake_er;
  volatile UW done;
} level_t;

// A request that is not a level of the chain: the case that raises a source
// at the same or a lower priority from the deepest level.
typedef struct {
  UINT intno, depth, order_at_entry;
  UW threshold;
  BOOL pending_before, entered_while_waiting;
  volatile UW done;
} late_t;

// How a case runs. late_priority is 0 for a plain chain.
typedef struct {
  UINT late_priority;
  // Bit k set: an ASM level k leaves by the direct return, clear: tk_ret_int.
  UW direct_levels;
} chain_case_t;

static level_t s_levels[NEST_DEPTH + 2];
static late_t s_late;
static const chain_case_t *s_case;
static volatile UINT s_next_level;
static volatile BOOL s_abort;
// Set by the deepest level before it raises the late request: any entry from
// then on is that request, even one that wrongly gets in at depth N+1.
static volatile BOOL s_late_armed;
static ID s_sem;
// Two tasks wait on the semaphore, one count each: the middle level's signal
// makes the first ready and the deepest level's the second, so a level in the
// middle and the deepest level both request a dispatch that must wait for the
// outermost exit.
enum { WOKEN_TASKS = 2 };
static volatile UINT s_child_order[WOKEN_TASKS];
static volatile UINT s_child_depth[WOKEN_TASKS];
static volatile UW s_child_cause[WOKEN_TASKS];
static volatile ER s_child_er[WOKEN_TASKS];
static volatile UINT s_children_ran;

static BOOL in_isr_stack(UW address, UW size) {
  return address >= (UW)_isr_stack_start &&
         address + size <= (UW)_isr_stack_end;
}

static BOOL boot_stack_is_intact(void) {
  for (const volatile UW *word = (const volatile UW *)_stack_start;
       word < (const volatile UW *)_stack_end; ++word) {
    if (*word != PAINT) {
      return FALSE;
    }
  }
  return TRUE;
}

static BOOL stack_boundary_ok(void) {
  UW sp;
  __asm__ volatile("mv %0, sp" : "=r"(sp));
  return in_isr_stack(sp, 0) && (sp & 15) == 0 && boot_stack_is_intact();
}

static void paint(unsigned char *start, unsigned char *end) {
  for (volatile UW *word = (volatile UW *)start; word < (volatile UW *)end;
       ++word) {
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

static void wake_the_task(level_t *level) {
  level->wake_er = tk_sig_sem(s_sem, 1);
}

static void late_entry(UINT intno, UINT depth) {
  s_late.intno = intno;
  s_late.depth = depth;
  s_late.order_at_entry = s_order;
  s_late.threshold = threshold();
  s_late.done = 1;
}

// The deepest level of the late-request case: opens the gateway of the other
// source (whose claim the level above still holds), raises it at a priority
// that must not get in, and waits with interrupts on.
static void raise_a_request_that_must_wait(UINT own) {
  UINT other = own == UART_IRQ ? RTC_IRQ : UART_IRQ;
  s_late_armed = TRUE;
  EndOfInt(other);
  EnableInt(other, (INT)s_case->late_priority);
  if (other == UART_IRQ) {
    out_b(UART_IER, IER_THRE);
  } else {
    rtc_set_alarm();
    out_w(RTC_IRQ_ENABLED, 1);
  }
  UW start = test_mtime();
  while (!CheckInt(other) &&
         (UW)(test_mtime() - start) < WAIT_MS * TEST_MTIME_PER_MS) {
  }
  s_late.pending_before = CheckInt(other);
  EI(0);
  start = test_mtime();
  while ((UW)(test_mtime() - start) < 20 * TEST_MTIME_PER_MS) {
  }
  UINT intsts;
  DI(intsts);
  s_late.entered_while_waiting = s_late.done != 0;
}

// The body of every level of the chain. It is inlined into the TA_HLNG handler
// and into the callback of the TA_ASM entry, so that each level takes one C
// frame whatever its kind: the stack figures are of the kernel's entries and
// one handler frame, not of the fixture's call chain.
static inline __attribute__((always_inline)) void level_body(UINT intno) {
  UINT depth = tkmc_isr_depth;
  quiet(intno);
  if (s_late_armed || depth != s_next_level || intno != source_of(depth)) {
    late_entry(intno, depth);
    return;
  }
  level_t *level = &s_levels[depth];
  level->entry_order = ++s_order;
  level->intno = intno;
  level->depth = depth;
  level->sp_entry = current_sp();
  level->mstatus_entry = read_mstatus();
  level->mie_entry = read_mie();
  level->threshold_entry = threshold();
  level->ctx = tkmc_active_interrupt_context;
  level->ctx_previous = level->ctx->previous;
  level->ctx_frame = level->ctx->frame;
  level->ctx_depth = level->ctx->depth;
  level->ctx_intno = level->ctx->intno;
  s_next_level = depth + 1;
  if (!stack_boundary_ok()) {
    s_abort = TRUE;
  }
  if (!s_abort && (depth == MIDDLE_LEVEL || depth == NEST_DEPTH)) {
    wake_the_task(level);
  }

  if (depth < NEST_DEPTH && !s_abort) {
    UINT next = source_of(depth + 1);
    level->sp_before_child = current_sp();
    if (depth >= 2) {
      EndOfInt(next); // The claim of level depth-1, on the source reused next.
    }
    EnableInt(next, (INT)(depth + 1));
    if (!stack_boundary_ok()) {
      s_abort = TRUE;
      quiet(next);
      DisableInt(next);
    } else {
      EI(0);
      level->probe = raise_and_wait(next, &s_levels[depth + 1].done);
      level->threshold_after_child = threshold();
      level->mstatus_after_child = read_mstatus();
      level->mie_after_child = read_mie();
      level->sp_after_child = current_sp();
      UINT intsts;
      DI(intsts);
      if (!stack_boundary_ok()) {
        s_abort = TRUE;
        quiet(next);
        DisableInt(next);
      }
      if (level->probe & SPIN_TIMED_OUT) {
        s_abort = TRUE;
        quiet(next);
        DisableInt(next);
      }
    }
  } else if (depth == NEST_DEPTH && s_case->late_priority != 0 && !s_abort) {
    raise_a_request_that_must_wait(intno);
  }

  level->sp_exit = current_sp();
  level->exit_order = ++s_order;
  level->done = 1;
}

static void level_handler(UINT intno) { level_body(intno); }

// Called by depth_asm_entry (test/interrupt_depth_entries.S).
UINT depth_asm_callback(UINT intno, void *frame) {
  (void)frame;
  UINT depth = tkmc_isr_depth;
  level_body(intno);
  return (s_case->direct_levels >> depth) & 1;
}

static void woken_task(void *arg) {
  UINT i = (UINT)(UW)arg;
  s_child_er[i] = tk_wai_sem(s_sem, 1, WAIT_MS);
  s_child_order[i] = s_order;
  s_child_depth[i] = tkmc_isr_depth;
  s_child_cause[i] = s_last_mcause;
  s_children_ran += 1;
  test_checkpoint();
}

// What the case saw at the task level.
typedef struct {
  UW probe, mie_chain_entry, mie_prepaint, mie_postpaint, mie_before, mie_after;
  UW mstatus_prepaint, mstatus_postpaint, threshold_after;
  UINT depth_after;
  const tkmc_interrupt_context *active_after;
  UINT used;
  UINT children_ran_at_resume;
  BOOL boot_stack_intact;
  BOOL task_stack_prepared, task_stack_canary_intact;
  UW task_sp, task_top, task_bottom, task_paint_end, task_painted_sp;
  UW task_frame_sp, task_min_sp, task_painted_used, task_frame_used, task_used;
  ER defined_uart, defined_rtc, deleted;
} task_view_t;

static BOOL paint_task_stack_prefix(task_view_t *view) {
  // tk_cre_tsk leaves initial_sp at the 32-word frame base below stack top.
  view->task_top = (UW)current->initial_sp + TASK_INITIAL_FRAME_SIZE;
  if (view->task_top <= TASK_STACK_SIZE) {
    return FALSE;
  }
  view->task_bottom = view->task_top - TASK_STACK_SIZE;
  UW sampled_sp = current_sp();
  if (sampled_sp <= view->task_bottom + TASK_STACK_GUARD ||
      sampled_sp > view->task_top) {
    return FALSE;
  }
  view->task_paint_end = depth_paint_task_stack(
      (volatile UW *)view->task_bottom, TASK_STACK_CANARY, PAINT,
      &view->task_sp, TASK_STACK_GUARD);
  return view->task_sp > view->task_bottom + TASK_STACK_GUARD &&
         view->task_sp <= view->task_top &&
         view->task_paint_end >= view->task_bottom + sizeof(UW) &&
         view->task_paint_end <= view->task_sp;
}

// The unpainted live-frame guard makes an untouched prefix a conservative
// high-water bound; the saved outer trap frame narrows it when it extends
// farther down the task stack. A frame outside the stack counts as all of it
// used, so that it fails the bound instead of shrinking it.
static void measure_task_stack(task_view_t *view) {
  volatile UW *word = (volatile UW *)(view->task_bottom + sizeof(UW));
  while (word < (volatile UW *)view->task_paint_end && *word == PAINT) {
    ++word;
  }
  view->task_stack_canary_intact =
      *(volatile UW *)view->task_bottom == TASK_STACK_CANARY;
  view->task_painted_sp = (UW)word;
  view->task_painted_used = view->task_top - view->task_painted_sp;
  UW frame = (UW)s_levels[1].ctx_frame;
  view->task_frame_sp = frame >= view->task_bottom && frame < view->task_top
                            ? frame
                            : view->task_bottom;
  view->task_frame_used = view->task_top - view->task_frame_sp;
  view->task_min_sp = view->task_painted_sp;
  if (view->task_frame_sp < view->task_min_sp) {
    view->task_min_sp = view->task_frame_sp;
  }
  view->task_used = view->task_top - view->task_min_sp;
}

static void run_chain(const chain_case_t *chain, const T_DINT *uart,
                      const T_DINT *rtc, task_view_t *view) {
  view->mie_chain_entry = read_mie();
  s_case = chain;
  for (UINT i = 0; i < NEST_DEPTH + 2; ++i) {
    s_levels[i] = (level_t){.wake_er = E_SYS};
  }
  s_late = (late_t){0};
  s_order = 0;
  s_next_level = 1;
  s_abort = FALSE;
  s_late_armed = FALSE;
  s_children_ran = 0;
  for (UINT i = 0; i < WOKEN_TASKS; ++i) {
    s_child_order[i] = 0;
    s_child_depth[i] = 99;
    s_child_cause[i] = 0;
    s_child_er[i] = E_SYS;
  }
  s_sem = tk_cre_sem(
      &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 2});
  TEST_ASSERT_GREATER_THAN(E_OK, s_sem);
  for (UINT i = 0; i < WOKEN_TASKS; ++i) {
    test_spawn(woken_task, CHILD_PRIORITY, (void *)(UW)i);
  }

  view->defined_uart = tk_def_int(UART_IRQ, uart);
  view->defined_rtc = tk_def_int(RTC_IRQ, rtc);
  EnableInt(UART_IRQ, LOWEST_LEVEL);
  DisableInt(RTC_IRQ);

  view->mie_prepaint = read_mie();
  view->mstatus_prepaint = read_mstatus();
  UINT intsts;
  DI(intsts);
  view->task_stack_prepared = paint_task_stack_prefix(view);
  EI(intsts);
  view->mie_postpaint = read_mie();
  view->mstatus_postpaint = read_mstatus();
  if (!view->task_stack_prepared) {
    s_abort = TRUE;
  }
  paint(_stack_start, _stack_end);
  paint(_isr_stack_start, _isr_stack_end);
  view->mie_before = view->mie_postpaint;
  // Counted from here, after the switches that test_spawn() made.
  s_external_schedules = 0;
  s_software_schedules = 0;
  s_schedule_ok = TRUE;
  s_last_mcause = 0;
  if (view->task_stack_prepared) {
    view->probe = raise_and_wait(UART_IRQ, &s_levels[1].done);
    measure_task_stack(view);
  } else {
    view->probe = SPIN_TIMED_OUT;
  }
  view->children_ran_at_resume = s_children_ran;
  view->mie_after = read_mie();
  view->threshold_after = threshold();
  view->depth_after = tkmc_isr_depth;
  view->active_after = tkmc_active_interrupt_context;
  view->used = interrupt_stack_high_water();
  view->boot_stack_intact = boot_stack_is_intact();

  // The late request of a p = 1 case comes in only now; wait for it.
  if (chain->late_priority != 0) {
    UW start = test_mtime();
    while (!s_late.done &&
           (UW)(test_mtime() - start) < WAIT_MS * TEST_MTIME_PER_MS) {
    }
  }

  quiet(UART_IRQ);
  quiet(RTC_IRQ);
  DisableInt(UART_IRQ);
  DisableInt(RTC_IRQ);
  // Drain pending PLIC latches before the next case enables either source.
  ClearInt(UART_IRQ);
  ClearInt(RTC_IRQ);
  tk_def_int(UART_IRQ, NULL);
  tk_def_int(RTC_IRQ, NULL);
  test_join();
  view->deleted = tk_del_sem(s_sem);

  putstring("STACK used=");
  puthex(view->used);
  putstring(" task_frame_bytes=");
  puthex(view->task_sp - (UW)s_levels[1].ctx_frame);
  putstring(" task_painted_used=");
  puthex(view->task_painted_used);
  putstring(" task_frame_used=");
  puthex(view->task_frame_used);
  putstring(" task_bound_used=");
  puthex(view->task_used);
  putstring(" task_bound_left=");
  puthex(TASK_STACK_SIZE - view->task_used);
  putstring(" task_mie=");
  puthex(view->mie_chain_entry);
  putstring(",");
  puthex(view->mie_prepaint);
  putstring(",");
  puthex(view->mie_postpaint);
  putstring(",");
  puthex(view->mie_after);
  putstring(" level_sp=");
  for (UINT k = 1; k <= NEST_DEPTH; ++k) {
    puthex((UW)_isr_stack_end - s_levels[k].sp_entry);
    putstring(k < NEST_DEPTH ? "," : " ");
  }
  putstring("ctx_sp=");
  for (UINT k = 1; k <= NEST_DEPTH; ++k) {
    puthex((UW)_isr_stack_end - (UW)s_levels[k].ctx);
    putstring(k < NEST_DEPTH ? "," : "\n");
  }
}

// The checks every chain shares: the order of the levels, what each level saw,
// and the state the chain left behind.
static void assert_the_chain(const task_view_t *view) {
  TEST_ASSERT_EQUAL(E_OK, view->defined_uart);
  TEST_ASSERT_EQUAL(E_OK, view->defined_rtc);
  TEST_ASSERT_EQUAL(E_OK, view->deleted);
  TEST_ASSERT_TRUE_MESSAGE(view->task_stack_prepared,
                           "the unused task-stack prefix was not painted");
  TEST_ASSERT_TRUE_MESSAGE(view->task_stack_canary_intact,
                           "the task-stack bottom canary was overwritten");
  TEST_ASSERT_TRUE_MESSAGE(view->boot_stack_intact,
                           "the interrupt stack ran into the boot stack");
  TEST_ASSERT_FALSE_MESSAGE(s_abort, "a level timed out or left its bounds");
  TEST_ASSERT_EQUAL_HEX32(view->mie_chain_entry, view->mie_prepaint);
  TEST_ASSERT_EQUAL_HEX32(view->mie_prepaint, view->mie_postpaint);
  TEST_ASSERT_EQUAL_HEX32(MIE_TIMER_AND_SOFTWARE,
                          view->mie_chain_entry & MIE_TIMER_AND_SOFTWARE);
  TEST_ASSERT_EQUAL_HEX32(MSTATUS_MIE, view->mstatus_prepaint & MSTATUS_MIE);
  TEST_ASSERT_EQUAL_HEX32(view->mstatus_prepaint & MSTATUS_MIE,
                          view->mstatus_postpaint & MSTATUS_MIE);
  // The scan starts above the canary word, so an overwritten lowest painted
  // word reads as TASK_STACK_SIZE - 4 bytes used, never TASK_STACK_SIZE.
  TEST_ASSERT_LESS_THAN_UINT_MESSAGE(TASK_STACK_SIZE - sizeof(UW),
                                     view->task_used,
                                     "no room left on the task stack");
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, view->probe,
                                  "the task's registers changed");
  for (UINT k = 1; k <= NEST_DEPTH; ++k) {
    const level_t *level = &s_levels[k];
    TEST_ASSERT_EQUAL_UINT_MESSAGE(k, level->entry_order, "entry order");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2 * NEST_DEPTH + 1 - k, level->exit_order,
                                   "exit order");
    TEST_ASSERT_EQUAL_UINT(source_of(k), level->intno);
    TEST_ASSERT_EQUAL_UINT(k, level->depth);
    TEST_ASSERT_EQUAL_UINT(k, level->ctx_depth);
    TEST_ASSERT_EQUAL_UINT(source_of(k), level->ctx_intno);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(k == 1 ? NULL : s_levels[k - 1].ctx,
                                  level->ctx_previous, "the active chain");
    TEST_ASSERT_TRUE_MESSAGE(
        in_isr_stack((UW)level->ctx, sizeof(tkmc_interrupt_context)),
        "the context record is outside the interrupt stack");
    TEST_ASSERT_EQUAL_HEX32(0, (UW)level->ctx % 16);
    if (k == 1) {
      // The trap frame goes just below where the task's wait had the stack.
      TEST_ASSERT_TRUE_MESSAGE((UW)level->ctx_frame < view->task_sp &&
                                   view->task_sp - (UW)level->ctx_frame <= 256,
                               "the outer frame is not on the task stack");
    } else {
      TEST_ASSERT_TRUE_MESSAGE(in_isr_stack((UW)level->ctx_frame, 128),
                               "a nested frame is outside the stack");
      TEST_ASSERT_TRUE(level->ctx_frame < (const void *)s_levels[k - 1].ctx);
    }
    TEST_ASSERT_EQUAL_UINT_MESSAGE(k, level->threshold_entry,
                                   "threshold at entry");
    TEST_ASSERT_EQUAL_HEX32(0, level->mstatus_entry & MSTATUS_MIE);
    TEST_ASSERT_EQUAL_HEX32(0, level->mie_entry & MIE_TIMER_AND_SOFTWARE);
    TEST_ASSERT_TRUE(in_isr_stack(level->sp_entry, 0));
    TEST_ASSERT_EQUAL_HEX32(0, level->sp_entry % 16);
    TEST_ASSERT_EQUAL_HEX32(level->sp_entry, level->sp_exit);
    if (k < NEST_DEPTH) {
      TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, level->probe,
                                      "a level's registers changed");
      TEST_ASSERT_EQUAL_UINT_MESSAGE(k, level->threshold_after_child,
                                     "threshold after the child returned");
      TEST_ASSERT_EQUAL_HEX32_MESSAGE(level->mie_entry, level->mie_after_child,
                                      "mie was not restored after the child");
      TEST_ASSERT_EQUAL_HEX32(MSTATUS_MIE,
                              level->mstatus_after_child & MSTATUS_MIE);
      TEST_ASSERT_EQUAL_HEX32(level->sp_before_child, level->sp_after_child);
      TEST_ASSERT_TRUE(s_levels[k + 1].sp_entry < level->sp_before_child);
    }
  }
  TEST_ASSERT_EQUAL_UINT(0, view->threshold_after);
  TEST_ASSERT_EQUAL_UINT(0, view->depth_after);
  TEST_ASSERT_NULL(view->active_after);
  TEST_ASSERT_EQUAL_HEX32(view->mie_before, view->mie_after);
  TEST_ASSERT_LESS_THAN_UINT_MESSAGE(ISR_STACK_SIZE, view->used,
                                     "no room left on the interrupt stack");
  if (s_case->late_priority == 0) {
    TEST_ASSERT_FALSE_MESSAGE(s_late.done, "an entry that was not a level");
  }
}

// The tasks that the middle and the deepest level made ready each ran once,
// after the outermost exit, from the trap that the outermost level's exit
// dispatched in: both had run before the case got back from its wait, so no
// later switch (such as one in test_join()) stands in for that dispatch.
static void assert_the_task_ran_after_the_chain(const task_view_t *view,
                                                UW cause) {
  TEST_ASSERT_EQUAL_UINT_MESSAGE(WOKEN_TASKS, view->children_ran_at_resume,
                                 "the woken tasks did not run at the exit");
  TEST_ASSERT_EQUAL(E_OK, s_levels[MIDDLE_LEVEL].wake_er);
  TEST_ASSERT_EQUAL(E_OK, s_levels[NEST_DEPTH].wake_er);
  for (UINT i = 0; i < WOKEN_TASKS; ++i) {
    TEST_ASSERT_EQUAL(E_OK, s_child_er[i]);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2 * NEST_DEPTH, s_child_order[i],
                                   "a woken task ran inside the chain");
    TEST_ASSERT_EQUAL_UINT(0, s_child_depth[i]);
    TEST_ASSERT_EQUAL_HEX32(cause, s_child_cause[i]);
  }
  TEST_ASSERT_TRUE_MESSAGE(s_schedule_ok,
                           "a task switch ran at the wrong depth or stack");
}

static const T_DINT s_hlng = {.intatr = TA_HLNG, .inthdr = level_handler};
static const T_DINT s_asm = {.intatr = TA_ASM, .inthdr = (FP)depth_asm_entry};

#define ODD_LEVELS 0xaaaaaaaau
#define EVEN_LEVELS 0x55555554u
#define ALL_LEVELS 0xfffffffeu

// A chain whose outermost exit dispatches from the external trap.
static void run_external_case(const chain_case_t *chain, const T_DINT *uart,
                              const T_DINT *rtc) {
  task_view_t view = {0};
  run_chain(chain, uart, rtc, &view);
  assert_the_chain(&view);
  TEST_ASSERT_EQUAL_UINT(1, s_external_schedules);
  assert_the_task_ran_after_the_chain(&view, MCAUSE_EXTERNAL);
}

// A chain whose outermost exit is a direct return: the task runs from the
// software trap that follows it.
static void run_software_case(const chain_case_t *chain, const T_DINT *uart,
                              const T_DINT *rtc) {
  task_view_t view = {0};
  run_chain(chain, uart, rtc, &view);
  assert_the_chain(&view);
  TEST_ASSERT_EQUAL_UINT(0, s_external_schedules);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT(1, s_software_schedules);
  assert_the_task_ran_after_the_chain(&view, MCAUSE_SOFTWARE);
}

static void nested_interrupt_c_handlers_nest_to_full_depth(void) {
  static const chain_case_t chain = {.late_priority = 0};
  run_external_case(&chain, &s_hlng, &s_hlng);
}

static void nested_interrupt_asm_handlers_with_tk_ret_int_outside(void) {
  static const chain_case_t chain = {.direct_levels = EVEN_LEVELS};
  run_external_case(&chain, &s_asm, &s_asm);
}

static void nested_interrupt_asm_handlers_with_direct_return_outside(void) {
  static const chain_case_t chain = {.direct_levels = ODD_LEVELS};
  run_software_case(&chain, &s_asm, &s_asm);
}

static void nested_interrupt_c_odd_asm_even_levels_with_tk_ret_int(void) {
  static const chain_case_t chain = {.direct_levels = 0};
  run_external_case(&chain, &s_hlng, &s_asm);
}

static void nested_interrupt_c_odd_asm_even_levels_with_direct_return(void) {
  static const chain_case_t chain = {.direct_levels = ALL_LEVELS};
  run_external_case(&chain, &s_hlng, &s_asm);
}

static void nested_interrupt_asm_odd_c_even_levels_with_tk_ret_int(void) {
  static const chain_case_t chain = {.direct_levels = 0};
  run_external_case(&chain, &s_asm, &s_hlng);
}

static void nested_interrupt_asm_odd_c_even_levels_with_direct_return(void) {
  static const chain_case_t chain = {.direct_levels = ALL_LEVELS};
  run_software_case(&chain, &s_asm, &s_hlng);
}

static void assert_the_late_request_waited(void) {
  TEST_ASSERT_TRUE_MESSAGE(s_late.pending_before,
                           "the late request was never pending");
  TEST_ASSERT_FALSE_MESSAGE(s_late.entered_while_waiting,
                            "the late request got in while the level waited");
  TEST_ASSERT_TRUE(s_late.done);
  TEST_ASSERT_EQUAL_UINT(source_of(NEST_DEPTH - 1), s_late.intno);
}

static void
nested_interrupt_same_priority_request_waits_for_deepest_level(void) {
  static const chain_case_t chain = {.late_priority = NEST_DEPTH};
  run_external_case(&chain, &s_hlng, &s_hlng);
  assert_the_late_request_waited();
  TEST_ASSERT_EQUAL_UINT(NEST_DEPTH, s_late.depth);
  TEST_ASSERT_EQUAL_UINT(NEST_DEPTH + 1, s_late.order_at_entry);
  TEST_ASSERT_EQUAL_UINT(NEST_DEPTH, s_late.threshold);
}

static void
nested_interrupt_lower_priority_request_waits_for_whole_chain(void) {
  static const chain_case_t chain = {.late_priority = 1};
  run_external_case(&chain, &s_hlng, &s_hlng);
  assert_the_late_request_waited();
  TEST_ASSERT_EQUAL_UINT(1, s_late.depth);
  TEST_ASSERT_EQUAL_UINT(2 * NEST_DEPTH, s_late.order_at_entry);
  TEST_ASSERT_EQUAL_UINT(1, s_late.threshold);
}

static const test_case_t s_cases[] = {
    CASE(nested_interrupt_c_handlers_nest_to_full_depth),
    CASE(nested_interrupt_asm_handlers_with_tk_ret_int_outside),
    CASE(nested_interrupt_asm_handlers_with_direct_return_outside),
    CASE(nested_interrupt_c_odd_asm_even_levels_with_tk_ret_int),
    CASE(nested_interrupt_c_odd_asm_even_levels_with_direct_return),
    CASE(nested_interrupt_asm_odd_c_even_levels_with_tk_ret_int),
    CASE(nested_interrupt_asm_odd_c_even_levels_with_direct_return),
    CASE(nested_interrupt_same_priority_request_waits_for_deepest_level),
    CASE(nested_interrupt_lower_priority_request_waits_for_whole_chain),
};

TEST_SUITE_MAIN("interrupt-depth", s_cases, 5000)
