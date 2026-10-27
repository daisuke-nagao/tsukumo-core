# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Added tests of deleting a semaphore that tasks wait for: five on the PC (release with `E_DLT`, the time limit of a released waiter, the free list afterwards, the task switch it asks for) and the case `delete_releases_waiters` in the `semaphore` suite.
- Added tests of waits that end by a time-out, then a signal or set before the task runs again: seven on the PC (`host-tests`, whose queue check now covers the wait queue of every semaphore, event flag and mailbox) and one case each in the `semaphore` and `event-flag` suites.
- Added the `bss-clear` test suite (`qemu-bss-clear`): `test/bss_dirty_entry.S` runs first, fills `.bss` with 0xa5 and starts the kernel as a board with dirty RAM would, and the suite checks that no 0xa5 is left.
- Added tests of `tkmc_memset()`: one matrix of every fill value, lengths 0 to 70 and offsets 0 to 3, run on the PC with UBSan (`host-tests`) and on QEMU (`qemu-memset`, with the shift check built in as a trap).
- Added a test runner (`test/runner.c`) that runs test cases one by one in their own tasks and prints a result line per case and a summary:
  - A failed assert ends only its own task, so later cases still run.
  - A case that does not finish in time is reported as a timeout and aborts the suite.
  - `selftest-*` ELFs check the runner itself and are run by `qemu-selftest-*` tests.
- Added unit tests of the kernel logic that run on the PC (`cmake --preset host`, `ctest --preset host`): a property test of `kernel/list.h` against a model, and tests of a single semaphore waiter, sleeping, time-outs (their rounding, order and the context switch they request) and the timer handler on a fake machine, built with AddressSanitizer and UBSan. The RISC-V build is selected by a toolchain file; without one the PC tests are built.
- The QEMU tests run with `-icount shift=5` by default (`QEMU_ICOUNT_SHIFT`), so guest time follows the instruction count: the UART log is the same on every run and the ctest set (without the host-clock runs, `ctest -LE host-clock`) takes about 3 seconds instead of 11. Each passing suite also runs once on the host clock (`qemu-<suite>-hostclock`, label `host-clock`) to catch races a fixed schedule hides. An empty `QEMU_ICOUNT_SHIFT` puts every test on the host clock.
- Split the kernel tests into suites, one ELF and one CTest each: `task-timer`, `event-flag`, `semaphore` and `mailbox`. Each case runs on its own, checks that its helper tasks really ran, and ends QEMU with a status that CTest judges.
- Added `CMakePresets.json` to define build and configure presets:
  - Presets include `debug`, `release`, `relwithdebinfo`, and `minsizerel` for different build configurations.
  - Common settings are inherited from the `base` preset, using the Ninja generator and exporting compile commands.
  - Supports `CMAKE_TOOLCHAIN_FILE` for RISC-V bare-metal toolchain configuration.
- Included `context_switch.s` to implement context switching with the `__context_switch` and `__launch_task` routines.
- Added `putstring.h` to provide a helper function for UART-based string output.
- Added `kernel/list.h` to implement a doubly linked list utility:
  - Provides functions like `tkmc_init_list_head`, `tkmc_list_add_tail`, `tkmc_list_del`, and `tkmc_list_empty`.
  - Macros for container manipulation (`tkmc_list_entry`, `tkmc_list_first_entry`).
- Enhanced task management in `main.c`:
  - Implemented `tkmc_create_task` for creating tasks with their stacks and entry points.
  - Added `tkmc_start_task` to mark tasks as ready.
  - Implemented `tkmc_context_switch` for cooperative multitasking between tasks.
  - Updated task initialization to create and launch `task1` and `task2`.
- Introduced `task1.c` and `task2.c` for demonstration tasks:
  - `task1` outputs "Hello, world." via UART using `putstring`.
  - `task2` outputs "FizzBuzz" via UART using `putstring`.
- Enhanced `typedef.h.in` with additional application-specific types:
  - Attributes (`ATR`), error codes (`ER`), function codes (`FN`), and priorities (`PRI`).
- Updated `CMakeLists.txt`:
  - Integrated `context_switch.s`, `task1.c`, `task2.c`, `putstring.h`, and `kernel/list.h`.
  - Enabled the generation of `typedef.h` from `typedef.h.in` using `DetermineTypes.cmake`.
  - Added new configuration options: `TK_HAS_DOUBLEWORD` for 64-bit support and `TK_SUPPORT_USEC` for microsecond-related types.

### Removed
- Removed the old chained tests (`test/task1.c` to `test/task4.c` and their helpers) and the `tsukumo-core-inject-fail.elf` check; the suites and the runner self-tests replace them.

### Changed
- Every way a wait ends (time limit, signal, set, mailbox delivery and delete, `tk_rel_wai()`, `tk_wup_tsk()`) now goes through one function, `tkmc_complete_wait()`, which takes the task off the timer queue and the wait queue of its object and makes it ready once.
- `tsukumo-core.elf` is now the kernel only (an empty `usermain`); the tests moved into the suite ELFs.
- Refactored `start.s`:
  - Improved alignment with `.balign 4`.
  - Updated handling of the global pointer (`gp`) with `.option norelax` and `.option relax`.
- Replaced the deprecated `tkmc_launch_task` in `launch_task.s` with `__launch_task` from `context_switch.s`.
- Applied consistent SPDX license header formatting across all source files.
- Refactored task management in `kernel/task.c` and `kernel/start.c`:
  - Replaced custom TCB linked list management with the new `tkmc_list_head` from `list.h`.
  - Added priority (`itskpri`) to `TCB` for task scheduling.
  - Simplified context switching logic with priority-based scheduling.

### Fixed
- `tk_del_sem()` on a semaphore that tasks wait for now releases them with `E_DLT`, as `tk_del_mbx()` does. It used to leave them waiting for ever, and linked the semaphore's wait queue to the free list with the waiters still on it; a waiter that left later (by its time limit, for one) then corrupted the free list.
- A task whose wait times out (semaphore, event flag, mailbox) is now taken off the wait queue of the object at once. It used to stay there until it ran again, and a signal or set that came in between made it ready a second time (a self-looped ready queue), replaced its time-out cause with E_OK, and spent a semaphore unit or cleared an event flag pattern on a task that no longer waited. `tk_rel_wai()` and `tk_wup_tsk()` also leave no time limit behind on the task now.
- The kernel now clears `.bss` at boot. `clear_bss()` declared the linker symbols as `int *` variables and read the memory at them as boundaries; that is 0 on QEMU, where RAM starts zeroed, so it cleared nothing. The symbols are now declared as arrays, and the kernel calls `tkmc_memset()`. On a board whose RAM is not zeroed, `.bss` was not cleared before.
- `tkmc_memset()` no longer shifts a value of 0x80 or more into the sign bit of an `int` (undefined behaviour) when it builds the fill word; it multiplies as unsigned instead. The bytes written are the same.
- A synchronous exception (illegal instruction, access fault, `ebreak`, ...) no longer makes the CPU run the same instruction again for ever. The kernel now stops, records `mcause`, `mepc` and `mtval` in `tkmc_last_trap` and calls the weak `tkmc_panic()`, which a board or a test can replace to report the cause. `qemu-panic-*` tests check an illegal instruction, a load access fault and an illegal instruction on a bad stack.

### Removed
- Deprecated `tkmc_launch_task` from `launch_task.s` as its functionality is now covered by `__launch_task` in `context_switch.s`.
- Removed custom linked list implementation in `kernel/task.c` in favor of the new `kernel/list.h`.
- Removed `tkmc_context_switch` in favor of the more flexible `tkmc_yield` function.
