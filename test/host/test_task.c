/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <tk/tkernel.h>
#include <unity.h>

#include "fakes.h"
#include "task.h"

enum { FRAME_WORDS = 32, PRIORITY = 4 };
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
                               .itskpri = PRIORITY,
                               .stksz = sizeof(s_stack),
                               .bufptr = s_stack});
  TEST_ASSERT_GREATER_THAN(0, id);
  return id;
}

void test_task_start_rebuilds_a_damaged_frame(void) {
  ID id = create_task();
  TCB *tcb = &tkmc_tcbs[id - 1];
  UINT *frame = tcb->initial_sp;
  const INT codes[] = {0, -1234567, 42};
  for (UINT run = 0; run < sizeof(codes) / sizeof(codes[0]); ++run) {
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

void test_task_start_clears_previous_wakeup_and_wait_state(void) {
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
  TEST_ASSERT_EQUAL_INT(PRIORITY, tcb->itskpri);
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

void test_starting_a_non_dormant_task_leaves_it_unchanged(void) {
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
                  .itskpri = PRIORITY,
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
// interrupts back on, and every table the kernel keeps for tasks as it was.
static void check_refused(const T_CTSK *pk, ER expected) {
  unsigned char tcbs[sizeof(tkmc_tcbs)];
  unsigned char ready[sizeof(tkmc_ready_queue)];
  memcpy(tcbs, tkmc_tcbs, sizeof(tcbs));
  memcpy(ready, tkmc_ready_queue, sizeof(ready));
  UINT free_before = free_tcb_count();
  TCB *current_before = current;
  TCB *next_before = next;

  TEST_ASSERT_EQUAL(expected, tk_cre_tsk(pk));
  TEST_ASSERT_TRUE(host_interrupts_enabled());
  TEST_ASSERT_EQUAL_MEMORY(tcbs, tkmc_tcbs, sizeof(tcbs));
  TEST_ASSERT_EQUAL_MEMORY(ready, tkmc_ready_queue, sizeof(ready));
  TEST_ASSERT_EQUAL_UINT(free_before, free_tcb_count());
  TEST_ASSERT_EQUAL_PTR(current_before, current);
  TEST_ASSERT_EQUAL_PTR(next_before, next);
  host_check_kernel_queues();
}

void test_a_null_packet_is_refused(void) { check_refused(NULL, E_PAR); }

void test_a_null_entry_is_refused(void) {
  T_CTSK pk = valid_packet();
  pk.task = NULL;
  check_refused(&pk, E_PAR);
}

void test_a_null_buffer_is_refused(void) {
  // 256 bytes at address 0 end on a 16 byte boundary and hold the frame, so
  // only the NULL check can refuse it.
  T_CTSK pk = packet_with_stack(NULL, 256);
  check_refused(&pk, E_PAR);
}

void test_a_priority_outside_the_range_is_refused(void) {
  const PRI refused[] = {0, -1, INT_MIN, CFN_MAX_PRI + 1, INT_MAX};
  for (UINT i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
    T_CTSK pk = packet_with_priority(refused[i]);
    check_refused(&pk, E_PAR);
  }
}

void test_the_highest_and_the_lowest_priority_are_accepted(void) {
  const PRI accepted[] = {1, CFN_MAX_PRI};
  for (UINT i = 0; i < sizeof(accepted) / sizeof(accepted[0]); ++i) {
    T_CTSK pk = packet_with_priority(accepted[i]);
    ID id = tk_cre_tsk(&pk);
    TEST_ASSERT_GREATER_THAN(0, id);
    TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(id, 0));
    TEST_ASSERT_EQUAL_INT(accepted[i], tkmc_tcbs[id - 1].itskpri);
    host_check_kernel_queues();
  }
}

// Each stack here ends on a 16 byte boundary, so the size is the only rule it
// breaks. The offset moves the start, to reach sizes that are not a multiple of
// 16. The refusal of 128 is a policy, not memory safety: the frame would fit,
// and the rule asks for room beyond it. The negative sizes are refused by the
// overflow check as well, so they do not show that the size check works; the
// QEMU suite does that, where a low address leaves the overflow check no say.
void test_a_stack_that_does_not_hold_the_frame_and_more_is_refused(void) {
  const struct {
    UINT offset;
    SZ stksz;
  } refused[] = {{0, 0},   {0, 16},  {0, 112}, {0, 128},
                 {8, 120}, {1, 127}, {0, -1},  {0, INT_MIN}};
  for (UINT i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
    T_CTSK pk = packet_with_stack(byte_offset(s_stack, refused[i].offset),
                                  refused[i].stksz);
    check_refused(&pk, E_PAR);
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

void test_the_smallest_accepted_stack_keeps_the_frame_inside_it(void) {
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

void test_a_stack_that_does_not_end_on_a_16_byte_boundary_is_refused(void) {
  const struct {
    UINT offset;
    SZ stksz;
  } refused[] = {{0, 520}, {8, 512}, {4, 516}, {0, 1023}};
  for (UINT i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
    T_CTSK pk = packet_with_stack(byte_offset(s_stack, refused[i].offset),
                                  refused[i].stksz);
    check_refused(&pk, E_PAR);
  }
}

// The smallest size the rule allows is one byte above the frame, not the next
// multiple of 16.
void test_a_stack_one_byte_above_the_frame_is_accepted(void) {
  T_CTSK pk = packet_with_stack(byte_offset(s_stack, 15), FRAME_BYTES + 1);
  ID id = tk_cre_tsk(&pk);
  TEST_ASSERT_GREATER_THAN(0, id);
  host_check_kernel_queues();
}

void test_only_the_end_of_the_stack_has_to_be_aligned(void) {
  // The start is 8 bytes off, the end is on a boundary.
  T_CTSK pk = packet_with_stack(byte_offset(s_stack, 8), 504);
  ID id = tk_cre_tsk(&pk);
  TEST_ASSERT_GREATER_THAN(0, id);
  TEST_ASSERT_EQUAL_UINT(0,
                         (uintptr_t)tkmc_tcbs[id - 1].initial_sp % STACK_ALIGN);
  host_check_kernel_queues();
}

void test_a_stack_whose_end_wraps_around_the_address_space_is_refused(void) {
  // The end is address 0 after the wrap, which is aligned, and the size is
  // large enough: only the overflow check can refuse it. The address is never
  // used as memory.
  T_CTSK pk = packet_with_stack(wrapping_address(), 256);
  check_refused(&pk, E_PAR);
}

// An attribute error is reported before a parameter error, whichever rule the
// packet breaks as well.
void test_an_attribute_error_comes_before_every_parameter_error(void) {
  const ATR bad_attributes[] = {
      TA_HLNG | TA_USERBUF | TA_DSNAME, // a bit the kernel does not support
      TA_HLNG,                          // no TA_USERBUF
  };
  for (UINT a = 0; a < sizeof(bad_attributes) / sizeof(bad_attributes[0]);
       ++a) {
    T_CTSK broken[] = {
        valid_packet(),
        packet_with_priority(0),
        packet_with_stack(s_stack, FRAME_BYTES),
        packet_with_stack(byte_offset(s_stack, 8), 512),
        packet_with_stack(wrapping_address(), 256),
        packet_with_stack(NULL, 256),
    };
    broken[0].task = NULL;
    for (UINT i = 0; i < sizeof(broken) / sizeof(broken[0]); ++i) {
      broken[i].tskatr = bad_attributes[a];
      check_refused(&broken[i], E_RSATR);
    }
  }
}

void test_a_refused_creation_changes_nothing_and_takes_no_task_slot(void) {
  // Something is running and something is ready, so that a refusal that
  // touched either would show.
  ID running = host_make_task(PRIORITY);
  host_set_current(running);
  host_make_task(PRIORITY + 1);

  T_CTSK refused[] = {
      packet_with_priority(0),
      packet_with_stack(s_stack, FRAME_BYTES),
      packet_with_stack(byte_offset(s_stack, 8), 512),
      packet_with_stack(wrapping_address(), 256),
  };
  for (UINT i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
    check_refused(&refused[i], E_PAR);
  }

  // The next free TCB is still the one that was next.
  ID expected = tkmc_list_first_entry(&tkmc_free_tcb, TCB, head)->tskid;
  T_CTSK ok = valid_packet();
  TEST_ASSERT_EQUAL(expected, tk_cre_tsk(&ok));
}

void test_refusals_do_not_use_up_the_tasks_the_kernel_can_hold(void) {
  T_CTSK refused[] = {
      packet_with_priority(0),
      packet_with_stack(s_stack, FRAME_BYTES),
      packet_with_stack(byte_offset(s_stack, 8), 512),
  };
  for (UINT i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
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
  for (UINT i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
    check_refused(&refused[i], E_PAR);
  }
}

enum { WAKEUP_LIMIT = 4095 };

// Build real queue membership for each branch; the host cannot run tasks.
static ID make_wakeup_target(UINT state) {
  if (state == TTS_RUN) {
    ID target = host_make_task(PRIORITY);
    host_set_current(target);
    return target;
  }

  ID caller = host_make_task(PRIORITY);
  ID target = host_make_task(state == TTS_WAI ? PRIORITY - 1 : PRIORITY + 1);
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

// Only wupcnt may change on success; a refusal must leave the snapshot intact.
static void check_wakeup_limit(UINT state, UINT count, BOOL interrupts_off) {
  host_reset();
  ID target = make_wakeup_target(state);
  TCB *tcb = &tkmc_tcbs[target - 1];
  tcb->wupcnt = count;
  host_snapshot_t before;
  host_take_snapshot(&before);

  UINT saved = 0;
  if (interrupts_off) {
    DI(saved);
  }
  ER ercd = tk_wup_tsk(target);
  BOOL enabled = host_interrupts_enabled();
  if (interrupts_off) {
    EI(saved);
  }

  TEST_ASSERT_EQUAL(!interrupts_off, enabled);
  TEST_ASSERT_TRUE(host_interrupts_enabled());
  TEST_ASSERT_EQUAL(count < WAKEUP_LIMIT ? E_OK : E_QOVR, ercd);
  UINT expected = count < WAKEUP_LIMIT ? count + 1 : count;
  TEST_ASSERT_EQUAL_UINT(expected, tcb->wupcnt);
  tcb->wupcnt = count;
  host_assert_unchanged(&before, "tk_wup_tsk may only queue one request");
  tcb->wupcnt = expected;
  host_check_kernel_queues();
}

void test_wakeup_count_is_bounded_for_ready_and_non_sleep_waiting_tasks(void) {
  const UINT states[] = {TTS_RDY, TTS_WAI};
  const UINT counts[] = {0, WAKEUP_LIMIT - 1, WAKEUP_LIMIT};
  for (UINT s = 0; s < sizeof(states) / sizeof(states[0]); ++s) {
    for (UINT c = 0; c < sizeof(counts) / sizeof(counts[0]); ++c) {
      check_wakeup_limit(states[s], counts[c], FALSE);
      check_wakeup_limit(states[s], counts[c], TRUE);
    }
  }
}

void test_wakeup_count_is_bounded_for_a_running_task_with_interrupts_off(void) {
  // A handler-equivalent fixture, not an actual ISR: with interrupts off the
  // running task may queue a request for itself.
  const UINT counts[] = {0, WAKEUP_LIMIT - 1, WAKEUP_LIMIT};
  for (UINT i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
    check_wakeup_limit(TTS_RUN, counts[i], TRUE);
  }
}

void test_consuming_a_wakeup_makes_room_for_one_more_request(void) {
  ID caller = host_make_task(PRIORITY);
  ID target = host_make_task(PRIORITY + 1);
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

// A task that wakes itself with interrupts on is refused before the limit is
// checked, so a full count gives E_OBJ too, and nothing changes.
void test_a_task_cannot_wake_itself_with_interrupts_on(void) {
  const UINT counts[] = {0, WAKEUP_LIMIT};
  for (UINT i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
    host_reset();
    ID self = make_wakeup_target(TTS_RUN);
    tkmc_tcbs[self - 1].wupcnt = counts[i];
    host_snapshot_t before;
    host_take_snapshot(&before);

    TEST_ASSERT_EQUAL(E_OBJ, tk_wup_tsk(self));
    TEST_ASSERT_TRUE(host_interrupts_enabled());
    host_assert_unchanged(&before, "a refused self-wakeup changes nothing");
    host_check_kernel_queues();
  }
}
