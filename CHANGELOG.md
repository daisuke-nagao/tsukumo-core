# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Add the public `TSK_SELF` constant (value 0); an API accepts it only where
  documented (see [TSK_SELF](docs/kernel/API_Support_Status.md#tsk_self))

- Add QEMU tests that nest external interrupts three to seven levels deep with
  high-level and assembly handlers, check the PLIC completion order and measure
  the interrupt stack against the boot stack below it
  (see [deep nesting validation](docs/developer/notes/deep-interrupt-nesting-validation.md))

- Add `ClearInt` to discard one selected PLIC pending request while preserving
  other sources, controller settings and active interrupt contexts
  (see [pending clear](docs/kernel/interrupts.md#clearing-a-pending-request))

- Add `EndOfInt` for explicit PLIC completion, coordinated with high-level and
  assembly interrupt exits
  (see [interrupt completion](docs/kernel/interrupts.md#explicit-completion))

- Add `CheckInt` to query a PLIC source's pending bit without claiming it
  (see [interrupt behavior](docs/kernel/interrupts.md))

- Support RV32 `TA_ASM` task and PLIC external-interrupt entries, including
  `tk_ret_int` and an inline direct-return macro
  (see [assembly entry conventions](docs/kernel/assembly-entry.md))

- Add the millisecond `SYSTIM` type and `LOCAL`, `EXPORT` and `IMPORT` linkage macros to the public headers
- Add `tk_set_utc` and `tk_get_utc` for millisecond UTC system time and declare `TK_SUPPORT_UTC` true
  (see [docs/kernel/behavior.md](docs/kernel/behavior.md#system-time-utc))

- Add a QEMU test of the `in_b`, `in_h`, `in_d`, `out_b`, `out_h` and `out_d` port access functions
- Add PC tests of `TWF_ANDW`, `TWF_BITCLR`, a refused message priority of 0 and `TA_TFIFO` receiver order

### Changed

- Rename the PC tests and QEMU cases, all but the runner's self-test fixtures, to name what they test
  (see [naming rule](docs/developer/testing.md#name-a-test))
- Restructure the tests around shared helpers and tables, split the largest files and register CTest cases by name

### Removed

- Remove `kernel/asm/rv32/launch_task.S`, an old copy of `__launch_task` that no build compiled

### Fixed

- Return `E_PAR` when `tk_wai_flg` combines `TWF_CLR` and `TWF_BITCLR`
- Stop a `TA_HLNG` interrupt handler that returns with interrupts enabled from letting interrupts in while the
  kernel completes the interrupt and restores the interrupted context; such a handler no longer needs `DI()` before
  it returns (see [nested interrupts](docs/kernel/interrupts.md#nested-interrupts))
- Strengthen UTC reset and refusal regressions with elapsed and pending ticks
- Keep the never-return attribute of the panic APIs when `<stdnoreturn.h>` is included before the kernel headers
- Run the RV32 public-declaration link checks one at a time so that `ctest -j` no longer fails them at random
- Compile the RV32 kernel library with `TKERNEL_CHECK_CONST`, as the programs that link it already were, and make
  the CMake option of that name switch it for that library and the programs that link it; the option had no effect
  before
- Make three PC tests of a wake-up fail when the kernel does not record the cause of the wake-up

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
