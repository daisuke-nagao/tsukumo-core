/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * The register-pattern spin of the interrupt suites, for C and for assembly.
 *
 * A spin puts a different value, TEST_SPIN_PATTERN(n), in every register that
 * a trap entry could change and that the wait does not need itself (t2-t6,
 * a2-a7 and s0-s11, n counting from 0 in that order), arms the interrupt by
 * storing value at *reg, waits for *flag to become nonzero or for the machine
 * timer to reach deadline, and returns which registers no longer hold their
 * value: bit n is the n-th register, and bit 31 means that the wait ran out. 0
 * means that every register survived and the flag was set.
 *
 * It is written in assembly because C cannot do this: the compiler does not
 * allow s0 as a clobber, and a call in the wait would change the registers it
 * is meant to watch. The wait itself needs four registers, which are the ones
 * left out: t0, t1, a0 and a1 (ra is the return address). Of these only a0,
 * the flag pointer, makes the wait fail if a trap restores it wrongly; t0 and
 * t1 are covered for the timer entry by the timer-switch suite, which checks
 * them with a different wait. A spin saves and restores ra and s0-s11 for its
 * caller.
 *
 * The spin arms the interrupt itself, once the patterns are in place. A caller
 * that armed it before the call could have the trap land in its own code,
 * before any pattern was loaded, and then pass without a pattern having
 * crossed a trap.
 *
 * Each spin is a function of its own, made by SPIN with the store instruction
 * that arms it (sb for a byte register, sw for a word register). One function
 * with a width argument would need a register of its own through the wait,
 * and every register is patterned.
 */

#ifndef UUID_01A1EDF6_7B13_7671_B2C3_34661FCD0C44
#define UUID_01A1EDF6_7B13_7671_B2C3_34661FCD0C44

#define TEST_SPIN_PATTERN(n) (0x7e570000 + (n))

#ifdef __ASSEMBLER__
// clang-format off

#include "asm/rv32/address.h"

  .macro PATTERN reg, n
  li \reg, TEST_SPIN_PATTERN(\n)
  .endm

  /* Sets bit \bit of a0 if \reg does not hold its pattern. */
  .macro CHECK reg, n, bit
  li t0, TEST_SPIN_PATTERN(\n)
  beq \reg, t0, 1f
  li t1, 1 << \bit
  or a0, a0, t1
1:
  .endm

  .macro SPIN name, store
  .text
  .global \name, @function
  .balign 4
\name:
  addi sp, sp, -16*4
  sw ra, 0*4(sp)
  sw s0, 1*4(sp)
  sw s1, 2*4(sp)
  sw s2, 3*4(sp)
  sw s3, 4*4(sp)
  sw s4, 5*4(sp)
  sw s5, 6*4(sp)
  sw s6, 7*4(sp)
  sw s7, 8*4(sp)
  sw s8, 9*4(sp)
  sw s9, 10*4(sp)
  sw s10, 11*4(sp)
  sw s11, 12*4(sp)

  /* The arming store below uses t0 and t1, which no pattern touches. */
  mv t0, a2
  mv t1, a3

  PATTERN t2, 0
  PATTERN t3, 1
  PATTERN t4, 2
  PATTERN t5, 3
  PATTERN t6, 4
  PATTERN a2, 5
  PATTERN a3, 6
  PATTERN a4, 7
  PATTERN a5, 8
  PATTERN a6, 9
  PATTERN a7, 10
  PATTERN s0, 11
  PATTERN s1, 12
  PATTERN s2, 13
  PATTERN s3, 14
  PATTERN s4, 15
  PATTERN s5, 16
  PATTERN s6, 17
  PATTERN s7, 18
  PATTERN s8, 19
  PATTERN s9, 20
  PATTERN s10, 21
  PATTERN s11, 22

  \store t1, 0(t0)
  li t0, CLINT_BASE_ADDRESS+CLINT_MTIME_OFFSET
2:
  lw t1, 0(a0)
  bnez t1, 3f
  lw t1, 0(t0)
  sub t1, t1, a1
  bltz t1, 2b
  li a0, 0x80000000
  j 4f
3:
  li a0, 0
4:
  CHECK t2, 0, 0
  CHECK t3, 1, 1
  CHECK t4, 2, 2
  CHECK t5, 3, 3
  CHECK t6, 4, 4
  CHECK a2, 5, 5
  CHECK a3, 6, 6
  CHECK a4, 7, 7
  CHECK a5, 8, 8
  CHECK a6, 9, 9
  CHECK a7, 10, 10
  CHECK s0, 11, 11
  CHECK s1, 12, 12
  CHECK s2, 13, 13
  CHECK s3, 14, 14
  CHECK s4, 15, 15
  CHECK s5, 16, 16
  CHECK s6, 17, 17
  CHECK s7, 18, 18
  CHECK s8, 19, 19
  CHECK s9, 20, 20
  CHECK s10, 21, 21
  CHECK s11, 22, 22

  lw ra, 0*4(sp)
  lw s0, 1*4(sp)
  lw s1, 2*4(sp)
  lw s2, 3*4(sp)
  lw s3, 4*4(sp)
  lw s4, 5*4(sp)
  lw s5, 6*4(sp)
  lw s6, 7*4(sp)
  lw s7, 8*4(sp)
  lw s8, 9*4(sp)
  lw s9, 10*4(sp)
  lw s10, 11*4(sp)
  lw s11, 12*4(sp)
  addi sp, sp, 16*4
  ret
  .endm

// clang-format on
#else /* !__ASSEMBLER__ */

#include <tk/tkernel.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/// The spin armed by a byte store (test/spin_with_registers.S), for the
/// interrupt suite and the UART levels of the interrupt-depth suites.
extern UW test_spin_with_registers(volatile UW *flag, UW deadline,
                                   volatile UB *reg, UB value);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __ASSEMBLER__ */

#endif /* UUID_01A1EDF6_7B13_7671_B2C3_34661FCD0C44 */
