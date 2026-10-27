/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * The first code the CPU runs in the bss-clear suite. It fills the whole .bss
 * with 0xa5 and then goes on to the kernel's real _start, so the kernel boots
 * as it would on a board whose RAM holds whatever the last run left in it. QEMU
 * starts with zeroed RAM, and a kernel that does not clear .bss would never
 * notice.
 *
 * It is the first code because it is in .reset, which the linker script places
 * first, in link order, and the executable's own sources come before the
 * kernel library that holds _start.
 */
  .section .reset, "ax", @progbits
  .global bss_dirty_entry, @function
  .balign 4
bss_dirty_entry:
  /* gp is not set yet, so the addresses must not be taken from it (linker
     relaxation would do that), as in _start. */
  .option norelax
  la t0, _bss_start
  la t1, _bss_end
  .option relax
  li t2, 0xa5
1:
  bgeu t0, t1, 2f
  sb t2, 0(t0)
  addi t0, t0, 1
  j 1b
2:
  /* Leave a mark, so that a run that never got here cannot pass for one that
     did. .data is loaded from the ELF and is not cleared, unlike .bss. */
  .option norelax
  la t0, bss_dirty_ran
  .option relax
  li t1, 1
  sw t1, 0(t0)
  j _start

  .section .data
  .global bss_dirty_ran
  .balign 4
bss_dirty_ran:
  .word 0
