# Timer handler cost when many waits end in one tick

Measured on 2026-11-09 (JST), after the EI restore change, with QEMU
`-icount shift=5` in Debug and MinSizeRel. All nine handler counts match the
pre-change measurements.

## Purpose

`tkmc_timer_handler()` in [timer.c](../../../kernel/timer.c) runs in the timer
interrupt, with interrupts disabled. For every wait it ends, it scans the timer
queue twice: once to find the earliest deadline, and once to take the elapsed
ticks off every entry. If n waits end in the same tick, the work grows with the
square of n. The kernel has at most 32 tasks, and a comment in the handler says
to revisit the scans if the measured interrupt latency requires it.

This note records how many instructions the handler takes when 1, 8 and 29
waits end in the same tick, so that a decision about the scans rests on a
number. It does not make that decision, and the kernel is not changed.
[timer-delay-budget.md](timer-delay-budget.md) later set the budget and
extended the measurement.

## How to repeat it

1. Build with the `debug` or `minsizerel` preset.
2. Run the suite and read the lines that start with `timer-cost:`. CTest prints
   the log of a passing test only with `-V`:

   ```shell
   ctest --test-dir ../build/debug -R qemu-timer-cost -V
   ```

The suite is registered only when QEMU is found at configure time and
`QEMU_ICOUNT_SHIFT` is a number of 8 or less. The default is 5. Like
`qemu-mailbox-cost`, it has no host-clock run, because `minstret` counts
instructions only under `-icount`.

## What is measured

- The handler is wrapped at link time (`--wrap=tkmc_timer_handler`). The wrapper
  reads `minstret` before and after the real handler, on the real trap path.
  The count leaves out the trap entry, the saving of registers, `schedule()` and
  the task switches.
- Each case starts n tasks that wait on a semaphore. After a tick edge, the case
  signals the semaphore, and every task then calls `tk_dly_tsk(50)`. All n
  sleeps start in the same tick and have the same limit, so they end in the same
  handler call. The case itself waits on a semaphore without a time limit, so
  it adds no entry to the timer queue.
- The wrapper counts, in each call, how many of the n sleeps the call ended. The
  suite reports the call that ended the most. The runner polls with its own
  timed sleep, and that sleep ended in the same call in every run here, so
  each count below covers the n waits plus one other. The report line says how
  many others there were.
- A case fails if the sleeps do not have the same number of ticks left, which
  means the burst crossed a tick edge. It also fails if no handler call ended
  all n sleeps, or if a task did not wake.
- 29 is the most the kernel allows: 32 tasks, less the idle task, the runner and
  the case.

## Results

Instructions in the timer handler, under QEMU `-icount shift=5`. The counts do
not depend on host timing. The pre-change results were the same in two runs of
each preset; the post-change values shown here were rechecked on 2026-11-09.

| Waits ended in one call | `debug` | `minsizerel` |
|---|---|---|
| 1 (+1 other) | 873 | 197 |
| 8 (+1 other) | 3589 | 1002 |
| 29 (+1 other) | 21439 | 6651 |

Instructions per ended wait, counting the other one (2, 9 and 30 waits):

| Waits ended in one call | `debug` | `minsizerel` |
|---|---|---|
| 2 | 437 | 99 |
| 9 | 399 | 111 |
| 30 | 715 | 222 |

From 9 to 30 waits, the cost per wait rises by about 1.8 times in `debug` and
2.0 times in `minsizerel`. A cost that was only linear would not rise. This fits
the two scans per wait. Three points do not prove a square law, because a
quadratic curve passes through any three points.

A tick is 10 ms and holds about 300000 instructions at shift 5 (see
`CMakeLists.txt`). The handler with 29 waits and the runner's one takes about
7.1% of a tick in `debug` and 2.2% in `minsizerel`.

## Limits of the measurement

- It counts instructions, not time. QEMU models no cache and no memory delay,
  and 300000 instructions per tick is a property of the `-icount` setting, so a
  board will differ.
- Interrupts stay off for the whole trap. The time they stay off is longer than
  these counts, which leave out the trap entry, `schedule()` and the switches.
- The wrapper adds a few instructions of its own, the same for every case. The
  queue length and the state of the n tasks are read outside the instruction
  count.
- Only waits with the same limit, started in one tick, were measured here. Waits
  that end together because an interrupt came late have different deadlines;
  [timer-delay-budget.md](timer-delay-budget.md) measures them.
- The extra wait is the runner's. Its tick parity is fixed by the schedule under
  `-icount`, so it was there in every run, but a change to the runner or to
  the suite can move it out of the call. The report line shows it.
- At `QEMU_ICOUNT_SHIFT=8` a tick holds about 39000 instructions. The burst of
  29 may not fit in one tick, and the case then fails with the message that the
  waits did not start in one tick.
- The check that the sleeps began in one tick compares the ticks they have left.
  It assumes the timer queue stores a count of ticks for each entry.
- This note sets no budget for the handler. The budget, and the worst cases
  against it, are in [timer-delay-budget.md](timer-delay-budget.md).
