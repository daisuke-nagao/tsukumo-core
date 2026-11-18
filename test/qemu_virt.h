/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// The devices of the QEMU virt machine that the QEMU suites raise interrupts
// from, and the mcause values of the machine interrupts.

#ifndef UUID_01A1EDF0_3941_7B70_BBB7_43A6186BAC4B
#define UUID_01A1EDF0_3941_7B70_BBB7_43A6186BAC4B

#include <tk/tkernel.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum {
  UART_IRQ = 10, // The PLIC source of the UART.
  UART_BASE = 0x10000000,
  UART_IER = UART_BASE + 1, // Interrupt enable register, a byte register.
  IER_THRE = 0x02,          // Transmitter holding register empty.
  RTC_IRQ = 11,             // The PLIC source of the goldfish RTC.
  RTC_BASE = 0x101000,
  RTC_TIME_LOW = RTC_BASE + 0x00,
  RTC_TIME_HIGH = RTC_BASE + 0x04,
  RTC_ALARM_LOW = RTC_BASE + 0x08,
  RTC_ALARM_HIGH = RTC_BASE + 0x0c,
  RTC_IRQ_ENABLED = RTC_BASE + 0x10,
  RTC_CLEAR_ALARM = RTC_BASE + 0x14,
  RTC_CLEAR_INTERRUPT = RTC_BASE + 0x1c,
};

static const UW MCAUSE_SOFTWARE = 0x80000003u; // Machine software interrupt
static const UW MCAUSE_TIMER = 0x80000007u;    // Machine timer interrupt
static const UW MCAUSE_EXTERNAL = 0x8000000bu; // Machine external interrupt

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A1EDF0_3941_7B70_BBB7_43A6186BAC4B */
