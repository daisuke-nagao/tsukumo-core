/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Calls beyond the specification's required task-independent set that this
// kernel refuses by policy with E_CTX: the calls that create or delete an
// object, tk_clr_flg() and tk_snd_mbx(). The
// ISR state is tkmc_isr_depth, which the trap entry sets; MIE says nothing
// about it, so a task inside DI() and the boot code, which has no current task
// yet, still make these calls.
//
// Errors in the arguments that need no object come first, then E_CTX, then the
// errors that need the object (E_NOEXS, E_LIMIT, the priority of a message of a
// TA_MPRI mailbox), the same order as in the wait calls. A refusal is checked
// as test_wait_context.c does: every task, queue and object as bytes, which
// takes in a waiting task that stays waiting and a message that is not queued.
//
// The calls that the specification allows in the ISR state are called there
// too, so that a refusal that is too wide fails a test.

#include <string.h>
#include <tk/tkernel.h>
#include <unity.h>

#include "context.h"
#include "event_flag.h"
#include "fakes.h"
#include "mailbox.h"
#include "semaphore.h"
#include "task.h"

enum { PRIORITY = 4, UNUSED_ID = 5 };

static ID s_self, s_sem_waiter, s_mbx_waiter, s_dormant;
static ID s_semid, s_flgid, s_mbxid, s_pri_mbxid;
static T_MSG s_message;
static T_MSG s_received_slot;
static T_MSG *s_received = &s_received_slot;
static T_MSG_PRI s_zero_priority_message; // msgpri is 0, which is invalid.

// The stacks of the task that tk_cre_tsk() creates in a row and of one that
// stays dormant. No task of this file runs.
static UW s_stacks[2][256] __attribute__((aligned(16)));

static void never_run(INT stacd, void *exinf) {
  (void)stacd;
  (void)exinf;
}

// The running task, one object of each kind (and a second, TA_MPRI mailbox),
// and a task that is not started. With waiting set, one more task waits for the
// semaphore and one for the mailbox, so that a call that wrongly went ahead
// would wake them. The running task is s_self.
static void fresh(BOOL waiting) {
  s_self = host_make_task(PRIORITY);
  s_semid = tk_cre_sem(
      &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
  s_flgid = tk_cre_flg(&(T_CFLG){.exinf = 0, .flgatr = TA_WSGL, .iflgptn = 5});
  s_mbxid = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TFIFO});
  s_pri_mbxid = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TFIFO | TA_MPRI});
  TEST_ASSERT_GREATER_THAN(0, s_semid);
  TEST_ASSERT_GREATER_THAN(0, s_flgid);
  TEST_ASSERT_GREATER_THAN(0, s_mbxid);
  TEST_ASSERT_GREATER_THAN(0, s_pri_mbxid);
  s_dormant = tk_cre_tsk(&(T_CTSK){.exinf = 0,
                                   .tskatr = TA_HLNG | TA_USERBUF,
                                   .task = (FP)never_run,
                                   .itskpri = PRIORITY,
                                   .stksz = sizeof(s_stacks[0]),
                                   .bufptr = s_stacks[0]});
  TEST_ASSERT_GREATER_THAN(0, s_dormant);
  if (waiting) {
    s_sem_waiter = host_make_task(PRIORITY);
    s_mbx_waiter = host_make_task(PRIORITY);
    host_set_current(s_sem_waiter);
    HOST_EXPECT_WAIT(tk_wai_sem(s_semid, 1, TMO_FEVR));
    host_set_current(s_mbx_waiter);
    HOST_EXPECT_WAIT(tk_rcv_mbx(s_mbxid, &s_received, TMO_FEVR));
  }
  host_set_current(s_self);
}

static ER cre_tsk(void) {
  return tk_cre_tsk(&(T_CTSK){.exinf = 0,
                              .tskatr = TA_HLNG | TA_USERBUF,
                              .task = (FP)never_run,
                              .itskpri = PRIORITY,
                              .stksz = sizeof(s_stacks[1]),
                              .bufptr = s_stacks[1]});
}
static ER cre_sem(void) {
  return tk_cre_sem(
      &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
}
static ER cre_flg(void) {
  return tk_cre_flg(&(T_CFLG){.exinf = 0, .flgatr = TA_WSGL, .iflgptn = 0});
}
static ER cre_mbx(void) {
  return tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TFIFO});
}
static ER clr_flg(void) { return tk_clr_flg(s_flgid, 0); }
static ER del_sem(void) { return tk_del_sem(s_semid); }
static ER del_mbx(void) { return tk_del_mbx(s_mbxid); }
static ER snd_mbx(void) { return tk_snd_mbx(s_mbxid, &s_message); }

typedef struct {
  const char *label;
  ER (*call)(void);
} row_t;

static const row_t s_refused[] = {
    {"tk_cre_tsk", cre_tsk}, {"tk_cre_sem", cre_sem}, {"tk_cre_flg", cre_flg},
    {"tk_cre_mbx", cre_mbx}, {"tk_clr_flg", clr_flg}, {"tk_del_sem", del_sem},
    {"tk_del_mbx", del_mbx}, {"tk_snd_mbx", snd_mbx},
};
enum { REFUSED_COUNT = sizeof(s_refused) / sizeof(s_refused[0]) };

void test_a_call_the_isr_state_may_not_make_is_refused_without_changes(void) {
  for (UINT row = 0; row < REFUSED_COUNT; ++row) {
    for (UINT depth = 1; depth <= 2; ++depth) {
      for (UINT irq = 0; irq < 2; ++irq) {
        for (UINT waiting = 0; waiting < 2; ++waiting) {
          host_reset();
          fresh(waiting);
          tkmc_isr_depth = depth;
          memset(&s_message, 0xA5, sizeof(s_message));
          memset(&s_received_slot, 0x5A, sizeof(s_received_slot));
          s_received = &s_received_slot;
          memset(&s_zero_priority_message, 0x3C,
                 sizeof(s_zero_priority_message));
          s_message.list.next = &s_received_slot.list;
          s_message.list.prev = &s_zero_priority_message.msgque.list;
          s_received_slot.list.next = &s_zero_priority_message.msgque.list;
          s_received_slot.list.prev = &s_message.list;
          s_zero_priority_message.msgque.list.next = &s_message.list;
          s_zero_priority_message.msgque.list.prev = &s_received_slot.list;
          s_zero_priority_message.msgpri = 0;
          memset(s_stacks, 0xC3, sizeof(s_stacks));
          T_MSG message_before;
          T_MSG_PRI zero_priority_message_before;
          T_MSG received_slot_before;
          UW stacks_before[2][256];
          memcpy(&message_before, &s_message, sizeof(message_before));
          memcpy(&zero_priority_message_before, &s_zero_priority_message,
                 sizeof(zero_priority_message_before));
          memcpy(&received_slot_before, &s_received_slot,
                 sizeof(received_slot_before));
          memcpy(stacks_before, s_stacks, sizeof(stacks_before));
          T_MSG *received_before = s_received;
          host_snapshot_t before;
          host_take_snapshot(&before);
          UINT saved;
          if (!irq) {
            DI(saved);
          }
          ER result = s_refused[row].call();
          BOOL after_irq = host_interrupts_enabled();
          EI(0);
          TEST_ASSERT_EQUAL_INT_MESSAGE(E_CTX, result, s_refused[row].label);
          TEST_ASSERT_EQUAL_MESSAGE(irq, after_irq, s_refused[row].label);
          TEST_ASSERT_TRUE_MESSAGE(tkmc_in_isr(), s_refused[row].label);
          host_assert_unchanged(&before, s_refused[row].label);
          TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&message_before, &s_message,
                                           sizeof(message_before),
                                           s_refused[row].label);
          TEST_ASSERT_EQUAL_MEMORY_MESSAGE(
              &zero_priority_message_before, &s_zero_priority_message,
              sizeof(zero_priority_message_before), s_refused[row].label);
          TEST_ASSERT_EQUAL_PTR_MESSAGE(received_before, s_received,
                                        s_refused[row].label);
          TEST_ASSERT_EQUAL_MEMORY_MESSAGE(
              &received_slot_before, &s_received_slot,
              sizeof(received_slot_before), s_refused[row].label);
          TEST_ASSERT_EQUAL_MEMORY_MESSAGE(stacks_before, s_stacks,
                                           sizeof(stacks_before),
                                           s_refused[row].label);
          host_check_kernel_queues();
        }
      }
    }
  }
}

// A task inside DI() and the boot code (no current task yet) are not the ISR
// state: the same calls go through. The kernel creates its first tasks that
// way.
void test_the_same_calls_work_in_a_task_with_interrupts_off_and_at_boot(void) {
  for (UINT row = 0; row < REFUSED_COUNT; ++row) {
    for (UINT boot = 0; boot < 2; ++boot) {
      host_reset();
      fresh(FALSE);
      UINT saved = 0;
      if (boot) {
        current = NULL;
        next = NULL;
      } else {
        DI(saved);
      }
      ER result = s_refused[row].call();
      EI(0);
      TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(E_OK, result,
                                               s_refused[row].label);
      TEST_ASSERT_FALSE(tkmc_in_isr());
    }
  }
}

typedef struct {
  const char *label;
  ER (*call)(void);
  ER in_isr;  // What the call returns in the ISR state.
  ER in_task; // What it returns in a task.
} order_row_t;

static ER cre_tsk_null(void) { return tk_cre_tsk(NULL); }
static ER cre_tsk_bad_attribute(void) {
  return tk_cre_tsk(&(T_CTSK){.tskatr = TA_HLNG | TA_USERBUF | 0x40000000,
                              .task = (FP)never_run,
                              .itskpri = PRIORITY,
                              .stksz = sizeof(s_stacks[1]),
                              .bufptr = s_stacks[1]});
}
static ER cre_tsk_no_entry(void) {
  return tk_cre_tsk(&(T_CTSK){.tskatr = TA_HLNG | TA_USERBUF,
                              .task = NULL,
                              .itskpri = PRIORITY,
                              .stksz = sizeof(s_stacks[1]),
                              .bufptr = s_stacks[1]});
}
static ER cre_tsk_no_user_buffer(void) {
  return tk_cre_tsk(&(T_CTSK){.tskatr = TA_HLNG,
                              .task = (FP)never_run,
                              .itskpri = PRIORITY,
                              .stksz = sizeof(s_stacks[1]),
                              .bufptr = s_stacks[1]});
}
static ER cre_tsk_no_buffer(void) {
  return tk_cre_tsk(&(T_CTSK){.tskatr = TA_HLNG | TA_USERBUF,
                              .task = (FP)never_run,
                              .itskpri = PRIORITY,
                              .stksz = sizeof(s_stacks[1]),
                              .bufptr = NULL});
}
static ER cre_tsk_priority_0(void) {
  return tk_cre_tsk(&(T_CTSK){.tskatr = TA_HLNG | TA_USERBUF,
                              .task = (FP)never_run,
                              .itskpri = 0,
                              .stksz = sizeof(s_stacks[1]),
                              .bufptr = s_stacks[1]});
}
static ER cre_tsk_small_stack(void) {
  return tk_cre_tsk(&(T_CTSK){.tskatr = TA_HLNG | TA_USERBUF,
                              .task = (FP)never_run,
                              .itskpri = PRIORITY,
                              .stksz = 16,
                              .bufptr = s_stacks[1]});
}
// The last check of tk_cre_tsk() before the context: the end of the stack is
// not aligned to 16 bytes.
static ER cre_tsk_misaligned_stack(void) {
  return tk_cre_tsk(&(T_CTSK){.tskatr = TA_HLNG | TA_USERBUF,
                              .task = (FP)never_run,
                              .itskpri = PRIORITY,
                              .stksz = sizeof(s_stacks[1]) - 8,
                              .bufptr = s_stacks[1]});
}
static ER cre_sem_null(void) { return tk_cre_sem(NULL); }
static ER cre_sem_bad_attribute(void) {
  return tk_cre_sem(&(T_CSEM){.sematr = 0x40000000, .isemcnt = 0, .maxsem = 1});
}
static ER cre_sem_bad_count(void) {
  return tk_cre_sem(&(T_CSEM){.sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 0});
}
static ER cre_flg_null(void) { return tk_cre_flg(NULL); }
static ER cre_flg_bad_attribute(void) {
  return tk_cre_flg(&(T_CFLG){.flgatr = 0x40000000});
}
static ER cre_mbx_null(void) { return tk_cre_mbx(NULL); }
static ER cre_mbx_bad_attribute(void) {
  return tk_cre_mbx(&(T_CMBX){.mbxatr = 0x40000000});
}
static ER clr_flg_id_0(void) { return tk_clr_flg(0, 0); }
static ER clr_flg_unused(void) { return tk_clr_flg(UNUSED_ID, 0); }
static ER del_sem_id_0(void) { return tk_del_sem(0); }
static ER del_sem_unused(void) { return tk_del_sem(UNUSED_ID); }
static ER del_mbx_id_0(void) { return tk_del_mbx(0); }
static ER del_mbx_unused(void) { return tk_del_mbx(UNUSED_ID); }
static ER snd_mbx_id_0(void) { return tk_snd_mbx(0, &s_message); }
static ER snd_mbx_null(void) { return tk_snd_mbx(s_mbxid, NULL); }
static ER snd_mbx_unused(void) { return tk_snd_mbx(UNUSED_ID, &s_message); }
static ER snd_mbx_priority_0(void) {
  return tk_snd_mbx(s_pri_mbxid, &s_zero_priority_message.msgque);
}

static const order_row_t s_order[] = {
    {"tk_cre_tsk(NULL)", cre_tsk_null, E_PAR, E_PAR},
    {"tk_cre_tsk, attribute", cre_tsk_bad_attribute, E_RSATR, E_RSATR},
    {"tk_cre_tsk, no entry", cre_tsk_no_entry, E_PAR, E_PAR},
    {"tk_cre_tsk, no TA_USERBUF", cre_tsk_no_user_buffer, E_RSATR, E_RSATR},
    {"tk_cre_tsk, no buffer", cre_tsk_no_buffer, E_PAR, E_PAR},
    {"tk_cre_tsk, priority 0", cre_tsk_priority_0, E_PAR, E_PAR},
    {"tk_cre_tsk, stack too small", cre_tsk_small_stack, E_PAR, E_PAR},
    {"tk_cre_tsk, misaligned stack end", cre_tsk_misaligned_stack, E_PAR,
     E_PAR},
    {"tk_cre_sem(NULL)", cre_sem_null, E_PAR, E_PAR},
    {"tk_cre_sem, attribute", cre_sem_bad_attribute, E_RSATR, E_RSATR},
    {"tk_cre_sem, count", cre_sem_bad_count, E_PAR, E_PAR},
    {"tk_cre_flg(NULL)", cre_flg_null, E_PAR, E_PAR},
    {"tk_cre_flg, attribute", cre_flg_bad_attribute, E_RSATR, E_RSATR},
    {"tk_cre_mbx(NULL)", cre_mbx_null, E_PAR, E_PAR},
    {"tk_cre_mbx, attribute", cre_mbx_bad_attribute, E_RSATR, E_RSATR},
    {"tk_clr_flg, ID 0", clr_flg_id_0, E_ID, E_ID},
    {"tk_clr_flg, no such flag", clr_flg_unused, E_CTX, E_NOEXS},
    {"tk_del_sem, ID 0", del_sem_id_0, E_ID, E_ID},
    {"tk_del_sem, no such semaphore", del_sem_unused, E_CTX, E_NOEXS},
    {"tk_del_mbx, ID 0", del_mbx_id_0, E_ID, E_ID},
    {"tk_del_mbx, no such mailbox", del_mbx_unused, E_CTX, E_NOEXS},
    {"tk_snd_mbx, ID 0", snd_mbx_id_0, E_ID, E_ID},
    {"tk_snd_mbx(NULL)", snd_mbx_null, E_PAR, E_PAR},
    {"tk_snd_mbx, no such mailbox", snd_mbx_unused, E_CTX, E_NOEXS},
    {"tk_snd_mbx, message priority 0", snd_mbx_priority_0, E_CTX, E_PAR},
};
enum { ORDER_COUNT = sizeof(s_order) / sizeof(s_order[0]) };

void test_the_isr_check_comes_after_the_argument_checks_and_before_the_object(
    void) {
  for (UINT row = 0; row < ORDER_COUNT; ++row) {
    for (UINT isr = 0; isr < 2; ++isr) {
      host_reset();
      fresh(TRUE);
      tkmc_isr_depth = isr;
      host_snapshot_t before;
      host_take_snapshot(&before);
      ER result = s_order[row].call();
      TEST_ASSERT_EQUAL_INT_MESSAGE(isr ? s_order[row].in_isr
                                        : s_order[row].in_task,
                                    result, s_order[row].label);
      host_assert_unchanged(&before, s_order[row].label);
      host_check_kernel_queues();
    }
  }
}

// With no object of the kind left, a task gets E_LIMIT and the ISR state still
// gets E_CTX: the check does not wait for the free list.
void test_the_isr_check_comes_before_running_out_of_objects(void) {
  static const struct {
    const char *label;
    ER (*call)(void);
  } creates[] = {{"tk_cre_tsk", cre_tsk},
                 {"tk_cre_sem", cre_sem},
                 {"tk_cre_flg", cre_flg},
                 {"tk_cre_mbx", cre_mbx}};
  for (UINT row = 0; row < sizeof(creates) / sizeof(creates[0]); ++row) {
    host_reset();
    fresh(FALSE);
    ER result = E_OK;
    for (UINT n = 0; n < 64 && result != E_LIMIT; ++n) {
      result = creates[row].call();
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(E_LIMIT, result, creates[row].label);
    host_snapshot_t before;
    host_take_snapshot(&before);
    tkmc_isr_depth = 1;
    TEST_ASSERT_EQUAL_INT_MESSAGE(E_CTX, creates[row].call(),
                                  creates[row].label);
    host_assert_unchanged(&before, creates[row].label);
    tkmc_isr_depth = 0;
    TEST_ASSERT_EQUAL_INT_MESSAGE(E_LIMIT, creates[row].call(),
                                  creates[row].label);
    host_check_kernel_queues();
  }
}

// The five calls the specification requires from the task-independent portion
// and this kernel implements succeed there.
void test_the_calls_the_specification_allows_in_the_isr_state_succeed(void) {
  host_reset();
  fresh(TRUE);
  tkmc_isr_depth = 1;
  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(s_flgid, 2));
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(s_semid, 1));
  TEST_ASSERT_EQUAL(E_OK, tk_rel_wai(s_mbx_waiter));
  TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(s_self));
  TEST_ASSERT_EQUAL(E_OK, tk_sta_tsk(s_dormant, 0));
  host_check_kernel_queues();
}
