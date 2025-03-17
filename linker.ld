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

  /* Reserve 16 bytes per CPU (adjust as needed) */
  /*
  .cpu_specific_memory : {
    PROVIDE( _cpu_specific_memory = . );
    . = . + 16*1;
  } >ram AT>ram
  */
}

