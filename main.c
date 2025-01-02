/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

static void clear_bss(void);

extern void task1(void);

static unsigned int task1_stack[1024];
extern void tkmc_launch_task(unsigned int *sp_end, void (*f)(void));

void tkmc_start(int a0, int a1) {
  clear_bss();
  tkmc_launch_task(task1_stack + sizeof(task1_stack) / sizeof(task1_stack[0]),
                   task1);
  return;
}

static void clear_bss(void) {
  extern int *_bss_start;
  extern int *_bss_end;
  for (int *ptr = _bss_start; ptr < _bss_end; ++ptr) {
    *ptr = 0;
  }
}
