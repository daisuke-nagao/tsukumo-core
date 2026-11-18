/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <unity.h>

#include <stdio.h>

#include "fakes.h"

// Every test starts from a freshly booted kernel on a fresh fake machine.
void setUp(void) { host_reset(); }
void tearDown(void) {}

void run_errno_tests(void);
void run_event_flag_tests(void);
void run_fixture_tests(void);
void run_intctl_tests(void);
void run_asm_interrupt_tests(void);
void run_pending_int_tests(void);
void run_interrupt_state_tests(void);
void run_isr_context_tests(void);
void run_list_tests(void);
void run_mailbox_tests(void);
void run_memset_tests(void);
void run_object_id_tests(void);
void run_semaphore_tests(void);
void run_system_time_tests(void);
void run_task_tests(void);
void run_timeout_ticks_tests(void);
void run_timer_tests(void);
void run_wait_tests(void);
void run_wait_context_tests(void);

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  UNITY_BEGIN();
  run_errno_tests();
  run_event_flag_tests();
  run_fixture_tests();
  run_intctl_tests();
  run_asm_interrupt_tests();
  run_pending_int_tests();
  run_interrupt_state_tests();
  run_isr_context_tests();
  run_list_tests();
  run_mailbox_tests();
  run_memset_tests();
  run_object_id_tests();
  run_semaphore_tests();
  run_system_time_tests();
  run_task_tests();
  run_timeout_ticks_tests();
  run_timer_tests();
  run_wait_tests();
  run_wait_context_tests();
  return UNITY_END();
}
