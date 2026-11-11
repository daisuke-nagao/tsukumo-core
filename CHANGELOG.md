# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- Keep the never-return attribute of the panic APIs when `<stdnoreturn.h>` is included before the kernel headers

## [0.0.1] - 2026-11-11

### Added

- Add tasks under a preemptive priority scheduler with create, start, exit, sleep, wake up, release and delay
  (see [docs/kernel/API_Support_Status.md](docs/kernel/API_Support_Status.md))
- Add semaphores with FIFO or priority wait queues and the `TA_FIRST` and `TA_CNT` request orders
- Add event flags, and mailboxes whose messages can be ordered by priority (`TA_MPRI`)
- Add time limits on waits, counted in 10 ms ticks of the 64-bit machine timer
- Add PLIC external interrupt handling and support priority nesting up to seven
  levels. Tests cover two-level nesting.
  (see [docs/kernel/interrupts.md](docs/kernel/interrupts.md))
- Add a shared 2048-byte interrupt stack for trap-handler C code, with the initial trap frame on each task stack
- Add I/O port access through `in_b`, `in_h`, `in_w`, `in_d`, `out_b`, `out_h`, `out_w` and `out_d`
- Add debugger-visible records and replaceable hooks for a failed startup and a fatal CPU trap
  (see [docs/kernel/behavior.md](docs/kernel/behavior.md))
- Add the RV32 port (`rv32imac`, `ilp32`) for the QEMU `virt` machine, with an idle task that halts with `wfi`
- Add CMake presets for three optimization levels, with and without link-time optimization, and for the PC tests
- Add QEMU test suites for instruction-count and host-clock runs
  (see [docs/developer/testing.md](docs/developer/testing.md))
- Add PC unit tests of the kernel logic on a fake machine, built with AddressSanitizer and UBSan
- Add instruction budgets for the interrupt-disabled time of the timer handler and of a mailbox send
  (see [docs/developer/notes/timer-delay-budget.md](docs/developer/notes/timer-delay-budget.md))
- Add documentation of the kernel's behavior, its interrupts and its conformance to the μT-Kernel 3.0 specification
  (see [docs/README.md](docs/README.md))
- Add a license check (`reuse lint`) and a Markdown check (`markdownlint-cli2`) to the pre-commit hooks
