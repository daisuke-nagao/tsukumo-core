# QEMU Usage Documentation

## How to Use the Scripts

### Linux
- **run_qemu.sh**: Launches QEMU normally. Example usage:
  ```sh
  ./run_qemu.sh
  ```

- **run_qemu_debug.sh**: Launches QEMU in debug mode with instruction-level tracing. Example usage:
  ```sh
  ./run_qemu_debug.sh
  ```

### Windows
- **run_qemu.bat**: Launches QEMU normally. Run it from Command Prompt.
- **run_qemu_debug.bat**: Launches QEMU in debug mode with instruction-level tracing.

The scripts boot the kernel image `tsukumo-core.elf`. It carries no tests, so it
idles until you stop QEMU with Ctrl+A, then X. The tests are separate ELFs; see
the next section.

## Running the Tests with CTest

Each test suite is its own ELF (`tsukumo-core-test-<suite>.elf`). When QEMU is
found at configure time, CMake registers one test per suite:

```sh
ctest --test-dir <build dir> --output-on-failure --no-tests=error
```

- `ctest` does not rebuild, so build first.
- The QEMU tests run with `-icount shift=5`: guest time follows the number of
  instructions the guest has run, not the host clock. The UART log is then the
  same on every run and a suite takes a fraction of a second. Each passing
  suite also runs once on the host clock as `qemu-<suite>-hostclock` (label
  `host-clock`; `ctest -L host-clock` runs just those, `ctest -LE host-clock`
  skips them), because a fixed schedule can hide a race that only the host clock
  shows. Those run one at a time: a stalled host would stretch the guest's waits.
  Configure with `-DQEMU_ICOUNT_SHIFT=` (empty) to put every test on the host
  clock; `QEMU_ICOUNT_SHIFT` also accepts `auto`.
- `--no-tests=error` makes `ctest` fail when no test was registered (QEMU not
  found at configure time) instead of reporting success.
- `qemu-task-timer`, `qemu-event-flag`, `qemu-semaphore`, `qemu-mailbox` and
  `qemu-memset` run the kernel suites. The test runner (`test/runner.c`) runs each case in its own
  task, prints a result line per case and a summary, and ends QEMU with exit
  status 0 if no case failed and 1 if one did. A case that does not finish in
  time is reported as a timeout and aborts its suite.
- `qemu-timer-wrap` moves the machine timer to 100 ms before a low-word wrap,
  with a nonzero high word, before starting the runner. A 300 ms delay must
  finish after the wrap. Its separate ELF keeps this clock jump out of the
  other suites; `qemu-timer-wrap-hostclock` repeats it on the host clock.
- `qemu-selftest-<name>-expectfail` and `qemu-selftest-<name>-output` check the
  test runner in `test/runner.c`. These `selftest-*` ELFs are built to fail, and
  the `-output` test checks which cases failed and how many ran. Run both of a
  pair: `-expectfail` is also green when QEMU fails to start. `selftest-pass` is
  the exception: one plain test, `qemu-selftest-pass`, that must pass.
- `qemu-bss-clear` checks that the kernel clears `.bss` at boot. QEMU starts with
  zeroed RAM, so `test/bss_dirty_entry.S`, linked first, fills `.bss` with 0xa5
  and then starts the kernel as a board with dirty RAM would. The suite has no
  test runner and prints `BSS OK` if no 0xa5 is left, which the ctest requires.
- `qemu-panic-illegal`, `qemu-panic-load-fault` and `qemu-panic-bad-stack` check
  that a fatal trap stops the kernel and reports its cause. The machine stops at
  the trap, so each ELF causes one trap; its replaced `tkmc_panic()`
  (`test/panic_hook.c`) prints `PANIC OK` only if mcause, mepc and mtval are as
  expected, and the ctest requires that line.

## QEMU Options Explanation

- `-machine virt`: Specifies the virtual board (virt board).
- `-kernel tsukumo-core.elf`: Loads the kernel image (tsukumo-core.elf).
- `-nographic`: Disables the GUI and uses the serial console.
- `-d in_asm`: Outputs instruction-level assembly logs (for debugging).
