# Building and running the tests

This page explains how to build and run the kernel's tests and how to read
their results. Read it before you submit a change, or when a test fails. The
kernel rules that the tests check are in
[Kernel behavior](../kernel/behavior.md).

## Two kinds of tests

| Kind | What it is | Preset | Run with |
| --- | --- | --- | --- |
| QEMU tests | One RISC-V program per test suite (`tsukumo-core-test-<suite>.elf`). QEMU runs each one, and the suite ends the run by itself. | `debug` or another RISC-V preset | `ctest --test-dir <build dir> --output-on-failure --no-tests=error` |
| PC unit tests | One program for the PC, `host-tests`, that compiles the kernel's logic and runs it without QEMU | `host` | `ctest --preset host` |

Each preset builds in its own directory, `../build/<preset>`, beside the
repository.

## Run the QEMU tests

Before you start, check the following:

- CMake registers the QEMU tests only if it finds `qemu-system-riscv32` when it
  configures the build. If it does not find QEMU, it prints a warning. In that
  case, configure again with
  `-DQEMU_EXECUTABLE=<path to qemu-system-riscv32>`.
- `ctest` does not rebuild anything, so build first.

From the root of the repository:

1. Configure the build:

   ```shell
   cmake --preset debug
   ```

2. Build the kernel and every test suite:

   ```shell
   cmake --build --preset debug
   ```

3. Run the tests:

   ```shell
   ctest --test-dir ../build/debug --output-on-failure --no-tests=error
   ```

   `--no-tests=error` makes `ctest` fail when no test was registered, for
   example because CMake did not find QEMU, instead of reporting success.

Most suites are one CTest test named `qemu-<suite>`. The
[list of QEMU suites](notes/run_qemu.md) says what each one checks.

### How CTest decides a result

Each QEMU test has a time limit of 30 seconds. Within it, the rule depends on
the suite:

| Suites | The test passes when |
| --- | --- |
| Most suites | QEMU exits with status 0 and the output has no `:FAIL:` line. The test runner (`test/runner.c`) exits with 0 if every case passed and 1 if one failed. |
| `panic-*`, `startup-*`, `task-context-*`, `bss-clear`, `timer-cost-max` | The output has the suite's marker line and no `:FAIL:` line. CTest ignores the exit status, which alone cannot show that the suite did what it is about. |
| `selftest-<name>` pairs | `qemu-selftest-<name>-expectfail` passes when QEMU exits with a failure status, and `qemu-selftest-<name>-output` passes when the output shows the expected failed cases and the expected number of cases. Run both: `-expectfail` alone also passes when QEMU fails to start. |

The marker lines are:

| Suites | Marker |
| --- | --- |
| `panic-*` | `PANIC OK`: the kernel stopped at a deliberate fatal trap and reported the right cause. |
| `startup-success` | `STARTUP SUCCESS OK` |
| Other `startup-*` | `STARTUP FAILURE OK stage=` |
| `task-context-isr` | `1 Tests 0 Failures` |
| `task-context-no-current-wait` | `NO CURRENT WAIT OK` |
| Other `task-context-*` | `CONTEXT STOP OK` |
| `bss-clear` | `BOOT ARG OK`, then `BSS OK` |
| `timer-cost-max` | `TIMER COST MAX OK` |

The self-test suites check the test runner itself and are built to fail.
`selftest-pass` is the exception: it is one plain test, `qemu-selftest-pass`,
that must pass.

The build directory also holds `word-access-tbaa`. It runs no QEMU: it compiles
`kernel/memcpy.c` and `kernel/memset.c` to LLVM IR and checks that their word
loops access memory as a type that may alias any object. CMake registers it
only together with the QEMU tests, so
`--no-tests=error` still fails when QEMU is missing.

### Guest clock and host-clock runs

By default, each QEMU test runs with `-icount shift=5,sleep=off`: guest time
follows the number of instructions the guest runs instead of the host clock,
so the output is the same on every run. Because one fixed schedule can hide a
race, each suite that is meant to pass also runs once on the host clock, as
`qemu-<suite>-hostclock` with the label `host-clock`. These runs go one at a
time.

- To run only the host-clock tests, add `-L host-clock` to the `ctest`
  command. To skip them, add `-LE host-clock`.
- To run every test on the host clock with no extra tests, configure with
  `-DQEMU_ICOUNT_SHIFT=` (empty). The cache variable `QEMU_ICOUNT_SHIFT` sets
  the shift (default 5) and also accepts `auto`.
- The suites `idle-halt`, `mailbox-cost`, `timer-cost` and `timer-cost-max`
  count instructions, so they have no host-clock run. CMake registers them only
  when `QEMU_ICOUNT_SHIFT` is a number of 8 or less, and warns at configure
  time when it skips them.

[Guest clock and host-clock runs in the QEMU reference](notes/run_qemu.md)
explains the clock options in full.

## Run the tests at every optimization level

The suites should pass at every optimization level, with and without link-time
optimization (LTO), which lets the compiler optimize across source files. Three
presets set the level, and each has an LTO twin. Run the tests in all six:

| Preset | Optimization | LTO twin |
| --- | --- | --- |
| `debug` | `-O0` | `debug-lto` |
| `relwithdebinfo` | `-O2` | `relwithdebinfo-lto` |
| `minsizerel` | `-Os` | `minsizerel-lto` |

For example, for `relwithdebinfo-lto`:

```shell
cmake --preset relwithdebinfo-lto
cmake --build --preset relwithdebinfo-lto
ctest --test-dir ../build/relwithdebinfo-lto --output-on-failure --no-tests=error
```

At `-O0`, clang does not optimize functions across files, so `debug-lto` builds
with LTO but optimizes little. `CMakePresets.json` also has a `release` preset,
which has no LTO twin and is not one of the six.

Every new build directory downloads Unity when it is configured. To reuse a
copy without network access, see
[Configure without network access](#configure-without-network-access).

The [memory and mailbox validation record](notes/optimization-lto-validation.md)
explains why the optimizer is a risk for the kernel's memory copies and mailbox
links (aliasing between pointers of different types), and records the six
configurations, the steps to reproduce, the results and the limits.

## Run the PC unit tests

The PC unit tests cover the kernel's logic: lists, wait queues, the timer
handler, the PLIC driver and `tkmc_memset`. They need no QEMU and no RISC-V
toolchain: the `host` preset builds them with the PC's own clang, with
AddressSanitizer and UndefinedBehaviorSanitizer (UBSan) switched on.

```shell
cmake --preset host
cmake --build --preset host
ctest --preset host
```

### What the PC tests compile

The kernel sources are compiled unchanged. Two headers that touch the machine,
`include/sys/interrupt.h` and `include/sys/ioport.h`, are replaced by stand-ins
in `test/host/shadow/sys/`, which come first on the include path. The task
switch `__launch_task()` is replaced by a stand-in that fails the test.

| Source | In the PC tests |
| --- | --- |
| `kernel/context.c`, `task.c`, `timer.c`, `semaphore.c`, `event_flag.c`, `mailbox.c`, `intctl.c` | Compiled |
| `kernel/memset.c` | Compiled through `test/host/memset_under_test.c`, which disables its weak `memset` alias so that it does not clash with the C library |
| `kernel/memcpy.c` | Not compiled: its weak `memcpy` alias would clash with the C library. The `memcpy` QEMU suite tests it. |
| `kernel/start.c` (boot, linker symbols, the first task launch), `kernel/ini_tsk.c`, `kernel/panic.c` and the assembly files | Not compiled. The QEMU tests cover them. |

### What the PC tests cannot check

The PC tests run the kernel on a fake machine: a model of the interrupt flag,
a few device registers and a log of every access to them. It cannot switch
stacks. When a call would block, the test stops the call at that point and
checks the kernel's queues and task states. The tests therefore check what
happens when a task blocks and when it is woken, but not the values a woken
task returns, and no task ends there. The QEMU tests check those.

The PC has 64-bit pointers, and the kernel keeps 32-bit addresses in plain
integers, for example in a new task's initial frame. The PC tests do not check
those either. A 32-bit build (`-m32`) was not possible on the machine where
these tests were written, because its clang ships the sanitizer runtimes for
x86_64 only. [`test/host/fakes.h`](../../test/host/fakes.h) describes the fake
machine in full.

## Configure without network access

The tests use [Unity](https://github.com/ThrowTheSwitch/Unity) v2.6.1, a unit
test framework for C. The first configure of each build directory downloads it
with CMake's FetchContent module, so it needs network access. Without network
access, point `FETCHCONTENT_SOURCE_DIR_UNITY` at an existing copy of the Unity
source tree when you configure:

```shell
cmake --preset debug -DFETCHCONTENT_SOURCE_DIR_UNITY=<path to a Unity source tree>
```
