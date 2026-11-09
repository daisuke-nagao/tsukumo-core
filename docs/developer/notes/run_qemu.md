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
- The QEMU tests run with `-icount shift=5,sleep=off`: guest time follows the
  number of instructions the guest has run, not the host clock, and jumps to
  the next timer deadline while the idle task halts in `wfi`. The UART log is
  then the same on every run and a suite takes a fraction of a second. Each passing
  suite also runs once on the host clock as `qemu-<suite>-hostclock` (label
  `host-clock`; `ctest -L host-clock` runs just those, `ctest -LE host-clock`
  skips them), because a fixed schedule can hide a race that only the host clock
  shows. Those run one at a time: a stalled host would stretch the guest's waits.
  Configure with `-DQEMU_ICOUNT_SHIFT=` (empty) to put every test on the host
  clock; `QEMU_ICOUNT_SHIFT` also accepts `auto`.
- `--no-tests=error` makes `ctest` fail when no test was registered (QEMU not
  found at configure time) instead of reporting success.
- `qemu-task-timer`, `qemu-task-restart`, `qemu-event-flag`, `qemu-semaphore`,
  `qemu-mailbox`, `qemu-memset` and `qemu-memcpy` run the kernel suites
  (`qemu-task-restart` restarts one task 100 times, ended by `tk_ext_tsk()` and
  by returning). The test runner (`test/runner.c`) runs each case in its own
  task, prints a result line per case and a summary, and ends QEMU with exit
  status 0 if no case failed and 1 if one did. A case that does not finish in
  time is reported as a timeout and aborts its suite.
- `word-access-tbaa` runs no QEMU, but is registered only with the QEMU suites,
  so `--no-tests=error` keeps working. It compiles `kernel/memcpy.c` and
  `kernel/memset.c` to LLVM IR and checks that their word loops access memory
  as a type that may alias any object (`test/check_word_tbaa.cmake`).
- `qemu-task-create` checks what `tk_cre_tsk()` refuses (priority, stack size,
  NULL entry or buffer, stack end alignment, and the 32 bit wrap of the stack
  end) with `E_PAR`, one case per input. It starts no task.
- `qemu-wait-context` checks that the wait calls (`tk_dly_tsk`, `tk_slp_tsk`,
  `tk_wai_sem`, `tk_wai_flg`, `tk_rcv_mbx`) return `E_CTX` and change nothing
  when interrupts are off. `DI()` stands in for an interrupt handler, which also
  runs with interrupts off. It also checks that `DI()` assigns a variable named
  `status`, the name of a local the macro used to declare.
- `qemu-idle-halt` reads `minstret` around a 100 ms sleep and checks that the
  sleep took about that long and that the guest retired between 1000 and 100000
  instructions, so an idle task that spins fails it. QEMU counts instructions
  in `minstret` only under `-icount`, so the suite has no `-hostclock` run. It
  is registered only when `QEMU_ICOUNT_SHIFT` is a number of 8 or less: with
  `auto` or a larger shift a spinning idle task would not run enough
  instructions to fail it.
  It also runs a lowest-priority child through repeated `tk_dly_tsk(0)` calls
  with only software interrupts enabled. Each yield must resume without a
  timer tick; an idle task that halts despite this ready child times out.
- `qemu-mailbox-cost` reads `minstret` around one mailbox call: that is how long
  the call keeps interrupts off. Three calls are checked on a mailbox that holds
  256 messages and on an empty one, and the counts may differ by at most 100
  instructions: `tk_del_mbx()`; a `TA_MPRI` send at or behind the last queued
  message; a send ahead of every message (the two sends also with 1 message). A send between two queued messages
  scans, so its time grows with the queue; it is held to a budget of 31250
  instructions (a tenth of a tick at shift 5) with 256 queued messages, for a
  send into the middle and one before the last message, which is the worst. After
  each send the case receives every message and checks that the sent one came out
  where the case means it to (see
  [mailbox-di-budget.md](mailbox-di-budget.md)). Like `qemu-idle-halt` it needs
  `-icount` with a shift of 8 or less, so it has no `-hostclock` run.
- `qemu-timer-cost` wraps `tkmc_timer_handler()` at link time and reads
  `minstret` around it. Cases: 1, 8 and 29 timed waits that end in the same
  tick; 29 waits with limits one tick apart, on time, 1 tick late and 29 ticks
  late (interrupts are held off, so that one call ends every wait it reaches);
  29 `TA_CNT` semaphore waits that time out 29 ticks late; and no waits with an
  interrupt 1 and 200 ticks late. A case reports the instructions of the most
  expensive handler call and fails when it is over the budget of 31250
  instructions (a tenth of a tick at shift 5), when the interrupt was not as
  late as planned, when the waits did not end as planned or not in the order of
  their deadlines, or when the semaphore waits cost much more than the same
  delays (see
  [timer-delay-budget.md](timer-delay-budget.md) and
  [timer-handler-cost.md](timer-handler-cost.md)). Like `qemu-mailbox-cost` it
  needs `-icount` with a shift of 8 or less, so it has no `-hostclock` run.
- `qemu-timer-switch` wraps `schedule()` at link time to record the `mcause` of
  the trap that runs it. A task that a timer interrupt wakes must be switched to
  in the timer trap (checked over four sleeps, because the runner can wake in
  the same trap and run first); a task woken by `tk_wup_tsk()` is switched to in
  the software interrupt; and a task that a timer trap preempts keeps the
  registers `t0`-`t6`, `a2`-`a7` and `s1`-`s11` it held.
- `qemu-interrupt` (and `qemu-interrupt-hostclock`) checks `tk_def_int()`,
  `EnableInt()` and `DisableInt()` with the transmitter-empty interrupt of the
  UART, which software raises and clears (PLIC source 10): the handler runs with
  its number as an ISR with interrupts off, a task it wakes is switched to in the
  same trap (`schedule()` is wrapped to record `mcause`), a disabled source
  waits, the default handler switches a source without a handler off, and bad
  numbers, levels and definitions are refused. The handler, the timer handler
  and `schedule()` must run on the interrupt stack with a depth of 1 in all
  three traps, a task keeps every register (`s0` included) through a trap, and
  the most the deepest paths use of that stack is measured. The goldfish RTC
  (PLIC source 11) is raised the same way, by an alarm at a time that has
  already passed (an alarm in the future would follow the host's clock, not the
  guest's), and nests in a UART handler that turns interrupts on: it is served
  inside the handler at a higher priority and after it at the same or a lower
  one, the timer waits for the handler, a task that the nested handler wakes
  runs once, after the outermost handler, and a task keeps every register
  through a nested trap. The claims QEMU cannot give are tested on the PC.
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
  and then starts the kernel as a board with dirty RAM would. The entry also
  leaves a value in `a0`, as boot code does, and `usermain()` must receive it
  (`BOOT ARG OK`). The suite has no test runner and prints `BSS OK` if no 0xa5
  is left; the ctest requires both lines.
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
