/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <tk/tkernel.h>
#include <unity.h>

#include "context.h"
#include "fakes.h"

enum { FRAME_WORDS = 32 };
static _Alignas(16) UINT s_stack[256];
static UINT s_exinf;

static void entry(INT stacd, void *exinf) {
  (void)stacd;
  (void)exinf;
}

static ID create_task(void) {
  ID id = tk_cre_tsk(&(T_CTSK){.exinf = &s_exinf,
                               .tskatr = TA_HLNG | TA_USERBUF,
                               .task = (FP)entry,
                               .itskpri = HOST_HIGH_PRIORITY,
                               .stksz = sizeof(s_stack),
                               .bufptr = s_stack});
  TEST_ASSERT_GREATER_THAN(0, id);
  return id;
}

static void test_tk_sta_tsk_rebuilds_damaged_frame(void) {
  ID id = create_task();
  TCB *tcb = &tkmc_tcbs[id - 1];
  UINT *frame = tcb->initial_sp;
  const INT codes[] = {0, -1234567, 42};
  for (UINT run = 0; run < HOST_COUNT(codes); ++run) {
    for (UINT i = 0; i < FRAME_WORDS; ++i) {
      frame[i] = 0x12340000u + i;
    }
    tcb->sp = frame - 4;
    TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(id, codes[run]));
    TEST_ASSERT_EQUAL_PTR(tcb->initial_sp, tcb->sp);
    // These word positions are the RV32 launch/save ABI, not host pointers.
    for (UINT i = 0; i < FRAME_WORDS; ++i) {
      UINT expected = 0xdeadbeefu;
      if (i == 0) {
        expected = (UINT)tk_ext_tsk;
      } else if (i == 6) {
        expected = (UINT)codes[run];
      } else if (i == 7) {
        expected = (UINT)&s_exinf;
      } else if (i == 28) {
        expected = (UINT)entry;
      }
      TEST_ASSERT_EQUAL_UINT32(expected, frame[i]);
    }
    host_check_kernel_queues();
    // The PC cannot run and exit a task; leave READY as an exit would.
    tkmc_list_del(&tcb->head);
    tkmc_init_list_head(&tcb->head);
    tcb->tskstat = TTS_DMT;
  }
}

static void test_tk_sta_tsk_clears_previous_wakeup_and_wait_state(void) {
  ID id = create_task();
  TCB *tcb = &tkmc_tcbs[id - 1];
  tcb->wupcnt = 3;
  tcb->wupcause = E_RLWAI;
  tcb->tskwait = TTW_SEM;
  tcb->delay_ticks = 99;
  // What the wait info holds besides its queue link: the union of the
  // per-object fields, whatever their layout.
  unsigned char *details =
      (unsigned char *)&tcb->winfo + sizeof(tcb->winfo.wait_queue);
  const UINT details_size = sizeof(tcb->winfo) - sizeof(tcb->winfo.wait_queue);
  memset(details, 0xa5, details_size);

  TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(id, 17));
  TEST_ASSERT_EQUAL_UINT(0, tcb->wupcnt);
  TEST_ASSERT_EQUAL(E_OK, tcb->wupcause);
  TEST_ASSERT_EQUAL_UINT(0, tcb->tskwait);
  TEST_ASSERT_EQUAL_UINT(0, tcb->delay_ticks);
  TEST_ASSERT_TRUE(tkmc_list_empty(&tcb->winfo.wait_queue));
  TEST_ASSERT_EACH_EQUAL_UINT8(0, details, details_size);
  TEST_ASSERT_EQUAL_PTR((FP)entry, tcb->task);
  TEST_ASSERT_EQUAL_PTR(&s_exinf, tcb->exinf);
  TEST_ASSERT_EQUAL_INT(HOST_HIGH_PRIORITY, tcb->itskpri);
  TEST_ASSERT_EQUAL_INT(id, tcb->tskid);
  host_check_kernel_queues();
}

static void check_start_refused(ID id) {
  TCB *tcb = &tkmc_tcbs[id - 1];
  unsigned char before[sizeof(TCB)];
  UINT frame_before[FRAME_WORDS];
  memcpy(before, tcb, sizeof(before));
  memcpy(frame_before, tcb->initial_sp, sizeof(frame_before));
  TCB *before_next = next;
  TEST_ASSERT_EQUAL(E_OBJ, tk_sta_tsk(id, 99));
  TEST_ASSERT_EQUAL_MEMORY(before, tcb, sizeof(before));
  TEST_ASSERT_EQUAL_MEMORY(frame_before, tcb->initial_sp, sizeof(frame_before));
  TEST_ASSERT_EQUAL_PTR(before_next, next);
  TEST_ASSERT_TRUE(host_interrupts_enabled());
  host_check_kernel_queues();
}

static void test_tk_sta_tsk_leaves_non_dormant_task_unchanged(void) {
  ID id = create_task();
  TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(id, 0));
  check_start_refused(id);
  host_set_current(id);
  check_start_refused(id);
  HOST_EXPECT_WAIT(tk_slp_tsk(TMO_FEVR));
  check_start_refused(id);
}

// ---- Creating a task: what tk_cre_tsk() refuses
// -------------------------------
//
// A refusal is E_PAR (E_RSATR for a bad attribute, which is checked first), and
// it changes nothing: tk_cre_tsk() writes nothing to the stack, so what the
// contract promises is the return value and that no TCB was taken. A refused
// call must never be followed by tk_sta_tsk(): the ID it returned is an error
// code.

extern tkmc_list_head tkmc_free_tcb;

enum { FRAME_BYTES = FRAME_WORDS * sizeof(UINT), STACK_ALIGN = 16 };

static T_CTSK valid_packet(void) {
  return (T_CTSK){.exinf = &s_exinf,
                  .tskatr = TA_HLNG | TA_USERBUF,
                  .task = (FP)entry,
                  .itskpri = HOST_HIGH_PRIORITY,
                  .stksz = sizeof(s_stack),
                  .bufptr = s_stack};
}

static T_CTSK packet_with_stack(void *bufptr, SZ stksz) {
  T_CTSK pk = valid_packet();
  pk.bufptr = bufptr;
  pk.stksz = stksz;
  return pk;
}

static T_CTSK packet_with_priority(PRI itskpri) {
  T_CTSK pk = valid_packet();
  pk.itskpri = itskpri;
  return pk;
}

static void *byte_offset(void *base, UINT offset) {
  return (unsigned char *)base + offset;
}

static void *wrapping_address(void) {
  return (void *)(uintptr_t)(UINTPTR_MAX - 255);
}

static UINT free_tcb_count(void) {
  UINT count = 0;
  for (tkmc_list_head *pos = tkmc_free_tcb.next; pos != &tkmc_free_tcb;
       pos = pos->next) {
    TEST_ASSERT_LESS_OR_EQUAL_UINT(CFN_MAX_TSKID, ++count);
  }
  return count;
}

// Calls tk_cre_tsk() with the packet and checks the refusal: the error code,
// interrupts back on, nothing the kernel keeps changed, and no task slot taken
// from the free list (its head is not in the snapshot).
static void check_refused_with(const T_CTSK *pk, ER expected,
                               const char *label) {
  host_snapshot_t before;
  host_take_snapshot(&before);
  UINT free_before = free_tcb_count();

  TEST_ASSERT_EQUAL_MESSAGE(expected, tk_cre_tsk(pk), label);
  TEST_ASSERT_TRUE_MESSAGE(host_interrupts_enabled(), label);
  host_assert_unchanged(&before, label);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(free_before, free_tcb_count(), label);
  host_check_kernel_queues();
}

static void check_refused(const T_CTSK *pk, ER expected) {
  check_refused_with(pk, expected, "a refused tk_cre_tsk()");
}

// Each packet breaks one rule, and tk_cre_tsk() refuses it with E_PAR.
//
// The stacks that are too small end on a 16 byte boundary, so the size is the
// only rule they break; an offset moves the start to reach sizes that are not a
// multiple of 16. The refusal of 128 is a policy, not memory safety: the frame
// would fit, and the rule asks for room beyond it. The negative sizes are
// refused by the overflow check as well, so they do not show that the size
// check works; the QEMU suite does that, where a low address leaves the
// overflow check no say.
static void
test_tk_cre_tsk_refuses_every_broken_packet_and_changes_nothing(void) {
  struct {
    const char *label;
    BOOL no_packet;
    T_CTSK pk;
  } rows[] = {
      {"no packet", TRUE, {0}},
      {"no entry", FALSE, valid_packet()},
      // 256 bytes at address 0 end on a 16 byte boundary and hold the frame,
      // so only the NULL check can refuse it.
      {"no buffer", FALSE, packet_with_stack(NULL, 256)},
      {"priority 0", FALSE, packet_with_priority(0)},
      {"priority -1", FALSE, packet_with_priority(-1)},
      {"priority INT_MIN", FALSE, packet_with_priority(INT_MIN)},
      {"priority above the lowest", FALSE,
       packet_with_priority(CFN_MAX_PRI + 1)},
      {"priority INT_MAX", FALSE, packet_with_priority(INT_MAX)},
      {"stack of 0", FALSE, packet_with_stack(s_stack, 0)},
      {"stack of 16", FALSE, packet_with_stack(s_stack, 16)},
      {"stack of 112", FALSE, packet_with_stack(s_stack, 112)},
      {"stack of 128", FALSE, packet_with_stack(s_stack, 128)},
      {"stack of 120", FALSE, packet_with_stack(byte_offset(s_stack, 8), 120)},
      {"stack of 127", FALSE, packet_with_stack(byte_offset(s_stack, 1), 127)},
      {"stack of -1", FALSE, packet_with_stack(s_stack, -1)},
      {"stack of INT_MIN", FALSE, packet_with_stack(s_stack, INT_MIN)},
      {"end 8 bytes off a boundary", FALSE, packet_with_stack(s_stack, 520)},
      {"end 8 bytes off, start 8 off", FALSE,
       packet_with_stack(byte_offset(s_stack, 8), 512)},
      {"end 8 bytes off, start 4 off", FALSE,
       packet_with_stack(byte_offset(s_stack, 4), 516)},
      {"end 15 bytes off", FALSE, packet_with_stack(s_stack, 1023)},
      // The end is address 0 after the wrap, which is aligned, and the size is
      // large enough: only the overflow check can refuse it. The address is
      // never used as memory.
      {"end wraps around the address space", FALSE,
       packet_with_stack(wrapping_address(), 256)},
  };
  rows[1].pk.task = NULL;
  // Each packet is refused on a kernel with no task, and on one where a task
  // runs with a ready task of its own priority and one below it, so that a
  // refusal that touched the running task or either ready queue would show.
  static const char *const states[] = {"no task",
                                       "a running and two ready tasks"};
  for (UINT i = 0; i < HOST_COUNT(rows); ++i) {
    for (UINT state = 0; state < HOST_COUNT(states); ++state) {
      char label[96];
      snprintf(label, sizeof(label), "%s, %s", rows[i].label, states[state]);
      host_reset();
      if (state != 0) {
        host_set_current(host_make_task(HOST_HIGH_PRIORITY));
        host_make_task(HOST_HIGH_PRIORITY);
        host_make_task(HOST_LOW_PRIORITY);
      }
      ID next_free = tkmc_list_first_entry(&tkmc_free_tcb, TCB, head)->tskid;
      check_refused_with(rows[i].no_packet ? NULL : &rows[i].pk, E_PAR, label);
      // The next creation still gets the TCB that was first free before the
      // refusal.
      T_CTSK ok = valid_packet();
      TEST_ASSERT_EQUAL_MESSAGE(next_free, tk_cre_tsk(&ok), label);
    }
  }
}

static void test_tk_cre_tsk_accepts_highest_and_lowest_priority(void) {
  const PRI accepted[] = {1, CFN_MAX_PRI};
  for (UINT i = 0; i < HOST_COUNT(accepted); ++i) {
    T_CTSK pk = packet_with_priority(accepted[i]);
    ID id = tk_cre_tsk(&pk);
    TEST_ASSERT_GREATER_THAN(0, id);
    TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(id, 0));
    TEST_ASSERT_EQUAL_INT(accepted[i], tkmc_tcbs[id - 1].itskpri);
    host_check_kernel_queues();
  }
}

enum { GUARD_FILL = 0xa5 };
// A stack with room around it. For the smallest stack that is accepted,
// tk_sta_tsk() builds the frame 16 bytes above the bottom.
typedef struct {
  _Alignas(STACK_ALIGN) unsigned char below[STACK_ALIGN];
  _Alignas(STACK_ALIGN) unsigned char stack[256];
  unsigned char above[STACK_ALIGN];
} guarded_stack_t;

static void
test_tk_cre_tsk_accepts_smallest_stack_that_keeps_frame_inside(void) {
  static guarded_stack_t area;
  memset(&area, GUARD_FILL, sizeof(area));
  const SZ smallest = FRAME_BYTES + STACK_ALIGN;
  T_CTSK pk = packet_with_stack(area.stack, smallest);
  ID id = tk_cre_tsk(&pk);
  TEST_ASSERT_GREATER_THAN(0, id);
  TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(id, 0));

  unsigned char *frame = tkmc_tcbs[id - 1].initial_sp;
  TEST_ASSERT_EQUAL_PTR(area.stack + STACK_ALIGN, frame);
  unsigned char *bytes = (unsigned char *)&area;
  for (UINT i = 0; i < sizeof(area); ++i) {
    if (bytes + i >= frame && bytes + i < frame + FRAME_BYTES) {
      continue; // the frame itself
    }
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(GUARD_FILL, bytes[i],
                                   "tk_sta_tsk wrote outside the frame");
  }
  host_check_kernel_queues();
}

// The smallest size the rule allows is one byte above the frame, not the next
// multiple of 16.
static void test_tk_cre_tsk_accepts_stack_one_byte_above_frame(void) {
  T_CTSK pk = packet_with_stack(byte_offset(s_stack, 15), FRAME_BYTES + 1);
  ID id = tk_cre_tsk(&pk);
  TEST_ASSERT_GREATER_THAN(0, id);
  host_check_kernel_queues();
}

static void test_tk_cre_tsk_requires_alignment_only_at_end_of_stack(void) {
  // The start is 8 bytes off, the end is on a boundary.
  T_CTSK pk = packet_with_stack(byte_offset(s_stack, 8), 504);
  ID id = tk_cre_tsk(&pk);
  TEST_ASSERT_GREATER_THAN(0, id);
  TEST_ASSERT_EQUAL_UINT(0,
                         (uintptr_t)tkmc_tcbs[id - 1].initial_sp % STACK_ALIGN);
  host_check_kernel_queues();
}

// An attribute error is reported before a parameter error, whichever rule the
// packet breaks as well.
static void
test_tk_cre_tsk_reports_attribute_error_before_every_parameter_error(void) {
  const ATR bad_attributes[] = {
      TA_HLNG | TA_USERBUF | TA_DSNAME, // a bit the kernel does not support
      TA_HLNG,                          // no TA_USERBUF
  };
  for (UINT a = 0; a < HOST_COUNT(bad_attributes); ++a) {
    T_CTSK broken[] = {
        valid_packet(),
        packet_with_priority(0),
        packet_with_stack(s_stack, FRAME_BYTES),
        packet_with_stack(byte_offset(s_stack, 8), 512),
        packet_with_stack(wrapping_address(), 256),
        packet_with_stack(NULL, 256),
    };
    broken[0].task = NULL;
    for (UINT i = 0; i < HOST_COUNT(broken); ++i) {
      broken[i].tskatr = bad_attributes[a];
      check_refused(&broken[i], E_RSATR);
    }
  }
}

static void
test_tk_cre_tsk_refused_creation_changes_nothing_and_takes_no_slot(void) {
  // Something is running and something is ready, so that a refusal that
  // touched either would show.
  ID running = host_make_task(HOST_HIGH_PRIORITY);
  host_set_current(running);
  host_make_task(HOST_HIGH_PRIORITY + 1);

  T_CTSK refused[] = {
      packet_with_priority(0),
      packet_with_stack(s_stack, FRAME_BYTES),
      packet_with_stack(byte_offset(s_stack, 8), 512),
      packet_with_stack(wrapping_address(), 256),
  };
  for (UINT i = 0; i < HOST_COUNT(refused); ++i) {
    check_refused(&refused[i], E_PAR);
  }

  // The next free TCB is still the one that was next.
  ID expected = tkmc_list_first_entry(&tkmc_free_tcb, TCB, head)->tskid;
  T_CTSK ok = valid_packet();
  TEST_ASSERT_EQUAL(expected, tk_cre_tsk(&ok));
}

static void test_tk_cre_tsk_refusals_do_not_use_up_task_capacity(void) {
  T_CTSK refused[] = {
      packet_with_priority(0),
      packet_with_stack(s_stack, FRAME_BYTES),
      packet_with_stack(byte_offset(s_stack, 8), 512),
      packet_with_stack(wrapping_address(), 256),
  };
  for (UINT i = 0; i < HOST_COUNT(refused); ++i) {
    check_refused(&refused[i], E_PAR);
  }
  // tk_cre_tsk() writes nothing to the stack, so one stack serves every task.
  T_CTSK ok = valid_packet();
  for (UINT i = 0; i < CFN_MAX_TSKID; ++i) {
    TEST_ASSERT_GREATER_THAN(0, tk_cre_tsk(&ok));
  }
  TEST_ASSERT_EQUAL(E_LIMIT, tk_cre_tsk(&ok));
  // A bad packet is still a parameter error when there is no TCB left: the
  // check comes before the allocation.
  for (UINT i = 0; i < HOST_COUNT(refused); ++i) {
    check_refused(&refused[i], E_PAR);
  }
}

// The kernel's limit on queued wake-up requests (TK_WAKEUP_MAXCNT), written
// out on purpose: a test that used the constant would pass whatever its value.
enum { WAKEUP_LIMIT = 4095 };

// Build real queue membership for each branch; the host cannot run tasks.
static ID make_wakeup_target(UINT state) {
  if (state == TTS_RUN) {
    ID target = host_make_task(HOST_HIGH_PRIORITY);
    host_set_current(target);
    return target;
  }

  ID caller = host_make_task(HOST_HIGH_PRIORITY);
  ID target = host_make_task(state == TTS_WAI ? HOST_HIGH_PRIORITY - 1
                                              : HOST_HIGH_PRIORITY + 1);
  host_set_current(caller);
  if (state == TTS_WAI) {
    ID semid =
        tk_cre_sem(&(T_CSEM){.sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
    TEST_ASSERT_GREATER_THAN(0, semid);
    tkmc_tcbs[caller - 1].tskstat = TTS_RDY;
    host_set_current(target);
    HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 50));
    host_set_current(caller);
  }
  return target;
}

// Calls tk_wup_tsk(target) in the given context and returns its result. The
// host cannot take a trap, so an ISR is the state the RV32 trap entries set
// (tkmc_isr_depth), whatever the interrupt flag says; the interrupt flag is
// the task's own DI() or the MIE a handler left on or off. Neither the flag nor
// the ISR state may be changed by the call.
static ER wake_in_context(ID target, BOOL interrupts_off, BOOL in_isr) {
  const host_context_t entered = host_enter(in_isr, interrupts_off);
  ER ercd = tk_wup_tsk(target);
  BOOL enabled = host_interrupts_enabled();
  BOOL still_in_isr = tkmc_in_isr();
  host_leave(entered);
  TEST_ASSERT_EQUAL(!interrupts_off, enabled);
  TEST_ASSERT_TRUE(host_interrupts_enabled());
  TEST_ASSERT_EQUAL(in_isr, still_in_isr);
  return ercd;
}

// Only wupcnt may change on success; a refusal must leave the snapshot intact.
static void check_wakeup_limit(UINT state, UINT count, BOOL interrupts_off,
                               BOOL in_isr) {
  host_reset();
  ID target = make_wakeup_target(state);
  TCB *tcb = &tkmc_tcbs[target - 1];
  tcb->wupcnt = count;
  host_snapshot_t before;
  host_take_snapshot(&before);

  ER ercd = wake_in_context(target, interrupts_off, in_isr);

  TEST_ASSERT_EQUAL(count < WAKEUP_LIMIT ? E_OK : E_QOVR, ercd);
  UINT expected = count < WAKEUP_LIMIT ? count + 1 : count;
  TEST_ASSERT_EQUAL_UINT(expected, tcb->wupcnt);
  tcb->wupcnt = count;
  host_assert_unchanged(&before, "tk_wup_tsk may only queue one request");
  tcb->wupcnt = expected;
  host_check_kernel_queues();
}

static void
test_tk_wup_tsk_bounds_wakeup_count_for_ready_and_non_sleep_waiting_tasks(
    void) {
  const UINT states[] = {TTS_RDY, TTS_WAI};
  const UINT counts[] = {0, WAKEUP_LIMIT - 1, WAKEUP_LIMIT};
  // Another task may be woken from a task or an ISR, with interrupts on or off.
  for (UINT s = 0; s < HOST_COUNT(states); ++s) {
    for (UINT c = 0; c < HOST_COUNT(counts); ++c) {
      for (UINT off = 0; off < 2; ++off) {
        for (UINT isr = 0; isr < 2; ++isr) {
          check_wakeup_limit(states[s], counts[c], off, isr);
        }
      }
    }
  }
}

// The running task gets a request only from an ISR, which has interrupted it,
// and the interrupt flag a handler left does not matter.
static void test_tk_wup_tsk_bounds_wakeup_count_for_running_task_in_isr(void) {
  const UINT counts[] = {0, WAKEUP_LIMIT - 1, WAKEUP_LIMIT};
  for (UINT c = 0; c < HOST_COUNT(counts); ++c) {
    for (UINT off = 0; off < 2; ++off) {
      check_wakeup_limit(TTS_RUN, counts[c], off, TRUE);
    }
  }
}

// A sleeper is woken, not given a request, whoever calls: the same from a task
// and from an ISR, with interrupts on or off. It is the higher priority, so its
// release asks for a task switch.
static void test_tk_wup_tsk_wakes_sleeper_in_every_context(void) {
  for (UINT off = 0; off < 2; ++off) {
    for (UINT isr = 0; isr < 2; ++isr) {
      host_reset();
      ID sleeper = host_make_task(HOST_HIGH_PRIORITY - 1);
      ID caller = host_make_task(HOST_HIGH_PRIORITY);
      TCB *tcb = &tkmc_tcbs[sleeper - 1];
      host_set_current(sleeper);
      HOST_EXPECT_WAIT(tk_slp_tsk(TMO_FEVR));
      // A value no wake writes, so a wake that skips the cause fails below.
      tcb->wupcause = E_SYS;
      host_set_current(caller);
      UINT requests = host_write_count(HOST_MSIP);

      TEST_ASSERT_EQUAL(E_OK, wake_in_context(sleeper, off, isr));

      TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
      TEST_ASSERT_EQUAL_UINT(0, tcb->wupcnt);
      TEST_ASSERT_EQUAL(E_OK, tcb->wupcause);
      TEST_ASSERT_EQUAL_PTR(tcb, next);
      TEST_ASSERT_EQUAL_UINT(requests + 1, host_write_count(HOST_MSIP));
      host_check_kernel_queues();
    }
  }
}

static void
test_tk_slp_tsk_consuming_wakeup_makes_room_for_one_more_tk_wup_tsk(void) {
  ID caller = host_make_task(HOST_HIGH_PRIORITY);
  ID target = host_make_task(HOST_HIGH_PRIORITY + 1);
  host_set_current(caller);
  tkmc_tcbs[target - 1].wupcnt = WAKEUP_LIMIT;

  // Resume the target to consume one queued request, then resume its caller.
  tkmc_tcbs[caller - 1].tskstat = TTS_RDY;
  host_set_current(target);
  TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(TMO_POL));
  TEST_ASSERT_EQUAL_UINT(WAKEUP_LIMIT - 1, current->wupcnt);
  tkmc_tcbs[target - 1].tskstat = TTS_RDY;
  host_set_current(caller);
  TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(target));
  TEST_ASSERT_EQUAL_UINT(WAKEUP_LIMIT, tkmc_tcbs[target - 1].wupcnt);

  host_snapshot_t before;
  host_take_snapshot(&before);
  TEST_ASSERT_EQUAL(E_QOVR, tk_wup_tsk(target));
  host_assert_unchanged(&before, "the replacement request fills the last slot");
  host_check_kernel_queues();
}

// A task that wakes itself is refused before the limit is checked, so a full
// count gives E_OBJ too, and nothing changes. DI() makes no difference: it does
// not turn the task into an ISR.
static void test_tk_wup_tsk_refuses_self_wake_with_interrupts_on_or_off(void) {
  const UINT counts[] = {0, WAKEUP_LIMIT - 1, WAKEUP_LIMIT};
  for (UINT c = 0; c < HOST_COUNT(counts); ++c) {
    for (UINT off = 0; off < 2; ++off) {
      host_reset();
      ID self = make_wakeup_target(TTS_RUN);
      tkmc_tcbs[self - 1].wupcnt = counts[c];
      host_snapshot_t before;
      host_take_snapshot(&before);

      TEST_ASSERT_EQUAL(E_OBJ, wake_in_context(self, off, FALSE));

      host_assert_unchanged(&before, "a refused self-wakeup changes nothing");
      host_check_kernel_queues();
    }
  }
}

static void
test_tk_ext_tsk_and_tk_exd_tsk_stop_in_invalid_context_before_changing_tasks(
    void) {
  for (UINT api = 0; api < 2; ++api) {
    for (UINT mode = 0; mode < 3; ++mode) {
      for (UINT irq = 0; irq < 2; ++irq) {
        // A valid task with IRQ on is the permitted termination path.
        if (mode == 0 && irq) {
          continue;
        }
        host_reset();
        host_two_tasks_and_objects();
        if (mode == 1) {
          current = NULL;
        }
        tkmc_isr_depth = mode == 2;
        host_snapshot_t before;
        host_take_snapshot(&before);
        UINT saved;
        if (!irq) {
          DI(saved);
        }
        host_fatal_t fatal;
        HOST_CATCH_FATAL_CONTEXT(fatal, api ? tk_exd_tsk() : tk_ext_tsk());
        EI(0);
        TEST_ASSERT_TRUE_MESSAGE(fatal.stopped, "invalid termination returned");
        TEST_ASSERT_EQUAL(api ? TKMC_CONTEXT_EXD_TSK : TKMC_CONTEXT_EXT_TSK,
                          fatal.api);
        TEST_ASSERT_TRUE(fatal.interrupts_off);
        TEST_ASSERT_EQUAL(mode == 2, tkmc_in_isr());
        host_assert_unchanged(&before,
                              "invalid termination must not unlink a task");
        host_check_kernel_queues();
      }
    }
  }
}

void run_task_tests(void) {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_tk_cre_tsk_refuses_every_broken_packet_and_changes_nothing);
  RUN_TEST(test_tk_sta_tsk_rebuilds_damaged_frame);
  RUN_TEST(test_tk_sta_tsk_clears_previous_wakeup_and_wait_state);
  RUN_TEST(test_tk_sta_tsk_leaves_non_dormant_task_unchanged);
  RUN_TEST(test_tk_cre_tsk_accepts_highest_and_lowest_priority);
  RUN_TEST(test_tk_cre_tsk_accepts_smallest_stack_that_keeps_frame_inside);
  RUN_TEST(test_tk_cre_tsk_accepts_stack_one_byte_above_frame);
  RUN_TEST(test_tk_cre_tsk_requires_alignment_only_at_end_of_stack);
  RUN_TEST(
      test_tk_cre_tsk_reports_attribute_error_before_every_parameter_error);
  RUN_TEST(test_tk_cre_tsk_refused_creation_changes_nothing_and_takes_no_slot);
  RUN_TEST(test_tk_cre_tsk_refusals_do_not_use_up_task_capacity);
  RUN_TEST(
      test_tk_wup_tsk_bounds_wakeup_count_for_ready_and_non_sleep_waiting_tasks);
  RUN_TEST(test_tk_wup_tsk_bounds_wakeup_count_for_running_task_in_isr);
  RUN_TEST(test_tk_wup_tsk_wakes_sleeper_in_every_context);
  RUN_TEST(test_tk_slp_tsk_consuming_wakeup_makes_room_for_one_more_tk_wup_tsk);
  RUN_TEST(test_tk_wup_tsk_refuses_self_wake_with_interrupts_on_or_off);
  RUN_TEST(
      test_tk_ext_tsk_and_tk_exd_tsk_stop_in_invalid_context_before_changing_tasks);
}
