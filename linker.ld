/*
 * SPDX-FileCopyrightText: 2024 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */
OUTPUT_ARCH("riscv")

ENTRY(_start)

MEMORY
{
  ram   (wxa) : ORIGIN = 0x80000000, LENGTH = 520K
}

SECTIONS
{
  .text : {
    *(.reset)
    *(.text .text.*)
  } >ram AT>ram

  .rodata : {
    . = ALIGN(16);
    *(.rodata .rodata.*)
    *(.note.* )
  } >ram AT>ram

  .data : {
    . = ALIGN(16);
    *(.data .data.*)
  } >ram AT>ram

  .sdata : {
    . = ALIGN(16);
    PROVIDE( __global_pointer$ = . + 0x800 );
    *(.sdata .sdata.*)
  } >ram AT>ram

  .sbss (NOLOAD) : {
    . = ALIGN(16);
    PROVIDE(_bss_start = .);
    *(.sbss .sbss.*)
  } >ram AT>ram

  .bss (NOLOAD) :{
    . = ALIGN(16);
    *(.bss .bss.*)
    PROVIDE(_bss_end = .);
  } >ram AT>ram

  .stack (NOLOAD) : {
    . = ALIGN(16);
    PROVIDE(_stack_start = .);
    . = . + 1024;
    PROVIDE(_stack_end = .);
  } >ram AT>ram

  /*
   * The stack the C parts of the trap entries run on (see start.S). It comes
   * after .stack and not before .bss: a stack grows down, so an overflow runs
   * into the section below. Below this one that is the boot stack, which is not
   * used after boot and which the fatal exception entry takes anew; below .bss
   * it would corrupt the kernel's tables. Nothing detects an overflow.
   *
   * Strictly increasing priorities 1 through 7 permit seven nesting levels.
   * Each nested entry needs a 128 byte trap frame and a 32 byte context, plus
   * the handler's call frames. The outer trap frame is on the task stack.
   * The interrupt-depth suites measure the tested handlers and check the
   * boundary. See docs/developer/notes/deep-interrupt-nesting-validation.md
   * for written high-water, reserved stack bounds and configuration limits.
   */
  .isr_stack (NOLOAD) : {
    . = ALIGN(16);
    PROVIDE(_isr_stack_start = .);
    . = . + 2048;
    PROVIDE(_isr_stack_end = .);
  } >ram AT>ram
  ASSERT(_isr_stack_end - _isr_stack_start == 2048,
         "the interrupt stack is not 2048 bytes")
  ASSERT(_isr_stack_start >= _stack_end,
         "the interrupt stack is not above the boot stack")
}
