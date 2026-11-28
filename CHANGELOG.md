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
- Declare the kernel's `schedule()` once, returning the `void *` frame, so that a wrapper of another type fails to build
- Drop include lines from the PC tests that `test/host/fakes.h` already provides or that nothing uses
- Define the timer and PLIC register addresses of the PC tests once, in `test/host/fakes.h`
- Share the PC tests' interrupt source numbers, a finite wait limit and the task priorities through `test/host/fakes.h`
- Drop the arguments that every caller of the `test_wait.c` object helpers passed with the same value
- Count creatable event flags and mailboxes with bounded fixture helpers, as semaphores already were
- Add `HOST_TRY_WAIT` for PC tests that need to know whether a call blocked without failing on it
- Size the PC test fixture's task stacks from `CFN_MAX_TSKID` instead of a copy of its value
- Give each row of the PC ISR-context call table its own effect check, so reordering the rows keeps the test right
- Merge the three PC tests that refuse waits inside `DI()`, in an ISR and without a task into one table
- Merge the PC tests of a semaphore head waiter leaving by time-out and by `tk_rel_wai` into one table
- Name the departure, row and interrupt state in every failure of the PC semaphore departure checks
- Share the loop over `ClearInt` configurations between the two PC tests that walk it
- Keep every PC test of the timer queue in `test_timer.c`
- Check each broken `tk_cre_tsk` packet with tasks running and ready, including which task slot is allocated next
- Put `test/` on the include path of Unity, the test runner and the QEMU suites only, not of the kernel and every
  other target
- Include the shared test headers by bare name in every QEMU suite
- Read the machine interrupt-enable bit in the QEMU suites through one `test_irq_enabled()` in `test/runner.h`
- Create the mailboxes of the mailbox QEMU suites with one `test_create_mailbox()` in the test runner
- Run both wake-up cases of the `task-timer` QEMU suite with one child task entry
- Drop the byte pattern seed that every caller in the `memcpy` QEMU suite passed with the same value
- Define the exception causes and addresses of the panic QEMU suites once, in `test/panic_expect.h`
- Reset the assembly-outer flag of the mixed nested interrupt cases before their checks, so a failed check no longer
  leaves it set for the next case
- Make `test/check_interrupt_completion.py` refuse a command line without `--expect=` instead of checking the
  `interrupt` suite's set by default
- Name the two-second spin limit of the `system-time` and `task-context` QEMU suites `SPIN_MS`, as other suites do
- Use the public `MSTATUS_MIE` and the shared `UART_BASE` in the QEMU tests instead of local copies, and say why the
  wake-up limit tests keep their own copy of `TK_WAKEUP_MAXCNT`
- Arm the UART level of the `interrupt-depth` QEMU suites with the shared `test_spin_with_registers`, dropping an
  identical copy of it
- Share the interrupt-stack, RTC, CSR and PLIC helpers of the `interrupt` and `interrupt-depth` QEMU suites through
  `test/isr_stack.h`, `test/qemu_virt.h` and `test/runner.h`
- Name the literal priorities and PLIC levels of the QEMU suites, and check each child priority against the runner and
  the case
- Take the include directories and definitions of the `word-access-tbaa` check from the kernel library, so that a
  setting added to the library reaches the check

### Removed

- Remove `kernel/asm/rv32/launch_task.S`, an old copy of `__launch_task` that no build compiled
- Remove six `PROVIDE` symbols from `linker.ld` that nothing referenced, so the linker never defined them
- Remove the C++ compiler and flags from `toolchain.cmake`; the project builds only C and assembly

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
- Make eight PC tests check the kernel queues, reset handler counters per PC test and delete two leaked QEMU objects

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

[Unreleased]: https://github.com/daisuke-nagao/tsukumo-core/compare/v0.0.1...HEAD
[0.0.1]: https://github.com/daisuke-nagao/tsukumo-core/releases/tag/v0.0.1
