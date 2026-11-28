# Building and running the tests

This page explains how to build and run the kernel's tests and how to read
their results. Read it before you submit a change, or when a test fails. The
kernel rules that the tests check are in
[Kernel behavior](../kernel/behavior.md).

## Two kinds of tests

| Kind | What it is | Preset | Run with |
| --- | --- | --- | --- |
| QEMU tests | One RISC-V program per test suite (`tsukumo-core-test-<suite>.elf`). QEMU runs each one, and the suite ends the run by itself. | `debug` or another RISC-V preset | `ctest --test-dir <build dir> --output-on-failure --no-tests=error` |
| PC unit tests | One program for the PC, `host-tests`, that compiles the kernel's logic and runs it without QEMU. The preset also runs the three [public-declaration checks](#other-ctest-tests). | `host` | `ctest --preset host` |

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

### Other CTest tests

Besides the suites and `word-access-tbaa`, the build directory registers these
tests:

| Test | What it checks |
| --- | --- |
| `qemu-interrupt-completion-trace`, `qemu-asm-task-trace`, `qemu-interrupt-depth-<n>-trace` | Runs the suite's program on QEMU with a trace of memory writes. `test/check_interrupt_completion.py` checks the PLIC completions of each case against the set it keeps for the suite. |
| `interrupt-completion-trace-self-test` | The self-test of that script. It runs no QEMU. |
| `public-declarations-direct`, `public-declarations-umbrella-off`, `public-declarations-umbrella-on` | Two translation units that include the public headers directly, through the umbrella header, and through it with `TKERNEL_CHECK_CONST`, compile and link. The `host` preset runs each as a program. The RISC-V presets register each as `<name>-link`, which builds it, one at a time. |

The trace tests, the self-test and the `-link` checks are registered only when
CMake finds QEMU, like the suites.

### Building with TKERNEL_CHECK_CONST off

The tests are run with the CMake option `TKERNEL_CHECK_CONST` on, the default,
which declares the packet arguments of the system calls `const`. With it off,
the build and the tests still pass, but the suites `interrupt`,
`interrupt-depth-<n>`, `system-time`, `task-context-<case>` and `task-create`
warn that they pass a `const` packet to a parameter without `const`. Those
warnings are expected with the option off.

### Guest clock and host-clock runs

By default each test runs QEMU on an instruction-count clock:

```sh
qemu-system-riscv32 -machine virt -bios none -nographic -icount shift=5,sleep=off -kernel <suite ELF>
```

With `-icount`, guest time follows the number of instructions the guest has
run instead of the host clock: one instruction takes 2^shift ns, so 32 ns at
shift 5, and a 10 ms kernel tick holds 312500 instructions. With `sleep=off`,
guest time jumps to the next timer deadline while the idle task halts in `wfi`
(the RISC-V wait-for-interrupt instruction), instead of waiting in real time.
The tests wait on timers, so on the host clock a stalled host stretches the
guest's waits and runs differ. On the instruction-count clock the UART log is
the same on every run, and a suite takes a fraction of a second.

A fixed schedule can hide a race that only the host clock shows. So every suite
that is meant to pass also runs once on the host clock, as
`qemu-<suite>-hostclock`, with this command:

```sh
qemu-system-riscv32 -machine virt -bios none -nographic -kernel <suite ELF>
```

These tests carry the label `host-clock`: `ctest -L host-clock` runs only them,
and `ctest -LE host-clock` skips them. They run one at a time (CTest
`RUN_SERIAL`), because a stalled host would stretch the guest's waits.

The cache variable `QEMU_ICOUNT_SHIFT` selects the clock:

| `QEMU_ICOUNT_SHIFT` | Main tests run with | Main tests one at a time | `-hostclock` tests | Instruction-count suites |
| --- | --- | --- | --- | --- |
| A number (default `5`) | `-icount shift=<number>,sleep=off` | No | Registered | Registered only for a number of 8 or less |
| `auto` | `-icount shift=auto`, which lets QEMU follow the host clock | Yes | Registered | Not registered |
| Empty (`-DQEMU_ICOUNT_SHIFT=`) | The host clock | Yes | Not registered | Not registered |

The instruction-count suites are `idle-halt`, `mailbox-cost`, `timer-cost` and
`timer-cost-max`. They read `minstret`, the RISC-V counter of retired
instructions. QEMU counts instructions in `minstret` only under `-icount`; on
the host clock the counter returns host timer ticks, which are not comparable.
These suites therefore have no `-hostclock` run. They also depend on how many
instructions fit in a unit of guest time: with `auto` that ratio follows the
host, and with a shift above 8 a spinning idle task would run too few
instructions for `idle-halt` to tell it from a halted one. When CMake skips one
of these suites, it warns at configure time: `<suite>: not run, it needs
QEMU_ICOUNT_SHIFT to be a number of 8 or less`.

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

## Name a test

A test's name says what it tests and what it promises, so that a failure
report reads as the rule that broke. The rule is the same for the PC tests and
the QEMU cases:

1. Start with the subject. That is the API the test calls, in lower case with
   underscores (`tk_sig_sem`, `enable_int`, `end_of_int`). A test of several
   APIs or of a mechanism names the feature instead (`semaphore`,
   `timer_queue`, `interrupt_stack`).
2. Then state the promise in the present tense: what the subject does or
   keeps.
3. Leave out the articles a, an and the.
4. Start a PC test with `test_`. A QEMU case has no prefix.

For example, `test_tk_cre_sem_refuses_null_packet_and_keeps_free_slots` is a PC
test, and `semaphore_fifo_first_keeps_head_ahead` is a QEMU case.

The cases of the runner's self-test suites (`selftest-*`) are the exception.
They are fixtures named for the part they play, such as `pass_case` and
`hang_case`, and the CTest regular expressions in `CMakeLists.txt` refer to
those names.

## Configure without network access

The tests use [Unity](https://github.com/ThrowTheSwitch/Unity) v2.6.1, a unit
test framework for C. The first configure of each build directory downloads it
with CMake's FetchContent module, so it needs network access. Without network
access, point `FETCHCONTENT_SOURCE_DIR_UNITY` at an existing copy of the Unity
source tree when you configure:

```shell
cmake --preset debug -DFETCHCONTENT_SOURCE_DIR_UNITY=<path to a Unity source tree>
```
