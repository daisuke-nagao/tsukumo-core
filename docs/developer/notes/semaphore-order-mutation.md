# Semaphore request order: what the tests catch

This record is for developers who change how `tk_wai_sem()` orders requests,
or the semaphore tests. It shows which deliberately wrong versions of the order
rule the host tests and the QEMU semaphore tests detect.

Run on 2026-11-05 (JST).

## Summary

Fifteen wrong versions of the rule that decides whether a request takes units
at once were each put into `tk_wai_sem()` and run against both test sets. With
the earlier, baseline host tests, two of them (M9b and M11) passed every test.
The host tests were then widened, and every wrong version now fails at least
one host test, although several fail only one. Six wrong versions pass every
QEMU test, so for those six the host tests are the only check.

## The rule under test

`tk_wai_sem()` decides at once whether a request takes units or waits behind
the tasks that already wait (the `may_take` block in `kernel/semaphore.c`).
The rule depends on the semaphore's attributes:

| Attribute | Meaning |
| --- | --- |
| `TA_TFIFO`, `TA_TPRI` | The wait queue is in order of arrival, or of task priority. |
| `TA_FIRST` | The task at the head of the wait queue is served first. |
| `TA_CNT` | The task with the smallest request is served first. |

The current rule is:

- A request takes units at once only if enough are left.
- If tasks wait and the semaphore is not `TA_CNT`, the request also has to
  outrank the head of the queue. That is possible only under `TA_TPRI`, and
  only with a strictly higher priority.

A passing run of the correct rule does not show whether the tests would catch a
count-only rule or a wrong ordering check. So each wrong version below was run
against two test sets:

- the host tests: the PC unit tests, which the `host` preset builds into one
  program, `host-tests`;
- the QEMU semaphore tests: the four CTest tests that `ctest -R qemu-semaphore`
  selects, which are `qemu-semaphore`, `qemu-semaphore-wait-release` and their
  `-hostclock` runs on the host clock.

## Results

Each wrong version has a label from M0 to M13, plus M9b; M stands for mutation,
a deliberate change to the code that a test should detect. The columns are:

- "Host before": the host tests that failed with the baseline host tests,
  before the widening described in
  [What the results show](#what-the-results-show).
- "Host after": the host tests that failed with the widened host tests, checked
  in under `test/host/test_semaphore.c`.
- "QEMU": the cases of the `semaphore` suite that failed. They fail in both the
  instruction-count and the host-clock run. "none" means that all four QEMU
  tests passed. The QEMU tests are the same in both comparisons.

| Change to the rule | Host before | Host after | QEMU |
| --- | --- | --- | --- |
| M0 the old code: count only (`may_take = semcnt >= cnt`) | order table, for-ever, outranks-later | order table, head-not-later | `fifo_first_keeps_the_head_ahead`, `tpri_first_lets_only_a_higher_priority_pass`, `refused_request_is_served_after_the_head` |
| M1 refuse whenever a task waits, also under `TA_TPRI` (`TA_CNT` stays exempt) | order table | order table, head-not-later | `tpri_first_lets_only_a_higher_priority_pass` |
| M2 apply the rule under `TA_CNT` too | order table | order table | `cnt_lets_a_small_request_pass` |
| M3 `TA_TPRI` takes at once, whatever the priority | order table, outranks-later | order table, head-not-later | `tpri_first_lets_only_a_higher_priority_pass` |
| M4 `<=` for `<` | order table | order table, head-not-later | `tpri_first_lets_only_a_higher_priority_pass` |
| M5 `>` for `<` | order table, outranks-later | order table, head-not-later | `tpri_first_lets_only_a_higher_priority_pass` |
| M6 weigh the request against the last waiter, not the head | outranks-later | head-not-later | none |
| M7 skip the rule for a poll | order table, outranks-later | order table, head-not-later | `fifo_first_keeps_the_head_ahead`, `tpri_first_lets_only_a_higher_priority_pass` |
| M8 skip the rule for a request with a time limit | order table | order table, head-not-later | `refused_request_is_served_after_the_head` |
| M9 skip the rule for a wait for ever | for-ever | order table | none |
| M9b skip the rule for a wait for ever, under `TA_TPRI` only | none | order table | none |
| M10 let a request that may go first take units without a count check | order table | order table | none |
| M11 check one unit, not the request, when tasks wait | none | order table | none |
| M12 a refused request returns `E_TMOUT` instead of waiting | 38 tests | 37 tests | all four QEMU tests |
| M13 with no task waiting, take whenever any unit is left (`semcnt > 0`) | maximum-counts | maximum-counts | none |

The table shortens the test names. These three tests are checked in and
registered by `test/host/main.c`:

| Short name | Test |
| --- | --- |
| order table | `test_a_new_request_takes_units_only_in_the_order_of_the_semaphore` |
| head-not-later | `test_a_request_is_weighed_against_the_head_not_a_later_waiter` |
| maximum-counts | `test_the_maximum_counts_units_before_they_go_to_waiters` |

These two were baseline cases. The widened tests absorbed them, so they are not
separate tests in the checkout:

| Short name | Test |
| --- | --- |
| for-ever | `test_a_request_refused_by_the_order_waits_for_ever_without_a_timer` |
| outranks-later | `test_a_request_that_outranks_only_a_later_waiter_takes_nothing` |

M12 breaks every wait, so most host tests fail, and its two numbers count the
failing host tests. They differ by one because the for-ever test was removed.
The row is here only to show that the tests notice a request that should wait
and does not.

## What the results show

Two wrong versions passed every baseline host test and every QEMU test:

- M9b, which skips the rule for a wait for ever under `TA_TPRI`. The only test
  in which the order decided a request that waits for ever used
  `TA_TFIFO | TA_FIRST`.
- M11, a count check that uses one unit, not the units the request asks for.
  Every request in the order table asked for one unit.

Four more were caught by one baseline host test each, and by no QEMU case:

- M6, which compares with the last waiter. Only one test weighed a request
  against a queue of two waiters.
- M9, which skips the rule for every wait for ever. Only one test had the order
  decide a request that waits for ever.
- M10, a missing count check on the path that lets a request go first. In the
  baseline, only the rows of the order table with no unit left showed it.
- M13, a count check that takes any unit when no task waits. Only one test
  asks for more units than are left when no task waits.

The widened host tests differ from the baseline as follows:

- The order table also runs each row as a wait for ever, which queues the
  request with no timer. This is the for-ever test's case, which was the first
  row of the table.
- The order table has rows with a head that waits for three units, two units
  left, and requests for two and for three units.
- `test_a_request_is_weighed_against_the_head_not_a_later_waiter` replaces the
  single case of a request that outranks only the later waiter with a table of
  five priorities, each tried as a poll and as a wait with a time limit.

With the widened tests every wrong version fails a host test, but several fail
only one: M2, M9, M9b, M10 and M11 fail only the order table, M6 only
head-not-later, and M13 only maximum-counts. The existing tests were widened
instead of duplicated: the order table now covers M9b and M11, which no test
caught before, but no second test for the same change was added.
Head-not-later is still the only test that can tell the head of the queue from
its tail, so M6 is still caught by one test.

The QEMU tests were not changed. M6, M9, M9b, M10, M11 and M13 pass them, and
the host tests are what catch these. In the QEMU cases in which the order
decides a request, the head of the queue waits for ever and the request polls
(`TMO_POL`). The exception is `refused_request_is_served_after_the_head`, whose
request waits with a time limit, behind the head, under `TA_TFIFO`. No QEMU
case has the order refuse a request that then waits for ever, or weighs a
request against a queue of two under `TA_TPRI`.

## How to repeat it

These steps repeat the "Host after" and "QEMU" columns with the checked-in
tests. The "Host before" column records an earlier run with the baseline test
cases, which are not a separate checked-in test target.

Use a clean, disposable checkout with the build tools that
[Testing](../testing.md) describes. Run every command from the repository root.

1. Configure the correct implementation with `cmake --preset host` and
   `cmake --preset debug`. Then run the commands of step 3 to build it and to
   check that both test sets pass.
2. Edit the `may_take` block of `tk_wai_sem()` in `kernel/semaphore.c`: the
   lines from `BOOL may_take = ...` to the end of the `if` that follows. Apply
   one change from the results table, and keep the rest of the function
   unchanged.
3. Rebuild and run both test sets:

   ```shell
   cmake --build --preset host
   ctest --preset host
   cmake --build --preset debug
   ctest --test-dir ../build/debug -R "^qemu-semaphore" --output-on-failure --no-tests=error
   ```

4. Record the failing test names. Then undo the edit before you try another
   change; `git diff -- kernel/semaphore.c` must be empty after the
   restoration.
5. Rebuild and rerun the correct implementation to confirm that it passes.

The changes were made one at a time, and each one changed only the lines it
names.

## Notes on the method

- M0 replaces the whole ordering decision with the count-only predicate
  `BOOL may_take = semcb->semcnt >= cnt;` and leaves the following
  `if (may_take)` acquisition and the wait handling unchanged.
- M9b and M11 were written to match the gaps found in the tests. They are not
  mistakes from the history of the kernel.
- No change is equivalent to the correct code: every one failed at least one
  test.
