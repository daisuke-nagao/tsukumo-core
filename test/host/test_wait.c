/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// What the kernel's queues and task states look like when a task blocks and
// when it is woken, which is everything that happens before a task is switched
// back in. On the target the woken task then runs the rest of its blocking
// call (it reads the wake-up cause and the result); that part cannot run on the
// PC, so the values a woken task returns are checked by the QEMU tests.
//
// After every step the tests check the kernel's ready and timer queues as a
// whole (host_check_kernel_queues) and that interrupts are back on, so a step
// that leaves a task on two queues or returns with interrupts off fails even
// when the test never looked at that task.
//
// A wait that ends by a time-out leaves nothing of the wait behind: the task is
// off the wait queue of the object too, or a later signal would make it ready a
// second time. The tests that follow a time-out with a signal check that.

#include <tk/tkernel.h>
#include <unity.h>

#include "asm/rv32/address.h"
#include "event_flag.h"
#include "fakes.h"
#include "mailbox.h"
#include "semaphore.h"
#include "task.h"

enum {
  HIGH_PRIORITY = 4,
  LOW_PRIORITY = 5,
  MSIP = CLINT_BASE_ADDRESS + CLINT_MSIP_OFFSET
};

static BOOL in_queue(const tkmc_list_head *head, const tkmc_list_head *node) {
  for (const tkmc_list_head *pos = head->next; pos != head; pos = pos->next) {
    if (pos == node) {
      return TRUE;
    }
  }
  return FALSE;
}

static TCB *tcb_of(ID tskid) { return &tkmc_tcbs[tskid - 1]; }

static void check_kernel_state(void) {
  TEST_ASSERT_TRUE_MESSAGE(host_interrupts_enabled(),
                           "a kernel call returned with interrupts off");
  host_check_kernel_queues();
}

static ID create_semaphore(INT initial) {
  ID semid = tk_cre_sem(&(T_CSEM){
      .exinf = 0, .sematr = TA_TFIFO, .isemcnt = initial, .maxsem = 1});
  TEST_ASSERT_GREATER_THAN(0, semid);
  return semid;
}

// A task that waits for a semaphore blocks, leaves the ready queue and joins
// the semaphore's wait queue, and the other task is the one to run next. A
// signal from another task takes it off the wait queue and makes it ready.
void test_semaphore_waiter_blocks_and_a_signal_wakes_it(void) {
  ID waiter = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  ID semid = create_semaphore(0);
  SEMCB *semcb = &tkmc_semcbs[semid - 1];
  TCB *tcb = tcb_of(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTW_SEM, tcb->tskwait);
  TEST_ASSERT_TRUE(in_queue(&semcb->wait_queue, &tcb->winfo.wait_queue));
  TEST_ASSERT_TRUE(tkmc_list_empty(&tcb->head)); // On no ready or timer queue.
  TEST_ASSERT_EQUAL_PTR(tcb_of(other), next);
  check_kernel_state();

  host_set_current(other);
  // A cause that is not E_OK, so E_OK afterwards shows the kernel wrote it.
  tcb->wupcause = E_RLWAI;
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  TEST_ASSERT_EQUAL(E_OK, tcb->wupcause);
  TEST_ASSERT_TRUE(tkmc_list_empty(&semcb->wait_queue));
  TEST_ASSERT_TRUE(in_queue(&tkmc_ready_queue[HIGH_PRIORITY - 1], &tcb->head));
  // The waiter took the unit that was signalled.
  TEST_ASSERT_EQUAL(0, semcb->semcnt);
  check_kernel_state();
}

// A wait with a time limit ends when the limit has run out: not a tick before,
// and the timer interrupt then makes the task ready with a time-out cause and
// takes it off the timer queue.
void test_semaphore_wait_ends_when_the_time_limit_runs_out(void) {
  ID waiter = host_make_task(HIGH_PRIORITY);
  ID semid = create_semaphore(0);
  SEMCB *semcb = &tkmc_semcbs[semid - 1];
  TCB *tcb = tcb_of(waiter);

  host_set_current(waiter);
  // 50 ms is 5 ticks, plus one because the first tick comes at any time.
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 50));
  TEST_ASSERT_EQUAL_UINT(TTW_SEM, tcb->tskwait);
  TEST_ASSERT_TRUE(in_queue(&tkmc_timer_queue, &tcb->head));
  check_kernel_state();

  host_tick(5);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb->tskstat);
  host_tick(1);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  TEST_ASSERT_EQUAL(E_TMOUT, tcb->wupcause);
  TEST_ASSERT_TRUE(in_queue(&tkmc_ready_queue[HIGH_PRIORITY - 1], &tcb->head));
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  TEST_ASSERT_TRUE(tkmc_list_empty(&semcb->wait_queue));
  check_kernel_state();
}

// tk_rel_wai() takes a sleeping task out of its wait and makes it ready, with a
// release cause that tells it the wait did not end normally.
void test_release_wait_wakes_a_sleeping_task(void) {
  ID sleeper = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  TCB *tcb = tcb_of(sleeper);

  host_set_current(sleeper);
  HOST_EXPECT_WAIT(tk_slp_tsk(TMO_FEVR));
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTW_SLP, tcb->tskwait);
  TEST_ASSERT_EQUAL_PTR(tcb_of(other), next);
  check_kernel_state();

  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_rel_wai(sleeper));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  TEST_ASSERT_EQUAL(E_RLWAI, tcb->wupcause);
  TEST_ASSERT_TRUE(in_queue(&tkmc_ready_queue[HIGH_PRIORITY - 1], &tcb->head));
  check_kernel_state();
}

// tk_rel_wai() on a task that waits for a semaphore with a time limit takes it
// off the semaphore's wait queue and the timer queue, so neither a later signal
// nor the time limit can wake it a second time.
void test_release_wait_takes_a_timed_semaphore_waiter_off_both_queues(void) {
  ID waiter = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  ID semid = create_semaphore(0);
  SEMCB *semcb = &tkmc_semcbs[semid - 1];
  TCB *tcb = tcb_of(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 50));
  TEST_ASSERT_TRUE(in_queue(&semcb->wait_queue, &tcb->winfo.wait_queue));
  TEST_ASSERT_TRUE(in_queue(&tkmc_timer_queue, &tcb->head));

  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_rel_wai(waiter));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  TEST_ASSERT_EQUAL(E_RLWAI, tcb->wupcause);
  TEST_ASSERT_TRUE(tkmc_list_empty(&semcb->wait_queue));
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  TEST_ASSERT_TRUE(in_queue(&tkmc_ready_queue[HIGH_PRIORITY - 1], &tcb->head));
  check_kernel_state();

  // The time limit has nothing left to act on.
  host_tick(10);
  TEST_ASSERT_EQUAL(E_RLWAI, tcb->wupcause);
  check_kernel_state();
}

// A wake-up request queued while a task waits for a semaphore is still there
// after tk_rel_wai() ends that wait, and the task's next sleep uses it up.
void test_release_wait_keeps_a_queued_wakeup(void) {
  ID waiter = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  ID semid = create_semaphore(0);
  TCB *tcb = tcb_of(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));

  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(waiter));
  TEST_ASSERT_EQUAL(E_OK, tk_rel_wai(waiter));
  TEST_ASSERT_EQUAL(E_RLWAI, tcb->wupcause);
  TEST_ASSERT_EQUAL_UINT(1, tcb->wupcnt);
  check_kernel_state();

  // Switch to the released task, as tk_wai_sem() would leave it after reading
  // the cause, and let it sleep.
  tcb->wupcause = E_OK;
  tcb_of(other)->tskstat = TTS_RDY;
  host_set_current(waiter);
  TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(TMO_POL));
  TEST_ASSERT_EQUAL_UINT(0, tcb->wupcnt);
  check_kernel_state();
}

// A signal that wakes a task with a time limit also takes it off the timer
// queue, or the time limit would later wake the task, which is running by then.
void test_a_signal_disarms_the_time_limit_of_its_waiter(void) {
  ID waiter = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  ID semid = create_semaphore(0);
  TCB *tcb = tcb_of(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 50));

  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  check_kernel_state();

  host_tick(10);
  TEST_ASSERT_EQUAL(E_OK, tcb->wupcause);
  check_kernel_state();
}

// The kernel asks for a context switch by writing 1 to the machine software
// interrupt register of the machine timer block (CLINT). A wrong address or
// value would leave a woken task waiting for a switch that never comes.
void test_waking_a_higher_priority_task_requests_a_context_switch(void) {
  ID waiter = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  ID semid = create_semaphore(0);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));

  host_set_current(other);
  UINT before = host_write_count(MSIP);
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL_UINT(before + 1, host_write_count(MSIP));
  TEST_ASSERT_EQUAL_UINT(1, host_last_write(MSIP));
  check_kernel_state();
}

// Waking a task that has a lower priority than the running one needs no switch.
void test_waking_a_lower_priority_task_requests_no_context_switch(void) {
  ID waiter = host_make_task(LOW_PRIORITY);
  ID other = host_make_task(HIGH_PRIORITY);
  ID semid = create_semaphore(0);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));

  host_set_current(other);
  UINT before = host_write_count(MSIP);
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL_UINT(before, host_write_count(MSIP));
  check_kernel_state();
}

// A wait with a time limit lasts a whole number of ticks: the limit in
// milliseconds rounded up, plus one tick because the first tick comes at any
// time after the wait began. A wait may be longer than asked, never shorter.
static void expect_time_out_after(TMO limit_ms, UINT ticks) {
  host_reset();
  ID waiter = host_make_task(HIGH_PRIORITY);
  ID semid = create_semaphore(0);
  TCB *tcb = tcb_of(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, limit_ms));
  host_tick(ticks - 1);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_WAI, tcb->tskstat,
                                 "the wait ended before its time limit");
  host_tick(1);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_RDY, tcb->tskstat,
                                 "the wait outlasted its time limit");
  check_kernel_state();
}

void test_a_time_limit_is_rounded_up_to_whole_ticks_plus_one(void) {
  expect_time_out_after(1, 2);
  expect_time_out_after(10, 2);
  expect_time_out_after(11, 3);
  expect_time_out_after(50, 6);
  expect_time_out_after(55, 7);
}

// Several tasks can be on the timer queue at once, and each one times out on
// its own tick: one that expires does not hold up or hurry up the others, and
// two that expire together are both woken.
void test_tasks_on_the_timer_queue_time_out_each_on_its_own_tick(void) {
  ID early = host_make_task(HIGH_PRIORITY);
  ID late = host_make_task(HIGH_PRIORITY);
  ID twin = host_make_task(HIGH_PRIORITY);
  ID semid = create_semaphore(0);

  host_set_current(early);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 10)); // 2 ticks
  host_set_current(late);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 30)); // 4 ticks
  host_set_current(twin);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 10)); // 2 ticks
  check_kernel_state();

  host_tick(1);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb_of(early)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb_of(late)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb_of(twin)->tskstat);
  host_tick(1);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb_of(early)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb_of(twin)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb_of(late)->tskstat);
  check_kernel_state();
  host_tick(1);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb_of(late)->tskstat);
  host_tick(1);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb_of(late)->tskstat);
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  check_kernel_state();
}

// When a time-out makes a task ready, the kernel picks the task to run next and
// asks for a switch only if it is not the running one.
void test_a_time_out_requests_a_switch_only_to_a_higher_priority_task(void) {
  ID waiter = host_make_task(HIGH_PRIORITY);
  ID runner = host_make_task(LOW_PRIORITY);
  ID semid = create_semaphore(0);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 10)); // 2 ticks
  host_set_current(runner);
  UINT before = host_write_count(MSIP);
  host_tick(1);
  TEST_ASSERT_EQUAL_UINT(before, host_write_count(MSIP));
  host_tick(1);
  TEST_ASSERT_EQUAL_PTR(tcb_of(waiter), next);
  TEST_ASSERT_EQUAL_UINT(before + 1, host_write_count(MSIP));
  TEST_ASSERT_EQUAL_UINT(1, host_last_write(MSIP));
  check_kernel_state();
}

void test_a_time_out_of_a_lower_priority_task_requests_no_switch(void) {
  ID waiter = host_make_task(LOW_PRIORITY);
  ID runner = host_make_task(HIGH_PRIORITY);
  ID semid = create_semaphore(0);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 10)); // 2 ticks
  host_set_current(runner);
  UINT before = host_write_count(MSIP);
  host_tick(2);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb_of(waiter)->tskstat);
  TEST_ASSERT_EQUAL_PTR(tcb_of(runner), next);
  TEST_ASSERT_EQUAL_UINT(before, host_write_count(MSIP));
  check_kernel_state();
}

// A timed wait records what the task waits for (sleeping, delay, semaphore).
// The kernel tells the waits apart by this field; what each kind lets wake the
// task is not tested here.
void test_timed_waits_record_what_the_task_waits_for(void) {
  ID sleeper = host_make_task(HIGH_PRIORITY);
  ID delayer = host_make_task(HIGH_PRIORITY);
  ID waiter = host_make_task(HIGH_PRIORITY);
  ID semid = create_semaphore(0);

  host_set_current(sleeper);
  HOST_EXPECT_WAIT(tk_slp_tsk(50));
  host_set_current(delayer);
  HOST_EXPECT_WAIT(tk_dly_tsk(50));
  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 50));

  TEST_ASSERT_EQUAL_UINT(TTW_SLP, tcb_of(sleeper)->tskwait);
  TEST_ASSERT_EQUAL_UINT(TTW_DLY, tcb_of(delayer)->tskwait);
  TEST_ASSERT_EQUAL_UINT(TTW_SEM, tcb_of(waiter)->tskwait);
  check_kernel_state();
}

// tk_rel_wai() refuses a task that is not waiting, the running task, and a task
// that does not exist, and changes nothing.
void test_release_wait_refuses_a_task_that_is_not_waiting(void) {
  ID running = host_make_task(HIGH_PRIORITY);
  ID ready = host_make_task(LOW_PRIORITY);
  ID blocked = host_make_task(LOW_PRIORITY);
  ID semid = create_semaphore(0);

  // A task that has just blocked is still the current one until it is switched
  // out, and it cannot release itself.
  host_set_current(blocked);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
  TEST_ASSERT_EQUAL(E_OBJ, tk_rel_wai(blocked));
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb_of(blocked)->tskstat);

  host_set_current(running);

  TEST_ASSERT_EQUAL(E_OBJ, tk_rel_wai(running));
  TEST_ASSERT_EQUAL(E_OBJ, tk_rel_wai(ready));
  TEST_ASSERT_EQUAL(E_NOEXS, tk_rel_wai(CFN_MAX_TSKID));
  TEST_ASSERT_EQUAL(E_ID, tk_rel_wai(0));
  TEST_ASSERT_EQUAL(E_ID, tk_rel_wai(CFN_MAX_TSKID + 1));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb_of(ready)->tskstat);
  check_kernel_state();
}

// A signal that comes after a wait has timed out, and before the task has run
// again, must not find the task on the semaphore's wait queue: it would make
// the task ready a second time, overwrite its time-out cause and spend a unit
// on a task that is no longer waiting.
void test_a_signal_after_a_time_out_leaves_the_unit_and_the_cause(void) {
  ID waiter = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  ID semid = create_semaphore(0);
  SEMCB *semcb = &tkmc_semcbs[semid - 1];
  TCB *tcb = tcb_of(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 10));
  host_tick(2);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  check_kernel_state();

  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL(E_TMOUT, tcb->wupcause);
  TEST_ASSERT_EQUAL(1, semcb->semcnt);
  check_kernel_state();
}

// The task that timed out waits for another semaphore. A signal to the first
// one must not wake it: it is not waiting for that one any more.
void test_a_signal_to_a_semaphore_the_task_left_does_not_wake_it(void) {
  ID waiter = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  ID first = create_semaphore(0);
  ID second = create_semaphore(0);
  TCB *tcb = tcb_of(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(first, 1, 10));
  host_tick(2);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(second, 1, TMO_FEVR));
  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(first, 1));
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb->tskstat);
  TEST_ASSERT_TRUE(
      in_queue(&tkmc_semcbs[second - 1].wait_queue, &tcb->winfo.wait_queue));
  TEST_ASSERT_EQUAL(1, tkmc_semcbs[first - 1].semcnt);
  check_kernel_state();

  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(second, 1));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  check_kernel_state();
}

static ID create_flag(ATR attributes) {
  ID flgid = tk_cre_flg(&(T_CFLG){.exinf = 0, .flgatr = attributes});
  TEST_ASSERT_GREATER_THAN(0, flgid);
  return flgid;
}

// Same for an event flag. The dead waiter asked for the pattern to be cleared,
// so a wrongly woken waiter would clear what the setter left.
void test_setting_a_flag_after_a_time_out_keeps_the_pattern_and_the_cause(
    void) {
  ID waiter = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  ID flgid = create_flag(TA_WSGL);
  TCB *tcb = tcb_of(waiter);
  UINT pattern = 0;

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_flg(flgid, 0x1, TWF_ANDW | TWF_CLR, &pattern, 10));
  host_tick(2);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  check_kernel_state();

  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(flgid, 0x1));
  TEST_ASSERT_EQUAL(E_TMOUT, tcb->wupcause);
  TEST_ASSERT_EQUAL_UINT(0x1, tkmc_flgcbs[flgid - 1].flgptn);
  check_kernel_state();
}

// A flag that allows one waiter refuses a second one while the first is on its
// wait queue; after a time-out the first is not, so the second may wait.
void test_a_flag_that_allows_one_waiter_is_free_after_a_time_out(void) {
  ID first = host_make_task(HIGH_PRIORITY);
  ID second = host_make_task(HIGH_PRIORITY);
  ID flgid = create_flag(TA_WSGL);
  UINT pattern = 0;

  host_set_current(first);
  HOST_EXPECT_WAIT(tk_wai_flg(flgid, 0x1, TWF_ANDW, &pattern, 10));
  host_tick(2);
  check_kernel_state();

  host_set_current(second);
  HOST_EXPECT_WAIT(tk_wai_flg(flgid, 0x2, TWF_ANDW, &pattern, TMO_FEVR));
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb_of(second)->tskstat);
  check_kernel_state();
}

// A message sent after a receiver has timed out stays in the mailbox for the
// next receiver; it is not handed to the task that gave up.
void test_a_message_sent_after_a_time_out_stays_in_the_mailbox(void) {
  static T_MSG message;
  ID receiver = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  ID mbxid = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TFIFO});
  TEST_ASSERT_GREATER_THAN(0, mbxid);
  T_MSG *received = NULL;
  TCB *tcb = tcb_of(receiver);

  host_set_current(receiver);
  HOST_EXPECT_WAIT(tk_rcv_mbx(mbxid, &received, 10));
  host_tick(2);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  check_kernel_state();

  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, &message));
  TEST_ASSERT_EQUAL(E_TMOUT, tcb->wupcause);
  TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(mbxid, &received, TMO_POL));
  TEST_ASSERT_EQUAL_PTR(&message, received);
  check_kernel_state();
}

// Waking a sleeper that has a time limit takes the limit away too: the task
// is neither on the timer queue nor counting ticks.
void test_waking_a_timed_sleeper_clears_its_time_limit(void) {
  ID sleeper = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  TCB *tcb = tcb_of(sleeper);

  host_set_current(sleeper);
  HOST_EXPECT_WAIT(tk_slp_tsk(50));
  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(sleeper));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  TEST_ASSERT_EQUAL_UINT(0, tcb->delay_ticks);
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  check_kernel_state();

  host_tick(10);
  TEST_ASSERT_EQUAL(E_OK, tcb->wupcause);
}

// The semaphore is deleted after the wait has timed out, and another one is
// created. The task must not be linked to what is left of the old wait queue:
// the control block's list head has been reused as a link of the free list, and
// unlinking the task later would corrupt that list.
void test_a_semaphore_deleted_after_a_time_out_leaves_no_link_to_the_task(
    void) {
  ID waiter = host_make_task(HIGH_PRIORITY);
  ID semid = create_semaphore(0);
  TCB *tcb = tcb_of(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 10));
  host_tick(2);
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  ID again = create_semaphore(0);

  TEST_ASSERT_TRUE(tkmc_list_empty(&tcb->winfo.wait_queue));
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_semcbs[again - 1].wait_queue));
  check_kernel_state();
}

// Deleting a semaphore releases everyone who waits for it, with the cause that
// says it was deleted. The waiters are ready afterwards, on their ready queues,
// and a signal to the deleted semaphore reaches nobody.
void test_deleting_a_semaphore_releases_every_waiter_with_the_deleted_cause(
    void) {
  ID first = host_make_task(HIGH_PRIORITY);
  ID second = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  ID semid = create_semaphore(0);

  host_set_current(first);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
  host_set_current(second);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
  host_set_current(other);
  check_kernel_state();

  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  ID waiters[] = {first, second};
  for (UINT i = 0; i < 2; ++i) {
    TCB *tcb = tcb_of(waiters[i]);
    TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
    TEST_ASSERT_EQUAL(E_DLT, tcb->wupcause);
    TEST_ASSERT_TRUE(
        in_queue(&tkmc_ready_queue[HIGH_PRIORITY - 1], &tcb->head));
  }
  check_kernel_state();

  TEST_ASSERT_EQUAL(E_NOEXS, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL(E_DLT, tcb_of(first)->wupcause);
  check_kernel_state();
}

// A waiter with a time limit leaves the timer queue too, or its time limit
// would later end a wait that has ended, and replace the cause.
void test_deleting_a_semaphore_ends_the_time_limit_of_its_waiter(void) {
  ID forever = host_make_task(HIGH_PRIORITY);
  ID timed = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  ID semid = create_semaphore(0);

  host_set_current(forever);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
  host_set_current(timed);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 50));
  host_set_current(other);

  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  check_kernel_state();

  host_tick(10);
  TEST_ASSERT_EQUAL(E_DLT, tcb_of(timed)->wupcause);
  check_kernel_state();
}

static UINT count_creatable_semaphores(void) {
  UINT created = 0;
  for (;;) {
    // A free list that does not end must fail the test, not hang it.
    TEST_ASSERT_LESS_OR_EQUAL_UINT(CFN_MAX_SEMID, created);
    ID semid = tk_cre_sem(
        &(T_CSEM){.exinf = 0, .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1});
    if (semid < 0) {
      TEST_ASSERT_EQUAL(E_LIMIT, semid);
      return created;
    }
    ++created;
  }
}

// The control block of a deleted semaphore goes back on the free list with an
// empty wait queue. If a waiter were still linked to it, the time limit of that
// waiter would unlink it from what is now the free list, and the kernel could
// not hand out all its semaphores any more.
void test_the_free_list_is_whole_after_deleting_a_semaphore_with_waiters(void) {
  ID waiter = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  ID semid = create_semaphore(0);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 50));
  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  host_tick(10);

  TEST_ASSERT_EQUAL_UINT(CFN_MAX_SEMID, count_creatable_semaphores());
  check_kernel_state();
}

// The release asks for a task switch only if a released task should run before
// the one that deleted the semaphore.
void test_deleting_a_semaphore_requests_a_switch_for_a_higher_priority_waiter(
    void) {
  ID waiter = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  ID semid = create_semaphore(0);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
  host_set_current(other);
  UINT before = host_write_count(MSIP);

  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  TEST_ASSERT_EQUAL_UINT(before + 1, host_write_count(MSIP));
  TEST_ASSERT_EQUAL_UINT(1, host_last_write(MSIP));
  TEST_ASSERT_EQUAL_PTR(tcb_of(waiter), next);
}

void test_deleting_a_semaphore_requests_no_switch_otherwise(void) {
  ID waiter = host_make_task(LOW_PRIORITY);
  ID other = host_make_task(HIGH_PRIORITY);
  ID waiting = create_semaphore(0);
  ID empty = create_semaphore(0);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(waiting, 1, TMO_FEVR));
  host_set_current(other);
  UINT before = host_write_count(MSIP);

  // A waiter of lower priority than the deleting task, then no waiter at all.
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(waiting));
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(empty));
  TEST_ASSERT_EQUAL_UINT(before, host_write_count(MSIP));
  check_kernel_state();
}

// On a TA_FIRST semaphore the head of the queue holds back the waiters behind
// it. When the head leaves without its units (its time limit runs out, or
// tk_rel_wai()), the units it could not use go to the waiters behind it, as far
// as they go, at that moment: a signal that came while the head waited is not
// lost until the next one. The kernel used to leave the next waiter waiting
// with the units lying there, and refused every new request meanwhile.
//
// A semaphore of two units: head waits for two, next waits for one, and one
// unit is signalled, which the head cannot use. The head has the lowest
// priority of the three tasks, so its own departure asks for no task switch:
// a switch request afterwards can only come from serving the next waiter.
static ID head_blocks_the_next_waiter(ID *head, ID *next_waiter, TMO next_ms) {
  ID semid = tk_cre_sem(&(T_CSEM){
      .exinf = 0, .sematr = TA_TFIFO | TA_FIRST, .isemcnt = 0, .maxsem = 2});
  TEST_ASSERT_GREATER_THAN(0, semid);
  *head = host_make_task(LOW_PRIORITY + 1);
  *next_waiter = host_make_task(HIGH_PRIORITY);
  ID other = host_make_task(LOW_PRIORITY);
  host_set_current(*head);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 2, 100)); // 11 ticks
  host_set_current(*next_waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, next_ms));
  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb_of(*head)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb_of(*next_waiter)->tskstat);
  TEST_ASSERT_EQUAL(1, tkmc_semcbs[semid - 1].semcnt);
  return semid;
}

// requests_before is the number of switch requests before the head left.
static void check_next_waiter_was_served(ID semid, ID next_waiter,
                                         UINT requests_before) {
  TCB *tcb = tcb_of(next_waiter);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  TEST_ASSERT_EQUAL(E_OK, tcb->wupcause);
  TEST_ASSERT_EQUAL(0, tkmc_semcbs[semid - 1].semcnt);
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_semcbs[semid - 1].wait_queue));
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  TEST_ASSERT_EQUAL_UINT(requests_before + 1, host_write_count(MSIP));
  TEST_ASSERT_EQUAL_PTR(tcb, next);
  check_kernel_state();
}

// The next waiter has a time limit of its own, so it is the task behind the
// head on the timer queue when the head's time runs out and it is served.
void test_a_head_that_times_out_hands_its_units_to_the_next_waiter(void) {
  ID head, next_waiter;
  ID semid = head_blocks_the_next_waiter(&head, &next_waiter, 200);
  UINT requests = host_write_count(MSIP);

  host_tick(11);
  TEST_ASSERT_EQUAL(E_TMOUT, tcb_of(head)->wupcause);
  check_next_waiter_was_served(semid, next_waiter, requests);
}

extern void tkmc_timer_handler(void);

// One delayed interrupt reaches both deadlines. B's earlier deadline must
// release it before A leaves; otherwise A hands the unit to an expired B.
void test_a_late_interrupt_expires_the_successor_before_the_head(void) {
  ID head, next_waiter;
  ID semid = head_blocks_the_next_waiter(&head, &next_waiter, 20);
  UINT requests = host_write_count(MSIP);

  host_mtime = 11 * HOST_TICK;
  tkmc_timer_handler();

  TEST_ASSERT_EQUAL(E_TMOUT, tcb_of(head)->wupcause);
  TEST_ASSERT_EQUAL(E_TMOUT, tcb_of(next_waiter)->wupcause);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb_of(head)->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb_of(next_waiter)->tskstat);
  TEST_ASSERT_EQUAL(1, tkmc_semcbs[semid - 1].semcnt);
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_semcbs[semid - 1].wait_queue));
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  TEST_ASSERT_EQUAL_UINT(requests + 1, host_write_count(MSIP));
  TEST_ASSERT_EQUAL_PTR(tcb_of(next_waiter), next);
  check_kernel_state();
}

// Reversing the deadlines must still hand the unit on when A expires, even
// though B's later deadline has also passed by the time the interrupt runs.
void test_a_late_interrupt_serves_the_successor_before_its_later_deadline(
    void) {
  ID head, next_waiter;
  ID semid = head_blocks_the_next_waiter(&head, &next_waiter, 200);
  UINT requests = host_write_count(MSIP);

  host_mtime = 21 * HOST_TICK;
  tkmc_timer_handler();

  TEST_ASSERT_EQUAL(E_TMOUT, tcb_of(head)->wupcause);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb_of(head)->tskstat);
  check_next_waiter_was_served(semid, next_waiter, requests);
}

void test_a_head_that_is_released_hands_its_units_to_the_next_waiter(void) {
  ID head, next_waiter;
  ID semid = head_blocks_the_next_waiter(&head, &next_waiter, TMO_FEVR);
  UINT requests = host_write_count(MSIP);

  TEST_ASSERT_EQUAL(E_OK, tk_rel_wai(head));
  TEST_ASSERT_EQUAL(E_RLWAI, tcb_of(head)->wupcause);
  check_next_waiter_was_served(semid, next_waiter, requests);
}

// Deleting the semaphore releases both with E_DLT: the head's departure hands
// nothing on, as the semaphore is going away with every waiter.
void test_deleting_a_semaphore_hands_no_units_to_the_waiters_it_releases(void) {
  ID head, next_waiter;
  ID semid = head_blocks_the_next_waiter(&head, &next_waiter, TMO_FEVR);

  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb_of(head)->tskstat);
  TEST_ASSERT_EQUAL(E_DLT, tcb_of(head)->wupcause);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb_of(next_waiter)->tskstat);
  TEST_ASSERT_EQUAL(E_DLT, tcb_of(next_waiter)->wupcause);
  check_kernel_state();
}

static const ATR departure_attrs[] = {TA_TFIFO | TA_FIRST, TA_TPRI | TA_FIRST,
                                      TA_TFIFO | TA_CNT, TA_TPRI | TA_CNT};

static void check_departure_waiter(ID id, ID semid, BOOL waiting, BOOL timed,
                                   ER cause) {
  TCB *tcb = tcb_of(id);
  TEST_ASSERT_EQUAL_UINT(waiting ? TTS_WAI : TTS_RDY, tcb->tskstat);
  TEST_ASSERT_EQUAL_UINT(waiting ? TTW_SEM : 0, tcb->tskwait);
  TEST_ASSERT_EQUAL(waiting && timed, in_queue(&tkmc_timer_queue, &tcb->head));
  TEST_ASSERT_EQUAL(!waiting,
                    in_queue(&tkmc_ready_queue[tcb->itskpri - 1], &tcb->head));
  if (waiting) {
    TEST_ASSERT_TRUE(
        in_queue(&tkmc_semcbs[semid - 1].wait_queue, &tcb->winfo.wait_queue));
    TEST_ASSERT_EQUAL(timed, tcb->delay_ticks != 0);
  } else {
    TEST_ASSERT_TRUE(tkmc_list_empty(&tcb->winfo.wait_queue));
    TEST_ASSERT_EQUAL_UINT(0, tcb->delay_ticks);
    TEST_ASSERT_EQUAL(cause, tcb->wupcause);
  }
  host_check_kernel_queues();
}

static ID departure_fixture(ATR attr, TMO b_timeout, ID *a, ID *b, ID *caller) {
  host_reset();
  ID semid = tk_cre_sem(&(T_CSEM){.sematr = attr, .maxsem = 2});
  TEST_ASSERT_GREATER_THAN(0, semid);
  *a = host_make_task(3);
  *b = host_make_task(4);
  *caller = host_make_task(5);
  host_set_current(*a);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 2, 100));
  host_set_current(*b);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, b_timeout));
  host_set_current(*caller);
  TEST_ASSERT_EQUAL_PTR(&tcb_of(*a)->winfo.wait_queue,
                        tkmc_semcbs[semid - 1].wait_queue.next);
  TEST_ASSERT_EQUAL(0, tkmc_semcbs[semid - 1].semcnt);
  check_departure_waiter(*a, semid, TRUE, TRUE, 0);
  check_departure_waiter(*b, semid, TRUE, b_timeout > 0, 0);
  return semid;
}

static void signal_before_departure(ID semid, ATR attr, ID a, ID b,
                                    BOOL b_timed, ID caller) {
  BOOL cnt = (attr & TA_CNT) != 0;
  UINT requests = host_write_count(MSIP);
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL(cnt ? 0 : 1, tkmc_semcbs[semid - 1].semcnt);
  check_departure_waiter(a, semid, TRUE, TRUE, 0);
  check_departure_waiter(b, semid, !cnt, b_timed, E_OK);
  TEST_ASSERT_EQUAL_UINT(requests + (cnt ? 1 : 0), host_write_count(MSIP));
  TEST_ASSERT_EQUAL_PTR(tcb_of(caller), current);
  TEST_ASSERT_EQUAL_PTR(tcb_of(cnt ? b : caller), next);
  TEST_ASSERT_TRUE(host_interrupts_enabled());
}

void test_semaphore_departure_attributes_on_timeout(void) {
  for (UINT row = 0; row < sizeof(departure_attrs) / sizeof(departure_attrs[0]);
       ++row) {
    ATR attr = departure_attrs[row];
    ID a, b, caller;
    ID semid = departure_fixture(attr, 200, &a, &b, &caller);
    signal_before_departure(semid, attr, a, b, TRUE, caller);
    UINT requests = host_write_count(MSIP);
    UINT saved;
    DI(saved);
    host_tick(10);
    TEST_ASSERT_EQUAL_UINT(1, tcb_of(a)->delay_ticks);
    check_departure_waiter(a, semid, TRUE, TRUE, 0);
    check_departure_waiter(b, semid, (attr & TA_CNT) == 0, TRUE, E_OK);
    TEST_ASSERT_EQUAL_UINT(requests, host_write_count(MSIP));
    host_tick(1);
    TEST_ASSERT_FALSE(host_interrupts_enabled());
    check_departure_waiter(a, semid, FALSE, FALSE, E_TMOUT);
    check_departure_waiter(b, semid, FALSE, FALSE, E_OK);
    TEST_ASSERT_EQUAL(0, tkmc_semcbs[semid - 1].semcnt);
    TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_semcbs[semid - 1].wait_queue));
    TEST_ASSERT_EQUAL_UINT(requests + 1, host_write_count(MSIP));
    TEST_ASSERT_EQUAL_PTR(tcb_of(a), next);
    TEST_ASSERT_EQUAL_PTR(tcb_of(caller), current);
    EI(saved);
    check_kernel_state();
  }
}

void test_semaphore_departure_attributes_on_release(void) {
  for (UINT row = 0; row < sizeof(departure_attrs) / sizeof(departure_attrs[0]);
       ++row) {
    for (UINT irq = 0; irq < 2; ++irq) {
      ID a, b, caller;
      ID semid =
          departure_fixture(departure_attrs[row], TMO_FEVR, &a, &b, &caller);
      signal_before_departure(semid, departure_attrs[row], a, b, FALSE, caller);
      UINT requests = host_write_count(MSIP);
      UINT saved = 0;
      if (!irq) {
        DI(saved);
      }
      TEST_ASSERT_EQUAL(E_OK, tk_rel_wai(a));
      TEST_ASSERT_EQUAL(irq, host_interrupts_enabled());
      check_departure_waiter(a, semid, FALSE, FALSE, E_RLWAI);
      check_departure_waiter(b, semid, FALSE, FALSE, E_OK);
      TEST_ASSERT_EQUAL(0, tkmc_semcbs[semid - 1].semcnt);
      TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_semcbs[semid - 1].wait_queue));
      TEST_ASSERT_EQUAL_UINT(requests + 1, host_write_count(MSIP));
      TEST_ASSERT_EQUAL_PTR(tcb_of(a), next);
      TEST_ASSERT_EQUAL_PTR(tcb_of(caller), current);
      if (!irq) {
        EI(saved);
      }
      check_kernel_state();
    }
  }
}

void test_semaphore_departure_attributes_on_delete(void) {
  for (UINT row = 0; row < sizeof(departure_attrs) / sizeof(departure_attrs[0]);
       ++row) {
    for (UINT signalled = 0; signalled < 2; ++signalled) {
      for (UINT irq = 0; irq < 2; ++irq) {
        ID a, b, caller;
        ID semid =
            departure_fixture(departure_attrs[row], 200, &a, &b, &caller);
        if (signalled) {
          signal_before_departure(semid, departure_attrs[row], a, b, TRUE,
                                  caller);
        }
        BOOL b_served = signalled && (departure_attrs[row] & TA_CNT);
        UINT requests = host_write_count(MSIP);
        UINT saved = 0;
        if (!irq) {
          DI(saved);
        }
        TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
        TEST_ASSERT_EQUAL(irq, host_interrupts_enabled());
        check_departure_waiter(a, semid, FALSE, FALSE, E_DLT);
        check_departure_waiter(b, semid, FALSE, FALSE, b_served ? E_OK : E_DLT);
        TEST_ASSERT_EQUAL_UINT(requests + 1, host_write_count(MSIP));
        TEST_ASSERT_EQUAL_PTR(tcb_of(a), next);
        TEST_ASSERT_EQUAL_PTR(tcb_of(caller), current);
        if (!irq) {
          EI(saved);
        }
        host_snapshot_t snapshot;
        host_take_snapshot(&snapshot);
        DI(saved);
        host_tick(1);
        EI(saved);
        host_assert_unchanged(&snapshot, "deleted waiters must have no timer");
        TEST_ASSERT_EQUAL_UINT(CFN_MAX_SEMID, count_creatable_semaphores());
        check_kernel_state();
      }
    }
  }
}

// Every call that makes a waiting task ready asks for a task switch when that
// task outranks the running one, and not otherwise. Without the request the
// woken task would run only at the next switch the system happens to make.
typedef struct {
  const char *name;
  void (*wait)(void);   // Blocks the current task on the object.
  ER (*wake)(ID tskid); // Makes the waiting task ready.
} wake_call_t;

static ID s_wake_semid;
static ID s_wake_flgid;
static ID s_wake_mbxid;
static UINT s_wake_flgptn;
static T_MSG *s_wake_msg;
static T_MSG s_wake_message;

static void wait_sleep(void) { tk_slp_tsk(TMO_FEVR); }
static void wait_flag(void) {
  tk_wai_flg(s_wake_flgid, 0x1, TWF_ORW, &s_wake_flgptn, TMO_FEVR);
}
static void wait_mailbox(void) {
  tk_rcv_mbx(s_wake_mbxid, &s_wake_msg, TMO_FEVR);
}
static void wait_semaphore(void) { tk_wai_sem(s_wake_semid, 1, TMO_FEVR); }
static ER wake_wup(ID tskid) { return tk_wup_tsk(tskid); }
static ER wake_rel_wai(ID tskid) { return tk_rel_wai(tskid); }
static ER wake_set_flg(ID tskid) {
  (void)tskid;
  return tk_set_flg(s_wake_flgid, 0x1);
}
static ER wake_snd_mbx(ID tskid) {
  (void)tskid;
  return tk_snd_mbx(s_wake_mbxid, &s_wake_message);
}
static ER wake_del_mbx(ID tskid) {
  (void)tskid;
  return tk_del_mbx(s_wake_mbxid);
}
static ER wake_sig_sem(ID tskid) {
  (void)tskid;
  return tk_sig_sem(s_wake_semid, 1);
}

static const wake_call_t s_wake_calls[] = {
    {"tk_wup_tsk", wait_sleep, wake_wup},
    {"tk_rel_wai", wait_sleep, wake_rel_wai},
    {"tk_set_flg", wait_flag, wake_set_flg},
    {"tk_snd_mbx", wait_mailbox, wake_snd_mbx},
    {"tk_del_mbx", wait_mailbox, wake_del_mbx},
    {"tk_sig_sem", wait_semaphore, wake_sig_sem},
};

static void check_wake_requests_a_switch(const wake_call_t *call,
                                         PRI waiter_prio, PRI other_prio,
                                         BOOL expected) {
  host_reset();
  s_wake_semid = create_semaphore(0);
  s_wake_flgid = tk_cre_flg(&(T_CFLG){.exinf = 0, .flgatr = TA_TFIFO});
  s_wake_mbxid = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TFIFO});
  ID waiter = host_make_task(waiter_prio);
  ID other = host_make_task(other_prio);
  host_set_current(waiter);
  HOST_EXPECT_WAIT(call->wait());
  host_set_current(other);
  UINT before = host_write_count(MSIP);

  TEST_ASSERT_EQUAL_MESSAGE(E_OK, call->wake(waiter), call->name);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_RDY, tcb_of(waiter)->tskstat, call->name);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(before + (expected ? 1 : 0),
                                 host_write_count(MSIP), call->name);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(tcb_of(expected ? waiter : other), next,
                                call->name);
  check_kernel_state();
}

void test_every_wake_call_requests_a_switch_only_to_a_higher_priority_task(
    void) {
  for (UINT i = 0; i < sizeof(s_wake_calls) / sizeof(s_wake_calls[0]); ++i) {
    check_wake_requests_a_switch(&s_wake_calls[i], HIGH_PRIORITY, LOW_PRIORITY,
                                 TRUE);
    check_wake_requests_a_switch(&s_wake_calls[i], LOW_PRIORITY, HIGH_PRIORITY,
                                 FALSE);
  }
}
