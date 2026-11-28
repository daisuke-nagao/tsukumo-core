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

#include "fakes.h"

// A task that waits for a semaphore blocks, leaves the ready queue and joins
// the semaphore's wait queue, and the other task is the one to run next. A
// signal from another task takes it off the wait queue and makes it ready.
static void test_tk_wai_sem_blocks_until_tk_sig_sem_wakes_waiter(void) {
  ID waiter = host_make_task(HOST_HIGH_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY);
  ID semid = host_create_semaphore();
  SEMCB *semcb = &tkmc_semcbs[semid - 1];
  TCB *tcb = host_tcb(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTW_SEM, tcb->tskwait);
  TEST_ASSERT_TRUE(host_in_queue(&semcb->wait_queue, &tcb->winfo.wait_queue));
  TEST_ASSERT_TRUE(tkmc_list_empty(&tcb->head)); // On no ready or timer queue.
  TEST_ASSERT_EQUAL_PTR(host_tcb(other), next);
  host_check_kernel_state();

  host_set_current(other);
  // A cause that is not E_OK, so E_OK afterwards shows the kernel wrote it.
  tcb->wupcause = E_RLWAI;
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  TEST_ASSERT_EQUAL(E_OK, tcb->wupcause);
  TEST_ASSERT_TRUE(tkmc_list_empty(&semcb->wait_queue));
  TEST_ASSERT_TRUE(
      host_in_queue(&tkmc_ready_queue[HOST_HIGH_PRIORITY - 1], &tcb->head));
  // The waiter took the unit that was signalled.
  TEST_ASSERT_EQUAL(0, semcb->semcnt);
  host_check_kernel_state();
}

// A wait with a time limit ends when the limit has run out: not a tick before,
// and the timer interrupt then makes the task ready with a time-out cause and
// takes it off the timer queue.
static void test_tk_wai_sem_ends_with_e_tmout_when_time_limit_runs_out(void) {
  ID waiter = host_make_task(HOST_HIGH_PRIORITY);
  ID semid = host_create_semaphore();
  SEMCB *semcb = &tkmc_semcbs[semid - 1];
  TCB *tcb = host_tcb(waiter);

  host_set_current(waiter);
  // 50 ms is 5 ticks, plus one because the first tick comes at any time.
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 50));
  TEST_ASSERT_EQUAL_UINT(TTW_SEM, tcb->tskwait);
  TEST_ASSERT_TRUE(host_in_queue(&tkmc_timer_queue, &tcb->head));
  host_check_kernel_state();

  host_tick(5);
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb->tskstat);
  host_tick(1);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  TEST_ASSERT_EQUAL(E_TMOUT, tcb->wupcause);
  TEST_ASSERT_TRUE(
      host_in_queue(&tkmc_ready_queue[HOST_HIGH_PRIORITY - 1], &tcb->head));
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  TEST_ASSERT_TRUE(tkmc_list_empty(&semcb->wait_queue));
  host_check_kernel_state();
}

// tk_rel_wai() takes a sleeping task out of its wait and makes it ready, with a
// release cause that tells it the wait did not end normally.
static void test_tk_rel_wai_wakes_sleeping_task(void) {
  ID sleeper = host_make_task(HOST_HIGH_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY);
  TCB *tcb = host_tcb(sleeper);

  host_set_current(sleeper);
  HOST_EXPECT_WAIT(tk_slp_tsk(TMO_FEVR));
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb->tskstat);
  TEST_ASSERT_EQUAL_UINT(TTW_SLP, tcb->tskwait);
  TEST_ASSERT_EQUAL_PTR(host_tcb(other), next);
  host_check_kernel_state();

  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_rel_wai(sleeper));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  TEST_ASSERT_EQUAL(E_RLWAI, tcb->wupcause);
  TEST_ASSERT_TRUE(
      host_in_queue(&tkmc_ready_queue[HOST_HIGH_PRIORITY - 1], &tcb->head));
  host_check_kernel_state();
}

// tk_rel_wai() on a task that waits for a semaphore with a time limit takes it
// off the semaphore's wait queue and the timer queue, so neither a later signal
// nor the time limit can wake it a second time.
static void test_tk_rel_wai_takes_timed_semaphore_waiter_off_both_queues(void) {
  ID waiter = host_make_task(HOST_HIGH_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY);
  ID semid = host_create_semaphore();
  SEMCB *semcb = &tkmc_semcbs[semid - 1];
  TCB *tcb = host_tcb(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 50));
  TEST_ASSERT_TRUE(host_in_queue(&semcb->wait_queue, &tcb->winfo.wait_queue));
  TEST_ASSERT_TRUE(host_in_queue(&tkmc_timer_queue, &tcb->head));

  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_rel_wai(waiter));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  TEST_ASSERT_EQUAL(E_RLWAI, tcb->wupcause);
  TEST_ASSERT_TRUE(tkmc_list_empty(&semcb->wait_queue));
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  TEST_ASSERT_TRUE(
      host_in_queue(&tkmc_ready_queue[HOST_HIGH_PRIORITY - 1], &tcb->head));
  host_check_kernel_state();

  // The time limit has nothing left to act on.
  host_tick(10);
  TEST_ASSERT_EQUAL(E_RLWAI, tcb->wupcause);
  host_check_kernel_state();
}

// A wake-up request queued while a task waits for a semaphore is still there
// after tk_rel_wai() ends that wait, and the task's next sleep uses it up.
static void test_tk_rel_wai_keeps_queued_wakeup(void) {
  ID waiter = host_make_task(HOST_HIGH_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY);
  ID semid = host_create_semaphore();
  TCB *tcb = host_tcb(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));

  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(waiter));
  TEST_ASSERT_EQUAL(E_OK, tk_rel_wai(waiter));
  TEST_ASSERT_EQUAL(E_RLWAI, tcb->wupcause);
  TEST_ASSERT_EQUAL_UINT(1, tcb->wupcnt);
  host_check_kernel_state();

  // Switch to the released task, as tk_wai_sem() would leave it after reading
  // the cause, and let it sleep.
  tcb->wupcause = E_OK;
  host_tcb(other)->tskstat = TTS_RDY;
  host_set_current(waiter);
  TEST_ASSERT_EQUAL(E_OK, tk_slp_tsk(TMO_POL));
  TEST_ASSERT_EQUAL_UINT(0, tcb->wupcnt);
  host_check_kernel_state();
}

// A signal that wakes a task with a time limit also takes it off the timer
// queue, or the time limit would later wake the task, which is running by then.
static void test_tk_sig_sem_disarms_time_limit_of_its_waiter(void) {
  ID waiter = host_make_task(HOST_HIGH_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY);
  ID semid = host_create_semaphore();
  TCB *tcb = host_tcb(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 50));

  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  host_check_kernel_state();

  host_tick(10);
  TEST_ASSERT_EQUAL(E_OK, tcb->wupcause);
  host_check_kernel_state();
}

// tk_rel_wai() refuses a task that is not waiting, the running task, and a task
// that does not exist, and changes nothing.
static void test_tk_rel_wai_refuses_task_that_is_not_waiting(void) {
  ID running = host_make_task(HOST_HIGH_PRIORITY);
  ID ready = host_make_task(HOST_LOW_PRIORITY);
  ID blocked = host_make_task(HOST_LOW_PRIORITY);
  ID semid = host_create_semaphore();

  // A task that has just blocked is still the current one until it is switched
  // out, and it cannot release itself.
  host_set_current(blocked);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
  host_snapshot_t before;
  host_take_snapshot(&before);
  TEST_ASSERT_EQUAL_MESSAGE(E_OBJ, tk_rel_wai(blocked),
                            "the current blocked task");
  host_assert_unchanged(&before, "the current blocked task");
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, host_tcb(blocked)->tskstat);

  host_set_current(running);

  const struct {
    ID tskid;
    ER expected;
    const char *label;
  } cases[] = {
      {running, E_OBJ, "the running task"},
      {ready, E_OBJ, "the ready task"},
      {CFN_MAX_TSKID, E_NOEXS, "a nonexistent task"},
      {0, E_ID, "task ID zero"},
      {CFN_MAX_TSKID + 1, E_ID, "a task ID above the maximum"},
  };
  for (UINT i = 0; i < HOST_COUNT(cases); ++i) {
    host_take_snapshot(&before);
    TEST_ASSERT_EQUAL_MESSAGE(cases[i].expected, tk_rel_wai(cases[i].tskid),
                              cases[i].label);
    host_assert_unchanged(&before, cases[i].label);
  }
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, host_tcb(ready)->tskstat);
  host_check_kernel_state();
}

// An ISR may release the task it interrupted right after that task began to
// wait. On the target the wait call has made the task waiting and asked for a
// switch, and an external interrupt, which outranks the switch, comes before
// it: the task is still current, and it is waiting. The release must end the
// wait and pick the next task again, so that the switch the trap exit makes
// goes to the released task, which outranks the one the wait call picked. The
// running task, though, is not waiting, and an ISR is refused for it as a task
// is. A handler is an ISR whatever MIE is: the trap is taken with interrupts
// off, and a handler may turn them on (docs/kernel/interrupts.md).
static ER release_from_an_isr(ID tskid, BOOL interrupts_off) {
  const host_context_t entered = host_enter(TRUE, interrupts_off);
  ER ercd = tk_rel_wai(tskid);
  host_leave(entered);
  return ercd;
}

static void
test_tk_rel_wai_in_isr_releases_task_it_interrupted_right_after_wait_began(
    void) {
  for (UINT round = 0; round < 2; ++round) {
    const BOOL interrupts_off = round == 0;
    const char *what = interrupts_off ? "an ISR with interrupts off"
                                      : "an ISR that turned interrupts on";
    host_reset();
    ID waiter = host_make_task(HOST_HIGH_PRIORITY);
    ID other = host_make_task(HOST_LOW_PRIORITY);
    host_set_current(waiter);
    HOST_EXPECT_WAIT(tk_slp_tsk(TMO_FEVR));
    TEST_ASSERT_EQUAL_PTR(host_tcb(waiter), current);
    TEST_ASSERT_EQUAL_PTR(host_tcb(other), next);

    ER released = release_from_an_isr(waiter, interrupts_off);

    TEST_ASSERT_EQUAL_MESSAGE(E_OK, released, what);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_RDY, host_tcb(waiter)->tskstat, what);
    TEST_ASSERT_EQUAL_MESSAGE(E_RLWAI, host_tcb(waiter)->wupcause, what);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(host_tcb(waiter), next, what);
    host_check_kernel_state();

    host_set_current(waiter);
    host_snapshot_t before;
    host_take_snapshot(&before);
    TEST_ASSERT_EQUAL_MESSAGE(
        E_OBJ, release_from_an_isr(waiter, interrupts_off), what);
    host_assert_unchanged(&before, "an ISR released the running task");
  }
}

// A signal that comes after a wait has timed out, and before the task has run
// again, must not find the task on the semaphore's wait queue: it would make
// the task ready a second time, overwrite its time-out cause and spend a unit
// on a task that is no longer waiting.
static void test_tk_sig_sem_after_time_out_leaves_unit_and_cause(void) {
  ID waiter = host_make_task(HOST_HIGH_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY);
  ID semid = host_create_semaphore();
  SEMCB *semcb = &tkmc_semcbs[semid - 1];
  TCB *tcb = host_tcb(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 10));
  host_tick(2);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  host_check_kernel_state();

  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL(E_TMOUT, tcb->wupcause);
  TEST_ASSERT_EQUAL(1, semcb->semcnt);
  host_check_kernel_state();
}

// The task that timed out waits for another semaphore. A signal to the first
// one must not wake it: it is not waiting for that one any more.
static void test_tk_sig_sem_does_not_wake_task_that_left_semaphore(void) {
  ID waiter = host_make_task(HOST_HIGH_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY);
  ID first = host_create_semaphore();
  ID second = host_create_semaphore();
  TCB *tcb = host_tcb(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(first, 1, 10));
  host_tick(2);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(second, 1, TMO_FEVR));
  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(first, 1));
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, tcb->tskstat);
  TEST_ASSERT_TRUE(host_in_queue(&tkmc_semcbs[second - 1].wait_queue,
                                 &tcb->winfo.wait_queue));
  TEST_ASSERT_EQUAL(1, tkmc_semcbs[first - 1].semcnt);
  host_check_kernel_state();

  TEST_ASSERT_EQUAL(E_OK, tk_sig_sem(second, 1));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  host_check_kernel_state();
}

static ID create_flag(void) {
  ID flgid = tk_cre_flg(&(T_CFLG){.exinf = 0, .flgatr = TA_WSGL});
  TEST_ASSERT_GREATER_THAN(0, flgid);
  return flgid;
}

// Same for an event flag. The dead waiter asked for the pattern to be cleared,
// so a wrongly woken waiter would clear what the setter left.
static void test_tk_set_flg_after_time_out_keeps_pattern_and_cause(void) {
  ID waiter = host_make_task(HOST_HIGH_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY);
  ID flgid = create_flag();
  TCB *tcb = host_tcb(waiter);
  UINT pattern = 0;

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_flg(flgid, 0x1, TWF_ANDW | TWF_CLR, &pattern, 10));
  host_tick(2);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  host_check_kernel_state();

  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_set_flg(flgid, 0x1));
  TEST_ASSERT_EQUAL(E_TMOUT, tcb->wupcause);
  TEST_ASSERT_EQUAL_UINT(0x1, tkmc_flgcbs[flgid - 1].flgptn);
  host_check_kernel_state();
}

// A flag that allows one waiter refuses a second one while the first is on its
// wait queue; after a time-out the first is not, so the second may wait.
static void test_tk_wai_flg_single_waiter_flag_is_free_after_time_out(void) {
  ID first = host_make_task(HOST_HIGH_PRIORITY);
  ID second = host_make_task(HOST_HIGH_PRIORITY);
  ID flgid = create_flag();
  UINT pattern = 0;

  host_set_current(first);
  HOST_EXPECT_WAIT(tk_wai_flg(flgid, 0x1, TWF_ANDW, &pattern, 10));
  host_tick(2);
  host_check_kernel_state();

  host_set_current(second);
  HOST_EXPECT_WAIT(tk_wai_flg(flgid, 0x2, TWF_ANDW, &pattern, TMO_FEVR));
  TEST_ASSERT_EQUAL_UINT(TTS_WAI, host_tcb(second)->tskstat);
  host_check_kernel_state();
}

// A message sent after a receiver has timed out stays in the mailbox for the
// next receiver; it is not handed to the task that gave up.
static void test_tk_snd_mbx_after_time_out_leaves_message_in_mailbox(void) {
  static T_MSG message;
  ID receiver = host_make_task(HOST_HIGH_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY);
  ID mbxid = tk_cre_mbx(&(T_CMBX){.exinf = 0, .mbxatr = TA_TFIFO});
  TEST_ASSERT_GREATER_THAN(0, mbxid);
  T_MSG *received = NULL;
  TCB *tcb = host_tcb(receiver);

  host_set_current(receiver);
  HOST_EXPECT_WAIT(tk_rcv_mbx(mbxid, &received, 10));
  host_tick(2);
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  host_check_kernel_state();

  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_snd_mbx(mbxid, &message));
  TEST_ASSERT_EQUAL(E_TMOUT, tcb->wupcause);
  TEST_ASSERT_EQUAL(E_OK, tk_rcv_mbx(mbxid, &received, TMO_POL));
  TEST_ASSERT_EQUAL_PTR(&message, received);
  host_check_kernel_state();
}

// Waking a sleeper that has a time limit takes the limit away too: the task
// is neither on the timer queue nor counting ticks.
static void test_tk_wup_tsk_on_timed_sleeper_clears_time_limit(void) {
  ID sleeper = host_make_task(HOST_HIGH_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY);
  TCB *tcb = host_tcb(sleeper);

  host_set_current(sleeper);
  HOST_EXPECT_WAIT(tk_slp_tsk(50));
  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_wup_tsk(sleeper));
  TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
  TEST_ASSERT_EQUAL_UINT(0, tcb->delay_ticks);
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  host_check_kernel_state();

  host_tick(10);
  TEST_ASSERT_EQUAL(E_OK, tcb->wupcause);
}

// The semaphore is deleted after the wait has timed out, and another one is
// created. The task must not be linked to what is left of the old wait queue:
// the control block's list head has been reused as a link of the free list, and
// unlinking the task later would corrupt that list.
static void test_tk_del_sem_after_time_out_leaves_no_link_to_task(void) {
  ID waiter = host_make_task(HOST_HIGH_PRIORITY);
  ID semid = host_create_semaphore();
  TCB *tcb = host_tcb(waiter);

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 10));
  host_tick(2);
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  ID again = host_create_semaphore();

  TEST_ASSERT_TRUE(tkmc_list_empty(&tcb->winfo.wait_queue));
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_semcbs[again - 1].wait_queue));
  host_check_kernel_state();
}

// Deleting a semaphore releases everyone who waits for it, with the cause that
// says it was deleted. The waiters are ready afterwards, on their ready queues,
// and a signal to the deleted semaphore reaches nobody.
static void test_tk_del_sem_releases_every_waiter_with_deleted_cause(void) {
  ID first = host_make_task(HOST_HIGH_PRIORITY);
  ID second = host_make_task(HOST_HIGH_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY);
  ID semid = host_create_semaphore();

  host_set_current(first);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
  host_set_current(second);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
  host_set_current(other);
  host_check_kernel_state();

  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  ID waiters[] = {first, second};
  for (UINT i = 0; i < 2; ++i) {
    TCB *tcb = host_tcb(waiters[i]);
    TEST_ASSERT_EQUAL_UINT(TTS_RDY, tcb->tskstat);
    TEST_ASSERT_EQUAL(E_DLT, tcb->wupcause);
    TEST_ASSERT_TRUE(
        host_in_queue(&tkmc_ready_queue[HOST_HIGH_PRIORITY - 1], &tcb->head));
  }
  host_check_kernel_state();

  TEST_ASSERT_EQUAL(E_NOEXS, tk_sig_sem(semid, 1));
  TEST_ASSERT_EQUAL(E_DLT, host_tcb(first)->wupcause);
  host_check_kernel_state();
}

// A waiter with a time limit leaves the timer queue too, or its time limit
// would later end a wait that has ended, and replace the cause.
static void test_tk_del_sem_ends_time_limit_of_its_waiter(void) {
  ID forever = host_make_task(HOST_HIGH_PRIORITY);
  ID timed = host_make_task(HOST_HIGH_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY);
  ID semid = host_create_semaphore();

  host_set_current(forever);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, TMO_FEVR));
  host_set_current(timed);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 50));
  host_set_current(other);

  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  TEST_ASSERT_TRUE(tkmc_list_empty(&tkmc_timer_queue));
  host_check_kernel_state();

  host_tick(10);
  TEST_ASSERT_EQUAL(E_DLT, host_tcb(timed)->wupcause);
  host_check_kernel_state();
}

// The control block of a deleted semaphore goes back on the free list with an
// empty wait queue. If a waiter were still linked to it, the time limit of that
// waiter would unlink it from what is now the free list, and the kernel could
// not hand out all its semaphores any more.
static void test_tk_del_sem_with_waiters_keeps_free_list_whole(void) {
  ID waiter = host_make_task(HOST_HIGH_PRIORITY);
  ID other = host_make_task(HOST_LOW_PRIORITY);
  ID semid = host_create_semaphore();

  host_set_current(waiter);
  HOST_EXPECT_WAIT(tk_wai_sem(semid, 1, 50));
  host_set_current(other);
  TEST_ASSERT_EQUAL(E_OK, tk_del_sem(semid));
  host_tick(10);

  TEST_ASSERT_EQUAL_UINT(CFN_MAX_SEMID, host_count_creatable_semaphores());
  host_check_kernel_state();
}

// Every call that makes a waiting task ready asks for a task switch when that
// task outranks the running one, and not otherwise. Without the request the
// woken task would run only at the next switch the system happens to make. The
// kernel asks for a switch by writing 1 to the machine software interrupt
// register of the machine timer block (CLINT); a wrong address or value would
// leave the woken task waiting for a switch that never comes.
typedef struct {
  const char *name;
  void (*wait)(void);   // Blocks the current task on the object.
  ER (*wake)(ID tskid); // Makes the waiting task ready.
  ER cause;             // The wake-up cause the waiter is left with.
} wake_call_t;

static host_objects_t s_wake;
static UINT s_wake_flgptn;
static T_MSG *s_wake_msg;
static T_MSG s_wake_message;

static void wait_sleep(void) { tk_slp_tsk(TMO_FEVR); }
static void wait_flag(void) {
  tk_wai_flg(s_wake.flgid, 0x1, TWF_ORW, &s_wake_flgptn, TMO_FEVR);
}
static void wait_mailbox(void) {
  tk_rcv_mbx(s_wake.mbxid, &s_wake_msg, TMO_FEVR);
}
static void wait_semaphore(void) { tk_wai_sem(s_wake.semid, 1, TMO_FEVR); }
static void wait_semaphore_2_ticks(void) { tk_wai_sem(s_wake.semid, 1, 10); }
static ER wake_wup(ID tskid) { return tk_wup_tsk(tskid); }
static ER wake_rel_wai(ID tskid) { return tk_rel_wai(tskid); }
static ER wake_set_flg(ID tskid) {
  (void)tskid;
  return tk_set_flg(s_wake.flgid, 0x1);
}
static ER wake_snd_mbx(ID tskid) {
  (void)tskid;
  return tk_snd_mbx(s_wake.mbxid, &s_wake_message);
}
static ER wake_del_mbx(ID tskid) {
  (void)tskid;
  return tk_del_mbx(s_wake.mbxid);
}
static ER wake_sig_sem(ID tskid) {
  (void)tskid;
  return tk_sig_sem(s_wake.semid, 1);
}
static ER wake_del_sem(ID tskid) {
  (void)tskid;
  return tk_del_sem(s_wake.semid);
}
// The time limit runs out. The tick before it asks for no switch.
static ER wake_time_out(ID tskid) {
  UINT before = host_write_count(HOST_MSIP);
  host_tick(1);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_WAI, host_tcb(tskid)->tskstat,
                                 "the wait ended a tick early");
  TEST_ASSERT_EQUAL_UINT_MESSAGE(before, host_write_count(HOST_MSIP),
                                 "a tick before the time limit asked for a "
                                 "switch");
  host_tick(1);
  return E_OK;
}

static const wake_call_t s_wake_calls[] = {
    {"tk_wup_tsk", wait_sleep, wake_wup, E_OK},
    {"tk_rel_wai", wait_sleep, wake_rel_wai, E_RLWAI},
    {"tk_set_flg", wait_flag, wake_set_flg, E_OK},
    {"tk_snd_mbx", wait_mailbox, wake_snd_mbx, E_OK},
    {"tk_del_mbx", wait_mailbox, wake_del_mbx, E_DLT},
    {"tk_sig_sem", wait_semaphore, wake_sig_sem, E_OK},
    {"tk_del_sem", wait_semaphore, wake_del_sem, E_DLT},
    {"time-out", wait_semaphore_2_ticks, wake_time_out, E_TMOUT},
};

static void check_wake_requests_a_switch(const wake_call_t *call,
                                         PRI waiter_prio, PRI other_prio,
                                         BOOL expected) {
  host_reset();
  s_wake = host_make_objects();
  ID empty = 0;
  if (call->wake == wake_del_sem && !expected) {
    empty = host_make_objects().semid;
  }
  ID waiter = host_make_task(waiter_prio);
  ID other = host_make_task(other_prio);
  host_set_current(waiter);
  HOST_EXPECT_WAIT(call->wait());
  host_set_current(other);
  UINT before = host_write_count(HOST_MSIP);
  // A cause no row expects, so a wake that never writes one fails the row.
  host_tcb(waiter)->wupcause = E_SYS;

  TEST_ASSERT_EQUAL_MESSAGE(E_OK, call->wake(waiter), call->name);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(TTS_RDY, host_tcb(waiter)->tskstat,
                                 call->name);
  TEST_ASSERT_EQUAL_MESSAGE(call->cause, host_tcb(waiter)->wupcause,
                            call->name);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(before + (expected ? 1 : 0),
                                 host_write_count(HOST_MSIP), call->name);
  if (expected) {
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, host_last_write(HOST_MSIP), call->name);
  }
  TEST_ASSERT_EQUAL_PTR_MESSAGE(host_tcb(expected ? waiter : other), next,
                                call->name);
  if (empty) {
    TEST_ASSERT_EQUAL(E_OK, tk_del_sem(empty));
    TEST_ASSERT_EQUAL_UINT(before, host_write_count(HOST_MSIP));
  }
  host_check_kernel_state();
}

static void test_wake_call_requests_switch_only_to_higher_priority_task(void) {
  for (UINT i = 0; i < HOST_COUNT(s_wake_calls); ++i) {
    check_wake_requests_a_switch(&s_wake_calls[i], HOST_HIGH_PRIORITY,
                                 HOST_LOW_PRIORITY, TRUE);
    check_wake_requests_a_switch(&s_wake_calls[i], HOST_LOW_PRIORITY,
                                 HOST_HIGH_PRIORITY, FALSE);
  }
}

void run_wait_tests(void) {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_tk_wai_sem_blocks_until_tk_sig_sem_wakes_waiter);
  RUN_TEST(test_tk_wai_sem_ends_with_e_tmout_when_time_limit_runs_out);
  RUN_TEST(test_tk_rel_wai_wakes_sleeping_task);
  RUN_TEST(test_tk_rel_wai_takes_timed_semaphore_waiter_off_both_queues);
  RUN_TEST(test_tk_rel_wai_keeps_queued_wakeup);
  RUN_TEST(test_tk_sig_sem_disarms_time_limit_of_its_waiter);
  RUN_TEST(test_tk_sig_sem_after_time_out_leaves_unit_and_cause);
  RUN_TEST(test_tk_sig_sem_does_not_wake_task_that_left_semaphore);
  RUN_TEST(test_tk_set_flg_after_time_out_keeps_pattern_and_cause);
  RUN_TEST(test_tk_wai_flg_single_waiter_flag_is_free_after_time_out);
  RUN_TEST(test_tk_snd_mbx_after_time_out_leaves_message_in_mailbox);
  RUN_TEST(test_tk_wup_tsk_on_timed_sleeper_clears_time_limit);
  RUN_TEST(test_tk_del_sem_releases_every_waiter_with_deleted_cause);
  RUN_TEST(test_tk_del_sem_ends_time_limit_of_its_waiter);
  RUN_TEST(test_tk_del_sem_with_waiters_keeps_free_list_whole);
  RUN_TEST(test_tk_del_sem_after_time_out_leaves_no_link_to_task);
  RUN_TEST(test_tk_rel_wai_refuses_task_that_is_not_waiting);
  RUN_TEST(
      test_tk_rel_wai_in_isr_releases_task_it_interrupted_right_after_wait_began);
  RUN_TEST(test_wake_call_requests_switch_only_to_higher_priority_task);
}
