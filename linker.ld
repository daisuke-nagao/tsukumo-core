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
    PROVIDE(_text_start = .);
    *(.reset)
    *(.text .text.*)
    PROVIDE(_text_end = .);
  } >ram AT>ram

  .rodata : {
    . = ALIGN(16);
    PROVIDE(_rodata_start = .);
    *(.rodata .rodata.*)
    *(.note.* )
    PROVIDE(_rodata_end = .);
  } >ram AT>ram

  .data : {
    . = ALIGN(16);
    PROVIDE(_data_start = .);
    *(.data .data.*)
  } >ram AT>ram

  .sdata : {
    . = ALIGN(16);
    PROVIDE( __global_pointer$ = . + 0x800 );
    *(.sdata .sdata.*)
    PROVIDE(_data_end = .);
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
   * The size is for the nesting of the external interrupt, which is as deep as
   * the PLIC has priorities (7): each level pushes a 128 byte trap frame and
   * the frames of its handler, so 2048 bytes leave about 290 bytes a level. A
   * stack of 1024 would be full at 3 or 4 levels.
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

  /* Reserve 16 bytes per CPU (adjust as needed) */
  /*
  .cpu_specific_memory : {
    PROVIDE( _cpu_specific_memory = . );
    . = . + 16*1;
  } >ram AT>ram
  */
}
