/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// What tk_cre_tsk() refuses on the target. The rules are checked on the PC as
// well (test/host/test_task.c); what only this suite shows is the 32 bit
// address arithmetic: on the PC the stack end that wraps around is a 64 bit
// wrap.
//
// A refusal is E_PAR, and no task is made, so the next free TCB is still the
// one that was next. Nothing here starts a task: tk_cre_tsk() writes nothing to
// the stack, and the frame is the business of tk_sta_tsk().
//
// Every case breaks one rule only, so that a case does not pass because of
// another rule. There is one case per input: a failed assert ends its case, and
// the cases after it would be hidden in a case that held them all. The kernel
// has no way to delete a task that was never started, so a task that a case
// creates takes a TCB until the program ends; only the last case creates one.

#include <tk/tkernel.h>
#include <unity.h>

#include "runner.h"
#include "task.h"

extern tkmc_list_head tkmc_free_tcb;

enum { FRAME_BYTES = 128, STACK_ALIGN = 16 };

// -nostdinc rules out <stdint.h>; this is the integer type as wide as a
// pointer.
typedef __UINTPTR_TYPE__ addr_t;
#define ADDR_MAX ((addr_t) - 1)

static _Alignas(STACK_ALIGN) UW s_stack[256];

static void entry(INT stacd, void *exinf) {
  (void)stacd;
  (void)exinf;
}

static T_CTSK packet(PRI itskpri, void *bufptr, SZ stksz) {
  return (T_CTSK){.exinf = NULL,
                  .tskatr = TA_HLNG | TA_USERBUF,
                  .task = (FP)entry,
                  .itskpri = itskpri,
                  .stksz = stksz,
                  .bufptr = bufptr};
}

static void check_refused(const T_CTSK *pk) {
  tkmc_list_head *next_free = tkmc_free_tcb.next;
  TEST_ASSERT_EQUAL(E_PAR, tk_cre_tsk(pk));
  TEST_ASSERT_EQUAL_PTR(next_free, tkmc_free_tcb.next);
}

static void priority_zero(void) {
  T_CTSK pk = packet(0, s_stack, sizeof(s_stack));
  check_refused(&pk);
}

static void priority_above_the_lowest(void) {
  T_CTSK pk = packet(CFN_MAX_PRI + 1, s_stack, sizeof(s_stack));
  check_refused(&pk);
}

static void null_entry(void) {
  T_CTSK pk = packet(2, s_stack, sizeof(s_stack));
  pk.task = NULL;
  check_refused(&pk);
}

static void null_buffer(void) {
  // 256 bytes from address 0 end on a boundary and hold the frame.
  T_CTSK pk = packet(2, NULL, 256);
  check_refused(&pk);
}

static void size_16(void) {
  T_CTSK pk = packet(2, s_stack, 16);
  check_refused(&pk);
}

static void size_112(void) {
  T_CTSK pk = packet(2, s_stack, 112);
  check_refused(&pk);
}

// The frame fits exactly, and nothing is left for the task.
static void size_of_the_frame(void) {
  T_CTSK pk = packet(2, s_stack, FRAME_BYTES);
  check_refused(&pk);
}

// The start is on a boundary, so the rule that matters is the one about the
// end.
// From a low address the end of a negative size does not wrap and is aligned,
// so only the size rule can refuse it. (From a RAM address the overflow check
// refuses it too.) The address is not used as memory.
static void negative_size_at_a_low_address(void) {
  T_CTSK pk = packet(2, (void *)0x1000, -2147483647 - 1);
  check_refused(&pk);
}

static void end_not_on_a_16_byte_boundary(void) {
  T_CTSK pk = packet(2, s_stack, 520);
  check_refused(&pk);
}

// A stack from 256 bytes below the top of the 32 bit address space ends at
// 2^32, which is 0 after the wrap; it is aligned and large enough. The address
// is not used as memory.
static void end_wrapping_around_the_address_space(void) {
  T_CTSK pk = packet(2, (void *)(addr_t)(ADDR_MAX - 255), 256);
  check_refused(&pk);
}

static void smallest_stack_takes_the_next_free_slot(void) {
  ID expected = tkmc_list_first_entry(&tkmc_free_tcb, TCB, head)->tskid;

  // The refusals above, and one more here, left the free list as it was.
  T_CTSK refused = packet(0, s_stack, sizeof(s_stack));
  check_refused(&refused);

  T_CTSK pk = packet(2, s_stack, FRAME_BYTES + STACK_ALIGN);
  TEST_ASSERT_EQUAL(expected, tk_cre_tsk(&pk));
}

static const test_case_t s_cases[] = {
    {"priority_zero", priority_zero},
    {"priority_above_the_lowest", priority_above_the_lowest},
    {"null_entry", null_entry},
    {"null_buffer", null_buffer},
    {"size_16", size_16},
    {"size_112", size_112},
    {"size_of_the_frame", size_of_the_frame},
    {"negative_size_at_a_low_address", negative_size_at_a_low_address},
    {"end_not_on_a_16_byte_boundary", end_not_on_a_16_byte_boundary},
    {"end_wrapping_around_the_address_space",
     end_wrapping_around_the_address_space},
    {"smallest_stack_takes_the_next_free_slot",
     smallest_stack_takes_the_next_free_slot},
};

TEST_SUITE_MAIN("task-create", s_cases, 5000)
