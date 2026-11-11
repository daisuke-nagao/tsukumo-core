# Timer handler cost when many waits end in one tick

This note records how many instructions the timer interrupt handler takes when
1, 8 and 29 timed waits with the same limit end in the same tick. Read it with
[timer-delay-budget.md](timer-delay-budget.md), which sets the handler's
budget and measures the worst cases that this note leaves out.

## Summary

- With 29 waits, plus one wait of the test runner, ending in one call, the
  handler took 21439 instructions in the `debug` preset and 6651 in
  `minsizerel`. (`debug` and `minsizerel` are CMake presets in
  `CMakePresets.json` with the Debug and MinSizeRel build types.)
- That is 69% and 21% of the budget of 31250 instructions, which
  [timer-delay-budget.md](timer-delay-budget.md) sets: a tenth of a 10 ms tick
  when QEMU runs with `-icount shift=5`, under which each instruction counts as
  32 ns of guest time. The handler is inside the budget in every case here.
- The cost per ended wait rises with the number of waits, which fits the two
  queue scans per wait described below.
- The worst cases, at the kernel's limit of 31 waits and with a late
  interrupt, are in [timer-delay-budget.md](timer-delay-budget.md).
- The counts below predate UTC tick accounting (2026-11-11), which adds 10
  instructions in `debug` and 7 in `minsizerel` to each measured timer-handler
  invocation. See the [measurement record](#measurement-record).

## Why the cost grows with the number of waits

`tkmc_timer_handler()` in [timer.c](../../../kernel/timer.c) runs in the timer
interrupt, with interrupts disabled. For every wait it ends, it scans the
timer queue twice: once to find the earliest deadline, and once to take the
elapsed ticks off every entry. If n waits end in the same tick, the work grows
with the square of n. The kernel has at most 32 tasks (`CFN_MAX_TSKID`). A
comment in the handler says to revisit the scans when the instruction budget
that the `qemu-timer-cost` suites check is exceeded.

This note measured the cost so that a decision about the scans rests on a
number. It set no budget and changed no kernel code.

## What is measured

The instructions are counted with `minstret`, the RISC-V counter of retired
instructions, under `-icount`, as
[How instructions are counted](timer-delay-budget.md#how-instructions-are-counted)
explains.

- The handler is wrapped at link time (`--wrap=tkmc_timer_handler`). The
  wrapper reads `minstret` before and after the real handler, on the real trap
  path. The count leaves out the trap entry, the saving of registers,
  `schedule()` and the task switches.
- Each case starts n tasks that wait on a semaphore. After a tick edge (the
  point where a tick ends and the timer interrupt falls due), the case signals
  the semaphore, and every task then calls `tk_dly_tsk(50)`. All n sleeps
  start in the same tick and have the same limit, so they end in the same
  handler call. The case itself waits on a semaphore without a time limit, so
  it adds no entry to the timer queue.
- The wrapper counts, in each call, how many of the n sleeps the call ended.
  The suite reports the call that ended the most. The test runner
  (`test/runner.c`, which runs each test case in its own task) polls with its
  own timed sleep, and that sleep ended in the same call in every run here.
  Each count below therefore covers the n waits plus one other. The report line
  says how many others there were.
- A case fails if the sleeps do not have the same number of ticks left, which
  means the burst crossed a tick edge. It also fails if no handler call ended
  all n sleeps, or if a task did not wake.
- 29 is the most this suite can make: 32 tasks, less the idle task, the runner
  and the case. The kernel allows 31, every task but the idle task, and
  `qemu-timer-cost-max` measures the worst cases there (see
  [timer-delay-budget.md](timer-delay-budget.md)).

## Results

Instructions in the timer handler, under QEMU `-icount shift=5`:

| Waits ended in one call | `debug` | `minsizerel` |
| --- | --- | --- |
| 1 (+1 other) | 873 | 197 |
| 8 (+1 other) | 3589 | 1002 |
| 29 (+1 other) | 21439 | 6651 |

Instructions per ended wait, counting the other one (2, 9 and 30 waits):

| Waits ended in one call | `debug` | `minsizerel` |
| --- | --- | --- |
| 2 | 437 | 99 |
| 9 | 399 | 111 |
| 30 | 715 | 222 |

From 9 to 30 waits, the cost per wait rises by about 1.8 times in `debug` and
2.0 times in `minsizerel`. A cost that was only linear would not rise. This
fits the two scans per wait. Three points do not prove a square law, because a
quadratic curve passes through any three points.

A tick is 10 ms and holds 312500 instructions at shift 5. The handler with 29
waits and the runner's one takes about 6.9% of a tick in `debug` and 2.1% in
`minsizerel`.

## How to reproduce the counts

1. From the root of the repository, build with the `debug` or the
   `minsizerel` preset:

   ```shell
   cmake --preset debug
   cmake --build --preset debug
   ```

2. Run the suite and read the lines that start with `timer-cost:`. CTest prints
   the log of a passing test only with `-V`. The pattern also runs
   `qemu-timer-cost-max`, whose lines start with `timer-cost-max:`:

   ```shell
   ctest --test-dir ../build/debug -R qemu-timer-cost -V
   ```

   For `minsizerel`, use that preset name and `../build/minsizerel`.

The suite is registered only when CMake finds QEMU at configure time and
`QEMU_ICOUNT_SHIFT` is a number of 8 or less. The default is 5. Like
`qemu-mailbox-cost`, it has no host-clock run, because `minstret` counts
instructions only under `-icount`.

## Limits of the measurement

- It counts instructions, not time, and 312500 instructions per tick is a
  property of the `-icount` setting, so a board will differ (see
  [How instructions are counted](timer-delay-budget.md#how-instructions-are-counted)).
- Interrupts stay off for the whole trap. The time they stay off is longer
  than these counts, which leave out the trap entry, `schedule()` and the
  switches.
- The wrapper adds a few instructions of its own, the same for every case. The
  queue length and the state of the n tasks are read outside the instruction
  count.
- Only waits with the same limit, started in one tick, were measured here.
  Waits that end together because an interrupt came late have different
  deadlines; [timer-delay-budget.md](timer-delay-budget.md) measures them.
- The extra wait is the runner's. Its tick parity is fixed by the schedule
  under `-icount`, so it was there in every run, but a change to the runner or
  to the suite can move it out of the call. The report line shows it.
- At `QEMU_ICOUNT_SHIFT=8` a tick holds about 39000 instructions. The burst of
  29 may not fit in one tick, and the case then fails with the message that
  the waits did not start in one tick.
- The check that the sleeps began in one tick compares the ticks they have
  left. It assumes the timer queue stores a count of ticks for each entry.

## Measurement record

The counts were rechecked on 2026-11-09 (JST) with QEMU `-icount shift=5` in
`debug` and `minsizerel`, after `EI()` was changed to restore the interrupt
state that `DI()` saved (see
[What EI restores](../../kernel/interrupts.md#what-ei-restores)). The counts
of all nine cases of `qemu-timer-cost` matched those measured before that
change. The counts do not depend on host timing, and before the change two
runs of each preset gave the same counts.

On 2026-11-11 the timer handler gained one 64-bit addition when it accounts
for elapsed ticks for the UTC clock. Fresh measurements of the three rows
above gave 883, 3599 and 21449 instructions in `debug` and 204, 1009 and 6658
in `minsizerel`. That is 10 and 7 more instructions per measured handler
invocation, respectively, for each of these wait counts. The
[delay-budget record](timer-delay-budget.md#measurement-record) includes the
new 31-wait measurements for all six presets.
