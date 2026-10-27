/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

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
  memset(tcb->winfo.dummy, 0xa5, sizeof(tcb->winfo.dummy));

  TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(id, 17));
  TEST_ASSERT_EQUAL_UINT(0, tcb->wupcnt);
  TEST_ASSERT_EQUAL(E_OK, tcb->wupcause);
  TEST_ASSERT_EQUAL_UINT(0, tcb->tskwait);
  TEST_ASSERT_EQUAL_UINT(0, tcb->delay_ticks);
  TEST_ASSERT_TRUE(tkmc_list_empty(&tcb->winfo.wait_queue));
  for (UINT i = 0; i < sizeof(tcb->winfo.dummy); ++i) {
    TEST_ASSERT_EQUAL_UINT8(0, tcb->winfo.dummy[i]);
  }
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
