/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
#include <tk/tkernel.h>
#include <unity.h>

#include "putstring.h"
#include "tasks.h"
#include "userstack.h"

// Function prototypes for sem_tsk_1 and sem_tsk_2
static void sem_tsk_1(INT stacd, void *exinf);
static void sem_tsk_2(INT stacd, void *exinf);
static void mailbox_tests(void);
static void mbx_receiver_task(INT stacd, void *exinf);
static void mbx_delete_receiver_task(INT stacd, void *exinf);
static void mbx_timeout_receiver_task(INT stacd, void *exinf);
static void mbx_wait_hi_task(INT stacd, void *exinf);
static void mbx_wait_lo_task(INT stacd, void *exinf);

static ID s_flgid = 0; // Global event flag ID
static ID s_mbx_flgid = 0;

typedef struct MailboxMessage {
  T_MSG hdr;
  INT value;
} MailboxMessage;

typedef struct MailboxPriorityMessage {
  T_MSG_PRI hdr;
  INT value;
} MailboxPriorityMessage;

static MailboxMessage s_mbx_wait_msg;
static MailboxMessage s_mbx_wait_pri_msg1;
static MailboxMessage s_mbx_wait_pri_msg2;
static T_MSG *s_mbx_hi_expected;
static T_MSG *s_mbx_lo_expected;
static INT s_mbx_wait_order[2];
static INT s_mbx_wait_order_count = 0;

/// Entry point for TASK3
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
void task4(INT stacd, void *exinf) {
  putstring("task4 start\n");

  // Create an event flag to synchronize sem_tsk_1 and
  // sem_tsk_2 with task4
  T_CFLG pk_cflg = {
      .exinf = NULL,      // No extended information
      .flgatr = TA_TFIFO, // FIFO order
      .iflgptn = 0,       // Initial flag pattern
  };
  ID flgid = tk_cre_flg(&pk_cflg);
  TEST_ASSERT_GREATER_THAN(E_OK, flgid);
  s_flgid = flgid;

  // Create a semaphore to synchronize access between sem_tsk_1 and
  // sem_tsk_2
  T_CSEM pk_csem = {
      .exinf = NULL,      // No extended information
      .sematr = TA_TFIFO, // FIFO order
      .isemcnt = 3,       // Initial count
      .maxsem = 3,        // Maximum count
  };

  ID semid = tk_cre_sem(&pk_csem);
  TEST_ASSERT_GREATER_THAN(E_OK, semid);

  // Create and start sem_tsk_1
  ID sem_tsk_1_id = tk_cre_tsk(&(T_CTSK){.exinf = NULL,
                                         .tskatr = TA_HLNG | TA_USERBUF,
                                         .task = sem_tsk_1,
                                         .itskpri = 3,
                                         .stksz = sizeof(g_stack1_1024B),
                                         .bufptr = g_stack1_1024B});
  TEST_ASSERT_GREATER_THAN(E_OK, sem_tsk_1_id);

  ER ercd = tk_sta_tsk(sem_tsk_1_id, semid);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  // Create and start sem_tsk_2
  ID sem_tsk_2_id = tk_cre_tsk(&(T_CTSK){.exinf = NULL,
                                         .tskatr = TA_HLNG | TA_USERBUF,
                                         .task = sem_tsk_2,
                                         .itskpri = 3,
                                         .stksz = sizeof(g_stack2_1024B),
                                         .bufptr = g_stack2_1024B});
  TEST_ASSERT_GREATER_THAN(E_OK, sem_tsk_2_id);

  ercd = tk_sta_tsk(sem_tsk_2_id, semid);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  // Wait for sem_tsk_1 and sem_tsk_2 to signal
  // completion via the event flag
  UINT flgptn;
  ercd = tk_wai_flg(flgid, 0x0003, TWF_ANDW | TWF_CLR, &flgptn, TMO_FEVR);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_EQUAL(0x0003, flgptn);

  tk_dly_tsk(100); // Delay to allow other tasks to finish

  //! @todo delete event flag and semaphore
  //! @todo deleting function is note implemented for event flag and semaphore
  ercd = tk_del_sem(semid); // Delete the semaphore
  TEST_ASSERT_EQUAL(E_OK, ercd);

  mailbox_tests();

  ID next_task_id = get_tskid(stacd + 1);
  if (next_task_id > 0) {
    // Start the next task if it exists
    ercd = tk_sta_tsk(next_task_id, stacd + 1); // Start the next task
    TEST_ASSERT_EQUAL(E_OK, ercd);
  }

  putstring("task4 finish\n");

  // Terminate task4
  tk_exd_tsk();
}

/// Entry point for TASK3_A
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
static void sem_tsk_1(INT stacd, void *exinf) {
  putstring("sem_tsk_1 start\n");

  ID semid = (ID)stacd;

  for (int i = 0; i < 5; ++i) {
    // Attempt to acquire the semaphore
    ER ercd = tk_wai_sem(semid, 3, TMO_FEVR);
    TEST_ASSERT_EQUAL(E_OK, ercd);

    // Simulate some work
    tk_dly_tsk(50);

    // Release the semaphore
    ercd = tk_sig_sem(semid, 1);
    TEST_ASSERT_EQUAL(E_OK, ercd);
    ercd = tk_sig_sem(semid, 1);
    TEST_ASSERT_EQUAL(E_OK, ercd);
    ercd = tk_sig_sem(semid, 1);
    TEST_ASSERT_EQUAL(E_OK, ercd);

    tk_dly_tsk(50);
  }

  // Signal completion by setting the event flag
  tk_set_flg(s_flgid, 0x0001); // Set the first bit of the event flag

  putstring("sem_tsk_1 finish\n");

  // Exit task
  tk_exd_tsk();
}

/// Entry point for TASK3_B
/// @param stacd Start code (startup information)
/// @param exinf Extended information passed at task creation
static void sem_tsk_2(INT stacd, void *exinf) {
  putstring("sem_tsk_2 start\n");

  ID semid = (ID)stacd;

  for (int i = 0; i < 5; ++i) {
    // Attempt to acquire the semaphore
    ER ercd = tk_wai_sem(semid, 3, TMO_FEVR);
    TEST_ASSERT_EQUAL(E_OK, ercd);

    // Simulate some work
    tk_dly_tsk(50);

    // Release the semaphore
    ercd = tk_sig_sem(semid, 1);
    TEST_ASSERT_EQUAL(E_OK, ercd);
    ercd = tk_sig_sem(semid, 1);
    TEST_ASSERT_EQUAL(E_OK, ercd);
    ercd = tk_sig_sem(semid, 1);
    TEST_ASSERT_EQUAL(E_OK, ercd);

    tk_dly_tsk(50);
  }

  // Signal completion by setting the event flag
  tk_set_flg(s_flgid, 0x0002); // Set the second bit of the event flag

  putstring("sem_tsk_2 finish\n");

  // Exit task
  tk_exd_tsk();
}

static ID create_mailbox_task(FP task, PRI priority, void *stack,
                              SZ stack_size) {
  ID tskid = tk_cre_tsk(&(T_CTSK){.exinf = NULL,
                                  .tskatr = TA_HLNG | TA_USERBUF,
                                  .task = task,
                                  .itskpri = priority,
                                  .stksz = stack_size,
                                  .bufptr = stack});
  TEST_ASSERT_GREATER_THAN(E_OK, tskid);
  return tskid;
}

static void wait_mailbox_flags(UINT pattern) {
  UINT flgptn = 0;
  ER ercd = tk_wai_flg(s_mbx_flgid, pattern, TWF_ANDW | TWF_BITCLR, &flgptn,
                       TMO_FEVR);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_EQUAL(pattern, flgptn & pattern);
}

static void mailbox_test_fifo_queue(void) {
  ID mbxid =
      tk_cre_mbx(&(T_CMBX){.exinf = NULL, .mbxatr = TA_TFIFO | TA_MFIFO});
  TEST_ASSERT_GREATER_THAN(E_OK, mbxid);

  MailboxMessage msg1 = {.value = 1};
  MailboxMessage msg2 = {.value = 2};
  T_MSG *pk_msg = NULL;

  ER ercd = tk_snd_mbx(mbxid, &msg1.hdr);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  ercd = tk_snd_mbx(mbxid, &msg2.hdr);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  ercd = tk_rcv_mbx(mbxid, &pk_msg, TMO_POL);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_EQUAL_PTR(&msg1.hdr, pk_msg);

  ercd = tk_rcv_mbx(mbxid, &pk_msg, TMO_POL);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_EQUAL_PTR(&msg2.hdr, pk_msg);

  ercd = tk_rcv_mbx(mbxid, &pk_msg, TMO_POL);
  TEST_ASSERT_EQUAL(E_TMOUT, ercd);

  ercd = tk_del_mbx(mbxid);
  TEST_ASSERT_EQUAL(E_OK, ercd);
}

static void mailbox_test_priority_queue(void) {
  ID mbxid = tk_cre_mbx(&(T_CMBX){.exinf = NULL, .mbxatr = TA_TFIFO | TA_MPRI});
  TEST_ASSERT_GREATER_THAN(E_OK, mbxid);

  MailboxPriorityMessage low = {.hdr.msgpri = 9, .value = 9};
  MailboxPriorityMessage high1 = {.hdr.msgpri = 1, .value = 1};
  MailboxPriorityMessage mid = {.hdr.msgpri = 5, .value = 5};
  MailboxPriorityMessage high2 = {.hdr.msgpri = 1, .value = 11};
  T_MSG *pk_msg = NULL;

  ER ercd = tk_snd_mbx(mbxid, (T_MSG *)&low.hdr);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  ercd = tk_snd_mbx(mbxid, (T_MSG *)&high1.hdr);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  ercd = tk_snd_mbx(mbxid, (T_MSG *)&mid.hdr);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  ercd = tk_snd_mbx(mbxid, (T_MSG *)&high2.hdr);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  ercd = tk_rcv_mbx(mbxid, &pk_msg, TMO_POL);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_EQUAL_PTR((T_MSG *)&high1.hdr, pk_msg);

  ercd = tk_rcv_mbx(mbxid, &pk_msg, TMO_POL);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_EQUAL_PTR((T_MSG *)&high2.hdr, pk_msg);

  ercd = tk_rcv_mbx(mbxid, &pk_msg, TMO_POL);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_EQUAL_PTR((T_MSG *)&mid.hdr, pk_msg);

  ercd = tk_rcv_mbx(mbxid, &pk_msg, TMO_POL);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_EQUAL_PTR((T_MSG *)&low.hdr, pk_msg);

  MailboxPriorityMessage invalid = {.hdr.msgpri = 0};
  ercd = tk_snd_mbx(mbxid, (T_MSG *)&invalid.hdr);
  TEST_ASSERT_EQUAL(E_PAR, ercd);

  ercd = tk_del_mbx(mbxid);
  TEST_ASSERT_EQUAL(E_OK, ercd);
}

static void mailbox_test_direct_delivery(void) {
  ID mbxid =
      tk_cre_mbx(&(T_CMBX){.exinf = NULL, .mbxatr = TA_TFIFO | TA_MFIFO});
  TEST_ASSERT_GREATER_THAN(E_OK, mbxid);

  s_mbx_wait_msg.value = 100;

  ID tskid = create_mailbox_task((FP)mbx_receiver_task, 2, g_stack1_1024B,
                                 sizeof(g_stack1_1024B));
  ER ercd = tk_sta_tsk(tskid, mbxid);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  ercd = tk_snd_mbx(mbxid, &s_mbx_wait_msg.hdr);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  wait_mailbox_flags(0x0001);

  ercd = tk_del_mbx(mbxid);
  TEST_ASSERT_EQUAL(E_OK, ercd);
}

static void mailbox_run_wait_order_test(ATR mbxatr, T_MSG *hi_expected,
                                        T_MSG *lo_expected, INT first,
                                        INT second) {
  ID mbxid = tk_cre_mbx(&(T_CMBX){.exinf = NULL, .mbxatr = mbxatr});
  TEST_ASSERT_GREATER_THAN(E_OK, mbxid);

  s_mbx_hi_expected = hi_expected;
  s_mbx_lo_expected = lo_expected;
  s_mbx_wait_order_count = 0;
  s_mbx_wait_order[0] = 0;
  s_mbx_wait_order[1] = 0;

  ID lo_tskid = create_mailbox_task((FP)mbx_wait_lo_task, 2, g_stack1_1024B,
                                    sizeof(g_stack1_1024B));
  ER ercd = tk_sta_tsk(lo_tskid, mbxid);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  ID hi_tskid = create_mailbox_task((FP)mbx_wait_hi_task, 1, g_stack2_1024B,
                                    sizeof(g_stack2_1024B));
  ercd = tk_sta_tsk(hi_tskid, mbxid);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  ercd = tk_snd_mbx(mbxid, &s_mbx_wait_pri_msg1.hdr);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  ercd = tk_snd_mbx(mbxid, &s_mbx_wait_pri_msg2.hdr);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  wait_mailbox_flags(0x0018);
  TEST_ASSERT_EQUAL(2, s_mbx_wait_order_count);
  TEST_ASSERT_EQUAL(first, s_mbx_wait_order[0]);
  TEST_ASSERT_EQUAL(second, s_mbx_wait_order[1]);

  ercd = tk_del_mbx(mbxid);
  TEST_ASSERT_EQUAL(E_OK, ercd);
}

static void mailbox_test_wait_queues(void) {
  s_mbx_wait_pri_msg1.value = 1;
  s_mbx_wait_pri_msg2.value = 2;

  mailbox_run_wait_order_test(TA_TFIFO | TA_MFIFO, &s_mbx_wait_pri_msg2.hdr,
                              &s_mbx_wait_pri_msg1.hdr, 2, 1);
  mailbox_run_wait_order_test(TA_TPRI | TA_MFIFO, &s_mbx_wait_pri_msg1.hdr,
                              &s_mbx_wait_pri_msg2.hdr, 1, 2);
}

static void mailbox_test_delete_waiter(void) {
  ID mbxid =
      tk_cre_mbx(&(T_CMBX){.exinf = NULL, .mbxatr = TA_TFIFO | TA_MFIFO});
  TEST_ASSERT_GREATER_THAN(E_OK, mbxid);

  ID tskid = create_mailbox_task((FP)mbx_delete_receiver_task, 2,
                                 g_stack1_1024B, sizeof(g_stack1_1024B));
  ER ercd = tk_sta_tsk(tskid, mbxid);
  TEST_ASSERT_EQUAL(E_OK, ercd);

  ercd = tk_del_mbx(mbxid);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  wait_mailbox_flags(0x0002);
}

static void mailbox_test_timeout_cleanup(void) {
  ID mbxid =
      tk_cre_mbx(&(T_CMBX){.exinf = NULL, .mbxatr = TA_TFIFO | TA_MFIFO});
  TEST_ASSERT_GREATER_THAN(E_OK, mbxid);

  ID tskid = create_mailbox_task((FP)mbx_timeout_receiver_task, 2,
                                 g_stack1_1024B, sizeof(g_stack1_1024B));
  ER ercd = tk_sta_tsk(tskid, mbxid);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  wait_mailbox_flags(0x0004);

  MailboxMessage msg = {.value = 77};
  T_MSG *pk_msg = NULL;

  ercd = tk_snd_mbx(mbxid, &msg.hdr);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  ercd = tk_rcv_mbx(mbxid, &pk_msg, TMO_POL);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_EQUAL_PTR(&msg.hdr, pk_msg);

  ercd = tk_del_mbx(mbxid);
  TEST_ASSERT_EQUAL(E_OK, ercd);
}

static void mailbox_tests(void) {
  putstring("mailbox_tests start\n");

  ID flgid = tk_cre_flg(
      &(T_CFLG){.exinf = NULL, .flgatr = TA_TFIFO | TA_WMUL, .iflgptn = 0});
  TEST_ASSERT_GREATER_THAN(E_OK, flgid);
  s_mbx_flgid = flgid;

  mailbox_test_fifo_queue();
  mailbox_test_priority_queue();
  mailbox_test_direct_delivery();
  mailbox_test_wait_queues();
  mailbox_test_delete_waiter();
  mailbox_test_timeout_cleanup();

  putstring("mailbox_tests finish\n");
}

static void mbx_receiver_task(INT stacd, void *exinf) {
  T_MSG *pk_msg = NULL;
  ER ercd = tk_rcv_mbx((ID)stacd, &pk_msg, TMO_FEVR);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_EQUAL_PTR(&s_mbx_wait_msg.hdr, pk_msg);

  tk_set_flg(s_mbx_flgid, 0x0001);
  tk_exd_tsk();
}

static void mbx_delete_receiver_task(INT stacd, void *exinf) {
  T_MSG *pk_msg = NULL;
  ER ercd = tk_rcv_mbx((ID)stacd, &pk_msg, TMO_FEVR);
  TEST_ASSERT_EQUAL(E_DLT, ercd);

  tk_set_flg(s_mbx_flgid, 0x0002);
  tk_exd_tsk();
}

static void mbx_timeout_receiver_task(INT stacd, void *exinf) {
  T_MSG *pk_msg = NULL;
  ER ercd = tk_rcv_mbx((ID)stacd, &pk_msg, 50);
  TEST_ASSERT_EQUAL(E_TMOUT, ercd);
  TEST_ASSERT_NULL(pk_msg);

  tk_set_flg(s_mbx_flgid, 0x0004);
  tk_exd_tsk();
}

static void mbx_wait_hi_task(INT stacd, void *exinf) {
  T_MSG *pk_msg = NULL;
  ER ercd = tk_rcv_mbx((ID)stacd, &pk_msg, TMO_FEVR);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_EQUAL_PTR(s_mbx_hi_expected, pk_msg);

  s_mbx_wait_order[s_mbx_wait_order_count++] = 1;
  tk_set_flg(s_mbx_flgid, 0x0008);
  tk_exd_tsk();
}

static void mbx_wait_lo_task(INT stacd, void *exinf) {
  T_MSG *pk_msg = NULL;
  ER ercd = tk_rcv_mbx((ID)stacd, &pk_msg, TMO_FEVR);
  TEST_ASSERT_EQUAL(E_OK, ercd);
  TEST_ASSERT_EQUAL_PTR(s_mbx_lo_expected, pk_msg);

  s_mbx_wait_order[s_mbx_wait_order_count++] = 2;
  tk_set_flg(s_mbx_flgid, 0x0010);
  tk_exd_tsk();
}
