# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Added host boundary tests of queued wakeups for ready, running and semaphore-waiting tasks, including unchanged queues and interrupt state on refusal and re-queuing after consumption. The `task-timer` QEMU suite also queues and consumes all 4095 requests for ready and semaphore-waiting tasks.
- Added tests of the counts a semaphore takes: nine on the PC (counts out of range and their order against `E_RSATR` and `E_LIMIT`, both ends of the range, signals past the maximum or near `INT_MAX` with and without waiters and with interrupts off, each refusal checked to change nothing) and the cases `counts_out_of_range_are_refused` and `signal_past_the_maximum_keeps_the_count` in the `semaphore` suite.
- Added tests of time limits near `INT_MAX`: a table on the PC over the five wait calls that checks the tick count for 1, 9, 10, 11, `INT_MAX - 9`, `INT_MAX - 8` and `INT_MAX` ms, and the case `longest_sleep_counts_its_ticks` in the `task-timer` QEMU suite.
- Added tests of calls with an ID out of range: a table on the PC over every call that takes an ID (0, -1, `INT_MIN` and one above the maximum are `E_ID` and change nothing; 1 and the maximum get past the check), rows for `tk_wai_flg()` with interrupts off, and cases for IDs 0 and -1 in the `task-restart` and `event-flag` QEMU suites.
- Added tests of wait calls made with interrupts off: six on the PC (every call and time limit, a call that could succeed at once, the order against the argument and object checks, and `tk_dly_tsk(0)` with interrupts on) and the `wait-context` QEMU suite (`qemu-wait-context`), which also checks that a refusal uses up no wake-up request, unit, pattern or message.
- Added tests of what `tk_cre_tsk()` refuses: fourteen on the PC (each rejected input breaks one rule only, and a refusal is checked to change nothing) and the `task-create` QEMU suite (`qemu-task-create`), which also covers the 32 bit address wrap and a negative stack size at a low address.
- Added host regressions for task frame reconstruction, runtime state reset and rejected starts, plus a `task-restart` QEMU suite that restarts the same task 100 times using both explicit exit and function return.
- Added host tests for full-width timer startup, low-word wrap, read-time carry and RV32 compare write order, plus a `timer-wrap` QEMU suite on both instruction-count and host clocks.
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
- `TK_SEMAPHORE_MAXCNT` is now 32767, the least that μT-Kernel 3.0 asks for, and `tk_cre_sem()` enforces it; it was 4095 and not checked.
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
- `tk_wup_tsk()` now enforces `TK_WAKEUP_MAXCNT` (4095) when queuing wakeups and returns `E_QOVR` without changing state at the limit. It used to allow counts almost up to `UINT_MAX`. Calls that depend on more than 4095 queued requests are now refused; waking a sleeping task still succeeds without queuing another request.
- `tk_cre_sem()` now refuses a maximum outside 1 to `TK_SEMAPHORE_MAXCNT` and an initial count outside 0 to the maximum with `E_PAR`, without taking a semaphore; it used to store any counts, a negative maximum included. An unsupported attribute is still `E_RSATR` and comes first, and `E_PAR` comes before `E_LIMIT`.
- `tk_sig_sem()` now returns `E_QOVR` and changes neither the count nor the waiters for any signal that would take the count past the maximum. The check was skipped for a signal larger than the maximum, so a semaphore of one unit could hold two, and a signal near `INT_MAX` overflowed the count. The maximum is checked before units go to the waiters, as before.
- `tk_dly_tsk()`, `tk_slp_tsk()`, `tk_wai_sem()`, `tk_wai_flg()` and `tk_rcv_mbx()` now accept every time limit up to `INT_MAX` ms (about 24.9 days), counted as before in 10 ms ticks rounded up plus one tick. The conversion added 9 to the signed time, which overflowed for times above `INT_MAX - 9`: undefined behavior, and on the RISC-V debug build a wait of about 4 billion ticks instead of 214748366.
- `tk_sta_tsk()`, `tk_wai_flg()` and `tk_set_flg()` now refuse an ID of 0 or below with `E_ID` and change nothing, as the other calls that take an ID already did. They checked only the upper bound, so such an ID formed an address in front of the task or flag table: `tk_sta_tsk()` read and could start whatever lay there, and `tk_set_flg()` wrote bits into it. This was also reached by passing the negative error code of a refused `tk_cre_tsk()` to `tk_sta_tsk()`.
- `tk_dly_tsk()`, `tk_slp_tsk()`, `tk_wai_sem()`, `tk_wai_flg()` and `tk_rcv_mbx()` now return `E_CTX` and change nothing when interrupts are off (in an interrupt handler, or between `DI()` and `EI()`), also for a poll, a call that could succeed at once and `tk_dly_tsk(0)`. A task is switched out when interrupts come back on, so such a call used to put the running task on a wait queue, return `E_OK` and leave the task in the waiting state until the next `EI()` switched it away. Argument errors (`E_ID`, `E_PAR`) are still reported first. The check reads the interrupt state, so a handler that enables interrupts again is not seen, and `tk_ext_tsk()` and `tk_exd_tsk()` called with interrupts off are not checked.
- `tk_cre_tsk()` now refuses a task it cannot start safely, with `E_PAR` and without taking a TCB: a NULL packet, entry or stack buffer, a priority outside 1 to `CFN_MAX_PRI`, a stack of 128 bytes (the saved frame) or less, a stack whose end wraps around the address space, and a stack whose end is not on a 16 byte boundary. It used to accept them: a bad priority indexed the ready queues out of range, a small stack put the saved frame below the buffer when the task started, and a NULL packet crashed the call. The 128 byte rule only says that the frame fits; how deep the task can go below it is up to the caller. An unsupported attribute is still `E_RSATR`, and is checked first.
- `tk_sta_tsk()` now rebuilds the full initial register frame on every start and clears previous wakeup and wait state. A restarted task no longer reuses a frame overwritten by the previous run's stack or inherits its queued wakeups.
- The machine timer now reads and schedules all 64 bits, with stable RV32 reads and safe split compare writes. Low-word wrap no longer breaks scheduling; delayed and duplicate interrupt tick accounting remains unchanged.
- `tk_del_sem()` on a semaphore that tasks wait for now releases them with `E_DLT`, as `tk_del_mbx()` does. It used to leave them waiting for ever, and linked the semaphore's wait queue to the free list with the waiters still on it; a waiter that left later (by its time limit, for one) then corrupted the free list.
- A task whose wait times out (semaphore, event flag, mailbox) is now taken off the wait queue of the object at once. It used to stay there until it ran again, and a signal or set that came in between made it ready a second time (a self-looped ready queue), replaced its time-out cause with E_OK, and spent a semaphore unit or cleared an event flag pattern on a task that no longer waited. `tk_rel_wai()` and `tk_wup_tsk()` also leave no time limit behind on the task now.
- The kernel now clears `.bss` at boot. `clear_bss()` declared the linker symbols as `int *` variables and read the memory at them as boundaries; that is 0 on QEMU, where RAM starts zeroed, so it cleared nothing. The symbols are now declared as arrays, and the kernel calls `tkmc_memset()`. On a board whose RAM is not zeroed, `.bss` was not cleared before.
- `tkmc_memset()` no longer shifts a value of 0x80 or more into the sign bit of an `int` (undefined behaviour) when it builds the fill word; it multiplies as unsigned instead. The bytes written are the same.
- A synchronous exception (illegal instruction, access fault, `ebreak`, ...) no longer makes the CPU run the same instruction again for ever. The kernel now stops, records `mcause`, `mepc` and `mtval` in `tkmc_last_trap` and calls the weak `tkmc_panic()`, which a board or a test can replace to report the cause. `qemu-panic-*` tests check an illegal instruction, a load access fault and an illegal instruction on a bad stack.

### Removed
- Deprecated `tkmc_launch_task` from `launch_task.s` as its functionality is now covered by `__launch_task` in `context_switch.s`.
- Removed custom linked list implementation in `kernel/task.c` in favor of the new `kernel/list.h`.
- Removed `tkmc_context_switch` in favor of the more flexible `tkmc_yield` function.
