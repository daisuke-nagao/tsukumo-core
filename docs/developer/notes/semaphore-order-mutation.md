# Semaphore request order: what the tests catch

Run on 2026-11-05 (JST).

## Purpose

`tk_wai_sem()` decides at once whether a request takes units, or waits
behind the tasks that already wait (`kernel/semaphore.c`, the `may_take`
block). The rule, added by commit `ae2c050` (F11):

- A request takes units at once only if enough are left.
- If tasks wait and the semaphore is not `TA_CNT`, the request also has to
  outrank the head of the queue. That is possible only under `TA_TPRI`, and
  only with a strictly higher priority.

`ae2c050` changed the kernel and added the tests in one commit, so nothing
showed that the tests fail on the old code, or on a wrong fix. This note
records that. Each wrong version of the rule below was run against the host
tests (`host-tests`) and the QEMU semaphore tests (the four CTest tests that
`ctest -R qemu-semaphore` selects).

## How to repeat it

1. Edit the `may_take` block of `tk_wai_sem()` in `kernel/semaphore.c` (the
   lines from `BOOL may_take = ...` to the end of the `if` that follows).
2. Build and run the PC tests: preset `host`, test `host-tests`.
3. Build and run the QEMU tests: preset `debug`, `ctest -R qemu-semaphore`.
4. Undo the edit. `git diff kernel/semaphore.c` must be empty.

The changes below were made one at a time. Each one changes only the lines it
names.

## Results

"Host before" is the set of host tests that failed with the tests as they were
when this branch began. "Host after" is the same change against the tests of
this branch. The QEMU tests are the same before and after. "QEMU" lists the
cases of the `semaphore` suite that failed (they fail in both the QEMU-clock
and the host-clock run); "none" means all four QEMU tests passed.

| Change to the rule | Host before | Host after | QEMU |
|---|---|---|---|
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

Test names are shortened. These tests exist on this branch:

- "order table" is
  `test_a_new_request_takes_units_only_in_the_order_of_the_semaphore`.
- "head-not-later" is
  `test_a_request_is_weighed_against_the_head_not_a_later_waiter`.
- "maximum-counts" is
  `test_the_maximum_counts_units_before_they_go_to_waiters`.

These two existed only before this branch, and are gone from it (see below):

- "for-ever" is
  `test_a_request_refused_by_the_order_waits_for_ever_without_a_timer`.
- "outranks-later" is
  `test_a_request_that_outranks_only_a_later_waiter_takes_nothing`.

M12 breaks every wait, so most host tests fail; the two numbers count the
failing host tests. The difference is the removed for-ever test. The row is
here only to show that a request that should wait, and does not, is noticed.

## What the check found

Two changes passed every test, on the host and on QEMU, when this branch began:

- M9b, a rule that is skipped for a wait for ever under `TA_TPRI`. The only
  test in which the order decided a request that waits for ever used
  `TA_TFIFO | TA_FIRST`.
- M11, a count check that uses one unit and not the units the request asks
  for. Every request in the order table asked for one unit.

Four more were caught by one host test each, and by no QEMU case:

- M6, which compares with the last waiter. Only one test weighed a request
  against a queue of two waiters.
- M9, a rule that is skipped for every wait for ever. Only one test had the
  order decide a request that waits for ever.
- M10, a missing count check on the path that lets a request go first. When
  the branch began, only the rows of the order table with no unit left showed
  it.
- M13, a count check that takes any unit when no task waits. Only one test
  asks for more units than are left when no task waits.

The tests of this branch change the following:

- The order table also runs each row as a wait for ever, which queues the
  request with no timer. This is the for-ever test's case, which was the
  first row of the table.
- The order table has rows with a head that waits for three units, two left,
  and requests for two and for three units.
- `test_a_request_is_weighed_against_the_head_not_a_later_waiter` replaces the
  single case of a request that outranks only the later waiter with a table of
  five priorities, each tried as a poll and as a wait with a time limit.

After this branch every change still fails a host test, but several fail only
one: M2, M9, M9b, M10 and M11 fail only the order table, M6 only
head-not-later, and M13 only maximum-counts. The tests were made wider, not
duplicated: the order table now covers M9b and M11, which nothing did, but a
second test for the same change was not added. Head-not-later is still the
only test that can tell a head from a tail, so M6 is still caught by one test.

The QEMU tests are not changed. M6, M9, M9b, M10, M11 and M13 pass them, and the
host tests are what catch these. In the QEMU cases in which the order decides
a request, the head of the queue waits for ever and the request polls
(`TMO_POL`). The exception is `refused_request_is_served_after_the_head`, whose request waits
with a time limit, behind the head, under `TA_TFIFO`. No QEMU case has the
order refuse a request that then waits for ever, or weighs a request against a
queue of two under `TA_TPRI`.

## Notes on the method

- M0 matches the code before `ae2c050` (`if (semcb->semcnt >= cnt)` in
  `tk_wai_sem()`).
- M9b and M11 are written to match the gaps found in the tests; they are not
  mistakes seen in the history of the kernel.
- No change is equivalent to the correct code: every one failed at least one
  test.
