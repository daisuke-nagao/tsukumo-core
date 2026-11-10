# Timer handler instruction budget and worst case

This note sets the instruction budget for the timer interrupt handler,
`tkmc_timer_handler()` in [timer.c](../../../kernel/timer.c), and records how
close the handler comes to it in the worst cases the tests can build. Read it
before you change the handler or the timer queue, or when you need to know how
long the handler keeps interrupts disabled. It also explains how the QEMU cost
suites count instructions, for this note and the other measurement notes.

## Summary

- The budget is 31250 instructions for one handler call. That is a tenth of a
  10 ms kernel tick when QEMU runs with `-icount shift=5`, the default for the
  QEMU tests, under which each instruction counts as 32 ns of guest time (see
  [How instructions are counted](#how-instructions-are-counted)).
- The worst measured call took 29223 instructions, 94% of the budget: 31
  semaphore waits, the most the kernel allows, ending in one timer interrupt
  that came 31 ticks late, in the `debug` preset. The same case took 8159
  instructions (26%) in `minsizerel`. (`debug` and `minsizerel` are CMake
  presets in `CMakePresets.json` with the Debug and MinSizeRel build types.)
- Every case in the [results](#results) is inside the budget in `debug` and
  `minsizerel`. The two cases at the kernel's limit of 31 waits were measured
  in all six tested presets and are inside the budget in each, with 2027
  instructions to spare in the worst one. The `qemu-timer-cost` and
  `qemu-timer-cost-max` QEMU test suites (listed in [run_qemu.md](run_qemu.md))
  fail when a case goes over the budget.
- By calculation, the handler stays inside the budget for a timer interrupt up
  to about 53 ticks (0.5 s) late when 31 waits end, and about 336 ticks (3.4 s)
  late when none end, in `debug` (see
  [How late the timer interrupt can be](#how-late-the-timer-interrupt-can-be)).
  The kernel does not bound how long interrupts stay disabled, so this holds
  only while no code keeps them off for longer.

## The budget

The handler runs in the timer interrupt, with interrupts disabled. The budget
allows the handler, by itself, at most a tenth of a kernel tick: 31250
instructions. The count is the one the cost suites read around the handler. It
leaves out the trap entry, the saving of registers, `schedule()` and the task
switches.

The budget is that count at any `-icount` shift. The handler spends the same
instructions at any shift, and the shift only changes how many a tick holds.
[mailbox-di-budget.md](mailbox-di-budget.md) applies the same budget to
`tk_snd_mbx()`, so the kernel has one budget for the time that interrupts stay
disabled.

Every case of `qemu-timer-cost` fails when the most expensive handler call of
the case is over the budget, and every case of `qemu-timer-cost-max` fails
when its late call, the one it measures, is over it. The budget has to hold in
the `debug` and the `minsizerel` preset.

## How instructions are counted

The cost suites (`qemu-timer-cost`, `qemu-timer-cost-max` and
`qemu-mailbox-cost`) count instructions, not time.
[run_qemu.md](run_qemu.md) describes how the QEMU suites run.

- The QEMU tests run QEMU with `-icount shift=N,sleep=off`, where N is the
  CMake cache variable `QEMU_ICOUNT_SHIFT` (default 5, set in
  `CMakeLists.txt`). Under `-icount`, guest
  time follows the number of instructions the guest has run, and one
  instruction takes 2^N ns. At shift 5 an instruction takes 32 ns, so a 10 ms
  tick holds 312500 instructions. At shift 8 a tick holds about 39000.
- `minstret` is the RISC-V machine-mode counter of retired (completed)
  instructions. A suite reads it before and after the code it measures, and
  the difference is the count.
- QEMU counts instructions in `minstret` only under `-icount`. On the host
  clock it returns host timer ticks, which are not comparable. The cost suites
  therefore have no `-hostclock` run (the extra host-clock run that other QEMU
  suites get), and CMake registers them only when it finds QEMU at configure
  time and `QEMU_ICOUNT_SHIFT` is a number of 8 or less.
- The counts do not depend on host timing.
- QEMU models no cache and no memory delay, and 312500 instructions per tick
  is a property of the `-icount` setting. A board will differ.

## What is measured

The suites wrap the handler at link time: the linker option
`--wrap=tkmc_timer_handler` sends the trap's call to a test wrapper, which
reads `minstret` around the real handler on the real trap path. The wrapper
keeps the cost of the most expensive call of each case. Each report line says
how many of the case's waits ended in the call that ended the most of them,
and how many other waits ended in the same call. The other wait is the polling
sleep of the test runner (`test/runner.c`, which runs each test case in its
own task).

### Cases of qemu-timer-cost

Each case starts n tasks that wait for a signal and begin a timed wait right
after it, all in one tick. A check that they began in one tick fails the case
if a tick edge (the point where a tick ends and the timer interrupt falls due)
came between them.

| Case | Waits | What happens |
| --- | --- | --- |
| Same limit | 1, 8 and 29 | All waits end in one call. These are the cases of [timer-handler-cost.md](timer-handler-cost.md). |
| Limits one tick apart, on time | 29 | The limits are 2 to 30 ticks (the kernel counts a time limit in 10 ms ticks, rounded up, plus one). The task that begins first has the longest limit, so the timer queue does not list the waits in the order of their deadlines. Each call ends one wait. |
| Limits one tick apart, 1 tick late | 29 | The case holds interrupts off until the machine time is 1 tick and an eighth of a tick past the pending compare value (the `mtimecmp` time at which the next timer interrupt is due). The handler finds 2 edges passed. |
| Limits one tick apart, 29 ticks late | 29 | The same with 29 ticks. The handler finds 30 edges passed, which reach every limit, so one call ends all 29 waits. |
| Semaphore waits, 29 ticks late | 29 | As the previous case, but the 29 tasks wait with the same limits on one `TA_CNT` semaphore that nothing signals. Under `TA_CNT`, the semaphore serves any waiter whose request its count covers, wherever the waiter is in the queue. |
| No waits, 1 and 200 ticks late | 0 | The handler only counts the edges. The difference of the two counts, divided by the 199 edges between them, is the cost of one edge. |

29 is the most waits this suite can make: 32 tasks, less the idle task, the
runner and the case. It is not the kernel's limit, which is every task but the
idle task: 31 waits.

### Cases of qemu-timer-cost-max

The `qemu-timer-cost-max` suite has no runner, and measures the two late cases
with distinct limits at the kernel's limit. `usermain` (the function the
kernel's initial task calls) makes 30 tasks and waits itself. All 31 waits
begin in one tick, the longest first (32 ticks down to 2), and one call 31
ticks late ends them all. No task is left to hold interrupts off, so the
suite's wrapper of the handler makes the call late, inside the trap.

The suite checks the same budget and the same semaphore margin as
`qemu-timer-cost`. It also checks that the call found 32 edges and ended 31
waits, and that the timer queue lists the waits longest first, each a tick
shorter than the one before, which also shows that they began in one tick.

The order matters. The scan for the earliest deadline costs most when the
queue lists the waits latest first, and it costs about 1740 instructions less
in `debug` when the tasks queue the other way round. `usermain` wakes the
tasks by number for each case, so that they queue in the same order every
time. Woken by a semaphore, they queued in the order their last waits had
ended, and a first version of the suite measured its semaphore case in that
cheap order without noticing.

### Other checks of the cases

Besides the budget, the cases of `qemu-timer-cost` check that they measure
what they claim:

- The wrapper counts the edges that each handler call found, from the distance
  the call moved the compare value. A case that holds interrupts off for L
  ticks fails unless one call found exactly L + 1 edges, so a late interrupt
  that did not come cannot pass as an on-time one.
- In the cases with limits one tick apart, the tasks must leave their waits in
  the order of their deadlines, the last to begin first. The tasks have one
  priority, so they run in the order in which the handler made them ready.
- The semaphore case must stay within 3125 instructions (a tenth of the
  budget) of the case with `tk_dly_tsk()` waits, whose worst call it follows.
  That margin guards the skipped wait-queue walk (see
  [Appendix: why semaphore time-outs skip the wait-queue walk](#appendix-why-semaphore-time-outs-skip-the-wait-queue-walk))
  in a preset where the budget alone would not show it.

The cases do not check that a late interrupt takes the passed ticks off every
wait, or that a wake-up request is kept when a wait ends. Neither behavior
changed with the skipped wait-queue walk, and the PC unit tests
`test_a_timer_interrupt_counts_every_period_that_has_passed`,
`test_a_late_timer_interrupt_ends_only_the_waits_it_reached` and
`test_release_wait_keeps_a_queued_wakeup` check them. The PC unit tests run
the kernel sources on the development PC, without QEMU (see
[testing.md](../testing.md)).

## Results

Instructions of the most expensive handler call, under QEMU `-icount shift=5`.
The budget is 31250. The share of the budget is in brackets.

| Case | `debug` | `minsizerel` |
| --- | --- | --- |
| 1 wait, same limit | 873 (3%) | 197 (1%) |
| 8 waits, same limit | 3589 (11%) | 1002 (3%) |
| 29 waits, same limit | 21439 (69%) | 6651 (21%) |
| 29 waits, 1 tick apart, on time | 3892 (12%) | 1184 (4%) |
| 29 waits, 1 tick apart, 1 tick late | 3983 (13%) | 1197 (4%) |
| 29 waits, 1 tick apart, 29 ticks late | 25731 (82%) | 7405 (24%) |
| 29 semaphore waits, 29 ticks late | 27471 (88%) | 7666 (25%) |
| no waits, 1 tick late | 708 (2%) | 139 (0.4%) |
| no waits, 200 ticks late | 18818 (60%) | 2725 (9%) |
| 31 waits, 1 tick apart, 31 ticks late (`qemu-timer-cost-max`) | 27363 (88%) | 7880 (25%) |
| 31 semaphore waits, 31 ticks late (`qemu-timer-cost-max`) | 29223 (94%) | 8159 (26%) |

The worst `debug` case is the semaphore one: 88% of the budget with 29 waits
and 94% with 31.

### The kernel's limit in six presets

The two `qemu-timer-cost-max` cases were measured in all six tested presets.
Each preset is named after its CMake build type, and the `-lto` presets add
link-time optimization.

| Preset | 31 waits, 1 tick apart, 31 ticks late | 31 semaphore waits, 31 ticks late |
| --- | --- | --- |
| `debug` | 27363 | 29223 |
| `minsizerel` | 7880 | 8159 |
| `relwithdebinfo` | 7910 | 8189 |
| `debug-lto` | 27105 | 28965 |
| `relwithdebinfo-lto` | 7876 | 8155 |
| `minsizerel-lto` | 7847 | 8126 |

The worst of all is `debug`, the semaphore case, 29223 (94%). The kernel's
limit is inside the budget in all six presets, with 2027 instructions to spare
in `debug`.

### Why the handler keeps two scans per wait

For every wait it ends, the handler scans the timer queue for the earliest
deadline, and again to take the ticks off, whatever the limits are. When all
the waits end in one call, distinct limits therefore cost a little more than
one limit: 25731 against 21439 in `debug`. The 29 extra edges the call counts
account for about 2640 of the 4292 extra instructions, at 91 each. On time,
with the waits spread over 29 calls, each call is cheap.

The two scans stay because every case is inside the budget with them. A merge
of them would have to keep two rules: ties in the queue go in queue order, and
the queue is scanned again after each wait ends, because ending a wait can
take other entries off the timer queue.

## How late the timer interrupt can be

The handler counts the missed edges one by one, because a 64-bit division
needs a library that the kernel does not link. From the two rows without
waits, with `p` edges passed, the count is about 526 + 91 p instructions in
`debug` and 113 + 13 p in `minsizerel`. An interrupt that is L ticks late finds
p = L + 1 edges passed. By calculation, not by measurement, the handler stays
inside the budget for a lateness of up to:

| Condition | `debug` | `minsizerel` |
| --- | --- | --- |
| no waits end | about 336 ticks (3.4 s) | about 2395 ticks (24 s) |
| 29 waits with distinct limits end (the semaphore row above, plus the extra edges; the most this suite can make) | about 70 ticks (0.7 s) | about 1843 ticks (18 s) |
| 31 waits with distinct limits end (the kernel's limit: the semaphore rows of `qemu-timer-cost-max`, plus the extra edges) | about 53 ticks (0.5 s) | about 1807 ticks (18 s) |

The same loop runs in `tkmc_schedule_timer()`, in task context with
interrupts off, while a timer interrupt is pending, and costs the same per
edge. A wait started in that time counts the edges that have passed.

The kernel has no bound on how long interrupts stay off. The budget holds as
long as no section keeps them off for longer than the table says.

## How to reproduce the counts

1. From the root of the repository, build with the `debug` or the
   `minsizerel` preset:

   ```shell
   cmake --preset debug
   cmake --build --preset debug
   ```

2. Run the suites and read the lines that start with `timer-cost:` (29 waits
   at most) and `timer-cost-max:` (31 waits). CTest prints the log of a passing
   test only with `-V`. The pattern matches both suites:

   ```shell
   ctest --test-dir ../build/debug -R qemu-timer-cost -V
   ```

   For `minsizerel`, use that preset name and `../build/minsizerel`.

## Limits of the measurement

- It counts instructions, not time (see
  [How instructions are counted](#how-instructions-are-counted)).
- Interrupts stay off for the whole trap. The time they stay off is longer
  than these counts, which leave out the trap entry, `schedule()` and the
  switches.
- The wrapper adds a few instructions of its own, the same for every case.
- Only `tk_dly_tsk()` waits and `TA_TFIFO | TA_CNT` semaphore waits were
  measured. After a time-out, `tkmc_complete_wait()` walks a queue only for a
  semaphore wait, so the waits on flags and mailboxes should cost as
  `tk_dly_tsk()` does, but they were not run.
- The semaphore case has one wait queue. Waiters of several semaphores that
  end in one call add no scan of their own, but were not run.
- The longest lateness measured is 200 ticks. The limits in the lateness table
  extend the cost per edge, and were not run.
- The budget is 31250 instructions, a tenth of a tick at shift 5. At a larger
  `QEMU_ICOUNT_SHIFT` a tick holds fewer instructions, so the same count is a
  larger share of it (31250 is 80% of a tick at shift 8). The 29 waits of
  [timer-handler-cost.md](timer-handler-cost.md), or the 31 of
  `qemu-timer-cost-max`, may not fit in one tick at shift 8 either.
- Only the shift 5 counts were measured. The suite is registered for shifts 0
  to 8, but was run only at 5. The 200-tick case spins for two seconds of
  guest time, which is about 2e9 instructions at shift 0, so it may not finish
  inside the 30 s that ctest allows there.

## Measurement record

- The counts were rechecked on 2026-11-09 (JST) with QEMU `-icount shift=5` in
  `debug` and `minsizerel`, after `EI()` was changed to restore the interrupt
  state that `DI()` saved (see
  [What EI restores](../../kernel/interrupts.md#what-ei-restores)). The handler
  counts matched those measured before that change, which two runs of each
  preset had given alike, so their budget shares and the per-edge and lateness
  figures derived from them stayed the same.
- The 29-wait rows were read again on 2026-11-10, with the kernel changes of
  that day, and did not change.
- The 31-wait rows were measured on 2026-11-10 in all six presets.
- The first three rows of the results are the counts of
  [timer-handler-cost.md](timer-handler-cost.md), and they did not change.

## Appendix: why semaphore time-outs skip the wait-queue walk

Time-outs of `TA_CNT` semaphore waits used to cost a third scan for each wait
that ended. The semaphore case was then over the budget in `debug`: 38201
instructions (122%), and 11639 in `minsizerel`. The first run of the case,
with the tasks beginning their waits in the order of their deadlines, took
36519 and 11233.

Each time-out called `tkmc_sem_serve_waiters()`, which walked the whole wait
queue of the semaphore and found nothing. Under `TA_CNT`, every request that
the semaphore's units (its count) covered had been served when the units were
signalled. A wait that leaves a `TA_CNT` queue frees
nobody, so `tkmc_complete_wait()` in [task.c](../../../kernel/task.c) now
skips the walk for `TA_CNT`. It keeps the walk for `TA_FIRST`, the attribute
under which the waiter at the head of the queue is served first, because a
departing head may have held back the waiters behind it.

The case dropped to 27471 in `debug` and 7666 in `minsizerel`. The deadline
order, the count of the ticks that passed and the wake-up requests are not
touched, and the two scans of the handler stay as they are.

Two checks hold the change:

- In `debug`, the budget fails without it.
- In `minsizerel`, the case is far inside the budget either way (11639
  against 31250), so the case also has to stay within a tenth of the budget of
  the delay case: 7666 against 7405 with the change, 11639 without it.

In the PC unit tests, the checker of the kernel queues fails a `TA_CNT`
waiter whose request the units cover, which is the fact that makes the skip
correct. `test_a_cnt_signal_serves_every_request_the_units_cover` fails when a
signal stops at the first request it serves.
