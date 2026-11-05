/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
#include "address.h"

/*
 * The frame that a trap pushes on the stack of the task it interrupts: 32
 * words, the registers in the order below, the resume address (mepc) in word
 * 28, the rest unused except word 29: the outermost external trap keeps the
 * mie it masked there while it runs. A task is switched out as this frame and
 * resumed from it, so every trap entry that can switch must build and unwind
 * the same one.
 * It is the layout of StashedRegisters in kernel/task.c, which also builds the
 * first frame of a task, and of __launch_task in launch_task.S.
 *
 * SAVE_FRAME changes sp and t0 only: t0 is saved first, then carries mepc.
 * ENTER_ISR, below, changes s0 and sp after it.
 */
  .macro SAVE_FRAME
  addi sp, sp, -32*4

  sw ra, 0*4(sp)
  sw t0, 1*4(sp)
  sw t1, 2*4(sp)
  sw t2, 3*4(sp)
  sw s0, 4*4(sp)
  sw s1, 5*4(sp)
  sw a0, 6*4(sp)
  sw a1, 7*4(sp)
  sw a2, 8*4(sp)
  sw a3, 9*4(sp)
  sw a4, 10*4(sp)
  sw a5, 11*4(sp)
  sw a6, 12*4(sp)
  sw a7, 13*4(sp)
  sw s2, 14*4(sp)
  sw s3, 15*4(sp)
  sw s4, 16*4(sp)
  sw s5, 17*4(sp)
  sw s6, 18*4(sp)
  sw s7, 19*4(sp)
  sw s8, 20*4(sp)
  sw s9, 21*4(sp)
  sw s10, 22*4(sp)
  sw s11, 23*4(sp)
  sw t3, 24*4(sp)
  sw t4, 25*4(sp)
  sw t5, 26*4(sp)
  sw t6, 27*4(sp)

  csrr t0, mepc
  sw t0, 28*4(sp)
  .endm

/* Pops the frame at sp, mepc included, and leaves sp above it. */
  .macro RESTORE_FRAME
  lw t0, 28*4(sp)
  csrw mepc, t0

  lw t6, 27*4(sp)
  lw t5, 26*4(sp)
  lw t4, 25*4(sp)
  lw t3, 24*4(sp)
  lw s11, 23*4(sp)
  lw s10, 22*4(sp)
  lw s9, 21*4(sp)
  lw s8, 20*4(sp)
  lw s7, 19*4(sp)
  lw s6, 18*4(sp)
  lw s5, 17*4(sp)
  lw s4, 16*4(sp)
  lw s3, 15*4(sp)
  lw s2, 14*4(sp)
  lw a7, 13*4(sp)
  lw a6, 12*4(sp)
  lw a5, 11*4(sp)
  lw a4, 10*4(sp)
  lw a3, 9*4(sp)
  lw a2, 8*4(sp)
  lw a1, 7*4(sp)
  lw a0, 6*4(sp)
  lw s1, 5*4(sp)
  lw s0, 4*4(sp)
  lw t2, 3*4(sp)
  lw t1, 2*4(sp)
  lw t0, 1*4(sp)
  lw ra, 0*4(sp)

  addi sp, sp, 32*4
  .endm

/*
 * The C parts of a trap (the handler and schedule()) run on their own stack,
 * not on the stack of the task the trap interrupted. SAVE_FRAME has already
 * put the task's frame on its own stack, so a task is still switched out as
 * that frame; only what the C code pushes goes elsewhere. The task stacks then
 * need room for the 128 byte frame and nothing more, however deep a handler
 * calls.
 *
 * ENTER_ISR counts the trap in tkmc_isr_depth (set before C runs, so the
 * handler and schedule() see tkmc_in_isr()), keeps the task's frame in s0 and
 * moves sp to the top of the interrupt stack. s0 is saved in the frame and
 * C code leaves it as it found it, so it holds the frame until the trap
 * leaves. It is the one register this macro changes besides sp and t0..t1.
 * LEAVE_ISR takes the trap off the count; sp must already be the frame of the
 * task that resumes. The timer and software entries use these two as they
 * are, because no trap is taken inside them; external_interrupt, which can
 * be interrupted, does the same by hand and keeps sp when the count says it
 * is already on the interrupt stack.
 *
 * Not done: mscratch holding the top of the interrupt stack, swapped with sp on
 * entry. That is for entries that must find a stack before they have saved
 * anything; here SAVE_FRAME needs only sp, so the task's own stack is the
 * place to start from.
 */
  .macro ENTER_ISR
  la t0, tkmc_isr_depth
  lw t1, 0(t0)
  addi t1, t1, 1
  sw t1, 0(t0)
  mv s0, sp
  la sp, _isr_stack_end
  .endm

  .macro LEAVE_ISR
  la t0, tkmc_isr_depth
  lw t1, 0(t0)
  addi t1, t1, -1
  sw t1, 0(t0)
  .endm
  .section .reset, "ax", @progbits
  .global _start, @function
  .balign 4
_start:
  .option norelax
  la gp, __global_pointer$
  .option relax

  /* a0 and a1 hold what the boot code passed (on QEMU the hart ID and the
     device tree address) and go to tkmc_start as they are, so the vector
     setup uses t0. */
  la t0, trap_handlers
  addi t0, t0, 1
  csrw mtvec, t0

  /* Enable software, timer and external interrupts */
  li t0, (1<<3) | (1<<7) | (1<<11)
  csrs mie, t0

  la sp, _stack_end

  call tkmc_start
1:
  wfi
  j 1b

  .balign 4
mtrap_handler:
  SAVE_FRAME
  ENTER_ISR

  /* Clear software interrupt */
  li t0, CLINT_BASE_ADDRESS+CLINT_MSIP_OFFSET
  sw zero, 0(t0)

  /* Always switch: it is the request itself that brought us here. */
  mv a0, s0
  call schedule
  mv sp, a0

  LEAVE_ISR
  RESTORE_FRAME
  mret

  /*
   * A wait that the timer ends calls dispatch(), which raises MSIP. Taking
   * that as a second trap after mret would save and restore the frame twice,
   * so the switch is made here instead: MSIP is read once after the handler,
   * and if it is up, cleared and served as mtrap_handler does.
   *
   * Not done: have tkmc_timer_handler return whether to switch, which would
   * change a contract that the host tests and test/suites/wakeup_context.c
   * rely on; nor compare next with current here, which would make this code
   * depend on every write to next in the C files being paired with dispatch().
   * MSIP is the request itself, from any source, also one raised before this
   * trap, which is served here as well.
   *
   * The timer trap is never nested, so there is no branch for a depth other
   * than 0: it is taken with MIE on only when no handler has interrupts on,
   * and external_interrupt masks it while one has.
   */
  .balign 4
timer_interrupt:
  SAVE_FRAME
  ENTER_ISR

  call tkmc_timer_handler

  li t0, CLINT_BASE_ADDRESS+CLINT_MSIP_OFFSET
  lw t1, 0(t0)
  beqz t1, 1f
  sw zero, 0(t0)
  mv a0, s0
  call schedule
  mv sp, a0
  j 2f
1:
  mv sp, s0
2:
  LEAVE_ISR
  RESTORE_FRAME
  mret

  /*
   * The external interrupt is the one trap that can nest: a handler that turns
   * interrupts on (EI) is interrupted by a source of a higher priority, which
   * the PLIC threshold set in tkmc_external_interrupt() lets in, and by no
   * other. A trap that finds the depth already above 0 is on the interrupt
   * stack, so it keeps sp, and its frame goes below the handler's.
   *
   * Only the outermost trap changes mie and switches tasks. It masks the timer
   * and software interrupts (MTIE and MSIE) while the handler runs, because
   * those traps would serve a task switch inside the handler: the task that a
   * nested handler wakes is switched to once, at the exit of the outermost
   * trap, which reads MSIP as the timer trap does. The mie bits it cleared are
   * in the frame (word 29, unused) until then. Nested traps change nothing of
   * mie and leave the switch to the outermost one.
   *
   * Not done: nesting of the timer and software interrupts. A software
   * interrupt that nested would have to be held back and served at the
   * outermost exit anyway, and a timer one that ran inside a handler would
   * only be ahead of it by the handler's time.
   *
   * Every exit sets mstatus.MPP to machine mode before mret. An mret sets MPP
   * to the least privileged mode there is (User on this machine), so the mret
   * of a nested trap would leave the outer one returning to User mode, where
   * the machine has no access to the kernel's memory: the outer handler runs
   * on, and its own mret then fails to fetch.
   *
   * A handler that has turned interrupts on must turn them off again before
   * it returns: the exit below puts the threshold and the frame back with
   * interrupts as the handler left them, and a trap in the middle would find
   * them half done.
   */
  .balign 4
external_interrupt:
  SAVE_FRAME

  la t0, tkmc_isr_depth
  lw t1, 0(t0)
  addi t1, t1, 1
  sw t1, 0(t0)
  mv s0, sp
  li t0, 1
  bne t1, t0, 1f
  la sp, _isr_stack_end
  li t1, (1<<3) | (1<<7)
  csrrc t1, mie, t1
  sw t1, 29*4(s0)
1:
  call tkmc_external_interrupt

  la t0, tkmc_isr_depth
  lw t1, 0(t0)
  li t0, 1
  bne t1, t0, 4f

  li t0, CLINT_BASE_ADDRESS+CLINT_MSIP_OFFSET
  lw t1, 0(t0)
  beqz t1, 2f
  sw zero, 0(t0)
  mv a0, s0
  call schedule
  mv sp, a0
  j 3f
2:
  mv sp, s0
3:
  lw t0, 29*4(s0)
  andi t0, t0, (1<<3) | (1<<7)
  csrs mie, t0
  li t0, 3<<11
  csrs mstatus, t0
  LEAVE_ISR
  RESTORE_FRAME
  mret
4:
  mv sp, s0
  li t0, 3<<11
  csrs mstatus, t0
  LEAVE_ISR
  RESTORE_FRAME
  mret

  /*
   * Synchronous exceptions (illegal instruction, access fault, ebreak, ...). In
   * vectored mode they all enter at vector 0, whatever their cause. No
   * exception here is recoverable, so the system stops and reports the cause.
   *
   * Not done: returning with mepc advanced by 4. That is wrong for compressed
   * instructions (2 bytes) and for faults that must be retried, and with mepc
   * unchanged mret runs the faulting instruction again for ever, which is what
   * this handler replaces.
   *
   * The stack is taken anew: the exception may be a stack overflow.
   */
  .balign 4
sync_exception_handler:
  la sp, _stack_end
  csrr a0, mcause
  csrr a1, mepc
  csrr a2, mtval
  call tkmc_fatal_trap
1:
  wfi
  j 1b

  .balign 4
default_handler:
  mret

  .balign 16
trap_handlers:
  j sync_exception_handler /* 0: Synchronous exceptions */
  .balign 4
  j default_handler    /* 1: Supervisor software interrupt */
  .balign 4
  j default_handler    /* 2: Reserved */
  .balign 4
  j mtrap_handler      /* 3: Machine software interrupt (MSIP) */
  .balign 4
  j default_handler    /* 4: Reserved */
  .balign 4
  j default_handler    /* 5: Reserved */
  .balign 4
  j default_handler    /* 6: Reserved */
  .balign 4
  j timer_interrupt    /* 7: Machine timer interrupt (MTIP) */
  .balign 4
  j default_handler    /* 8: Reserved */
  .balign 4
  j default_handler    /* 9: Supervisor external interrupt */
  .balign 4
  j default_handler    /* 10: Reserved */
  .balign 4
  j external_interrupt /* 11: Machine external interrupt (MEIP) */
  .balign 4
  j default_handler    /* 12: Reserved */
  .balign 4
  j default_handler    /* 13: Reserved */
  .balign 4
  j default_handler    /* 14: Reserved */
  .balign 4
  j default_handler    /* 15: Reserved */
