# Instruction budget of the timer handler, and its worst case

Measured on 2026-11-06 (JST).

## Purpose

`tkmc_timer_handler()` in [timer.c](../../../kernel/timer.c) runs in the timer
interrupt, with interrupts disabled. [timer-handler-cost.md](timer-handler-cost.md)
counted its instructions when many waits with one limit end in one tick, and set
no budget. This note sets the budget and measures the worst cases that note left
out:

- waits with different limits, which the handler ends one after another in the
  order of their deadlines;
- an interrupt that comes late, so that one call counts every tick edge it
  missed and ends every wait those edges reach;
- waits on a `TA_CNT` semaphore, where the former timeout path walked the
  semaphore wait queue; the current path skips that walk;
- the loop that counts the missed edges, one by one, on its own.

## The budget

The handler, by itself, may take at most a tenth of a kernel tick. A tick is
10 ms and one instruction takes 2^shift ns under `-icount shift=<shift>`, so a
tick holds 312500 instructions at the default shift 5, and the budget is 31250
instructions. The budget is that count, whatever the shift: the handler spends
the same instructions at any shift, and the shift only changes how many a tick
holds. The count is the one `qemu-timer-cost` reads around the handler. It leaves
out the trap entry, the saving of registers, `schedule()` and the task switches,
as in [timer-handler-cost.md](timer-handler-cost.md).

Every case of `qemu-timer-cost` fails when the most expensive handler call of the
case is over the budget. The budget has to hold in the `debug` and the
`minsizerel` preset.

## How to repeat it

1. Build with the `debug` or `minsizerel` preset.
2. Run the suite and read the lines that start with `timer-cost:`:

   ```shell
   ctest --test-dir ../build/debug -R qemu-timer-cost -V
   ```

The suite is registered only when QEMU is found at configure time and
`QEMU_ICOUNT_SHIFT` is a number of 8 or less. The counts do not depend on host
timing. They were the same in two runs of each preset.

## What is measured

The handler is wrapped at link time, as before. The wrapper keeps the cost of
the most expensive call of the case. A line reports how many of the case's waits
ended in the call that ended the most of them, and how many other waits (the
runner's own polling sleep) ended in it.

Each case starts n tasks that wait for a signal and begin a timed wait right
after it, all in one tick. The check that they began in one tick fails the case
if a tick edge came between.

- **Same limit** (the cases of the first note): 1, 8 and 29 waits end in one call.
- **Limits one tick apart, on time**: 29 waits, the limits being 2 to 30 ticks
  (a wait counts one tick more than its limit in 10 ms steps). The task that
  begins first has the longest limit, so the timer queue does not list the waits
  in the order of their deadlines. Each call ends one wait.
- **Limits one tick apart, 1 tick late**: the case holds interrupts off until the
  machine time is 1 tick and an eighth of a tick past the pending compare value. The
  handler finds 2 edges passed.
- **Limits one tick apart, 29 ticks late**: the same with 29 ticks. The handler
  finds 30 edges passed, which reach every limit, so one call ends all 29 waits.
- **Semaphore waits, 29 ticks late**: as before, but the 29 tasks wait on one
  `TA_CNT` semaphore that nothing signals, with the same limits.
- **No waits, 1 and 200 ticks late**: the handler only counts the edges. The
  difference of the two, divided by the 199 edges between them, is the cost of
  one edge.

29 is the most waits the kernel allows: 32 tasks, less the idle task, the runner
and the case.

Besides the budget, the cases check what they claim to measure:

- The wrapper counts the edges that each handler call found, from the distance
  the call moved the compare value. A case that holds interrupts off for L ticks
  fails unless one call found exactly L + 1 edges, so a late interrupt that did
  not come cannot pass as an on-time one.
- In the cases with limits one tick apart, the tasks must leave their waits in
  the order of their deadlines, the last to begin first. The tasks have one
  priority, so they run in the order in which the handler made them ready.
- The semaphore case must stay within 3125 instructions (a tenth of the budget)
  of the case with `tk_dly_tsk()` waits, whose worst call it follows. That is
  what guards the change below in a preset where the budget alone would not
  show it.

What the cases do not check is that a late interrupt takes the passed ticks off
every wait, and that a wake-up request is kept when a wait ends. They did not
change, and the PC tests `test_a_timer_interrupt_counts_every_period_that_has_passed`,
`test_a_late_timer_interrupt_ends_only_the_waits_it_reached` and
`test_release_wait_keeps_a_queued_wakeup` check them.

## Results

Instructions of the most expensive handler call, under QEMU `-icount shift=5`.
The budget is 31250. The share of the budget is in brackets.

| Case | `debug` | `minsizerel` |
|---|---|---|
| 1 wait, same limit | 873 (3%) | 197 (1%) |
| 8 waits, same limit | 3589 (11%) | 1002 (3%) |
| 29 waits, same limit | 21439 (69%) | 6651 (21%) |
| 29 waits, 1 tick apart, on time | 3892 (12%) | 1184 (4%) |
| 29 waits, 1 tick apart, 1 tick late | 3983 (13%) | 1197 (4%) |
| 29 waits, 1 tick apart, 29 ticks late | 25731 (82%) | 7405 (24%) |
| 29 semaphore waits, 29 ticks late | 27471 (88%) | 7666 (25%) |
| no waits, 1 tick late | 708 (2%) | 139 (0.4%) |
| no waits, 200 ticks late | 18818 (60%) | 2725 (9%) |

The first three rows are the counts of the first note, and they did not change.
The worst `debug` case, the semaphore one, takes 88% of the budget.

When all the waits end in one call, distinct limits cost a little more than one
limit (25731 against 21439 in `debug`; the 29 extra edges the call counts account
for about 2640 of the 4292 at 91 each): the handler scans the queue for the
earliest deadline, and again to take the ticks off, for every wait it ends,
whatever the limits are. On time, with the waits spread over 29 calls, each call
is cheap.

### The semaphore case, before and after

Before the optimization described below, the semaphore case was over the budget
in `debug`: 38201 instructions (122%), and 11639 in `minsizerel`. The first run
of the case, with the tasks beginning their waits in the order of their
deadlines, took 36519 and 11233. Under `TA_CNT`, each time-out called
`tkmc_sem_serve_waiters()`, which walked the whole wait queue of the semaphore
and found nothing, as every request the units covered was served when they were
signalled. That was a third scan for each wait that ended. A wait that leaves a
`TA_CNT` queue frees nobody, so
`tkmc_complete_wait()` in [task.c](../../../kernel/task.c) now skips the walk for
`TA_CNT` and keeps it for `TA_FIRST`, where the departing head may have held back
the waiters behind it. The case dropped to 27471 in `debug` and 7666 in
`minsizerel`. The deadline order, the count of the ticks that passed and the
wake-up requests are not touched: the two scans of the handler stay as they are.

Two checks hold the change. In `debug` the budget fails without it. In
`minsizerel` the case is far inside the budget either way (11639 against 31250),
so the case also has to stay within a tenth of the budget of the delay case
(7666 against 7405 with the change, 11639 without it). On the PC, the checker of
the kernel queues now fails a `TA_CNT` waiter whose request the units cover, the
fact that makes the skip right, and `test_a_cnt_signal_serves_every_request_the_units_cover`
fails when a signal stops at the first request it serves.

The two scans stay because every case is inside the budget with them. A merge of
them would have to keep two rules: ties in the queue go in queue order, and the
queue is scanned again after each wait ends, because ending a wait can take other
entries off the timer queue.

### The late interrupt

The handler counts the missed edges one by one (a 64 bit division needs a library
the kernel does not link). From the two last rows, with `p` edges passed, the
count is about 526 + 91 p instructions in `debug` and 113 + 13 p in `minsizerel`.
An interrupt that is L ticks late finds p = L + 1 edges passed. By calculation,
not by measurement, the handler stays inside the budget for a lateness of up to:

| Condition | `debug` | `minsizerel` |
|---|---|---|
| no waits end | about 336 ticks (3.4 s) | about 2395 ticks (24 s) |
| 29 waits with distinct limits end (the semaphore row above, plus the extra edges) | about 70 ticks (0.7 s) | about 1843 ticks (18 s) |

The same loop runs in `tkmc_schedule_timer()`, in task context with interrupts
off, while a timer interrupt is pending, and costs the same per edge. A wait
started in that time counts the edges that have passed.

The kernel has no bound on how long interrupts stay off. The budget holds as long
as no section keeps them off for longer than the table says.

## Limits of the measurement

- It counts instructions, not time. QEMU models no cache and no memory delay,
  and 312500 instructions per tick is a property of the `-icount` setting, so a
  board will differ.
- Interrupts stay off for the whole trap. The time they stay off is longer than
  these counts, which leave out the trap entry, `schedule()` and the switches.
- The wrapper adds a few instructions of its own, the same for every case.
- Only `tk_dly_tsk()` waits and `TA_TFIFO | TA_CNT` semaphore waits were
  measured. After a time-out, `tkmc_complete_wait()` walks a queue only for a
  semaphore wait, so the waits on flags and mailboxes should cost as
  `tk_dly_tsk()` does, but they were not run.
- The semaphore case has one wait queue. Waiters of several semaphores that end
  in one call add no scan of their own, but were not run.
- The longest lateness measured is 200 ticks. The limits in the table extend the
  cost per edge, and were not run.
- The budget is 31250 instructions, a tenth of a tick at shift 5. At a larger
  `QEMU_ICOUNT_SHIFT` a tick holds fewer instructions, so the same count is a
  larger share of it (31250 is 80% of a tick at shift 8). The 29 waits of the
  first note may not fit in one tick at shift 8 either.
- Only the shift 5 counts were measured. The suite is registered for shifts 0 to
  8, but was run only at 5. The 200-tick case spins for two seconds of guest
  time, which is about 2e9 instructions at shift 0, so it may not finish inside
  the 30 s that ctest allows there.
