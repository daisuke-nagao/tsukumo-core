# Wake-up context checks and the faults they detect

This record is for developers who change the context check of `tk_wup_tsk()`,
how the trap entries track the ISR state, or the wake-up tests. It shows
whether the existing tests detect three deliberately wrong versions of that
check.

Run on 2026-11-08 (JST).

## Summary

Three wrong versions of the context check in `tk_wup_tsk()`, labelled M0, M1
and M2, were each applied on their own and run against the host tests and the
QEMU suites `task-timer` and `wakeup-context`. Every wrong version failed host
tests and at least one QEMU case, each time at an assertion in the expected
case, and the correct implementation passed before and after the experiment.
The existing tests detect all three faults without additions. The experiment
did not cover real hardware, other optimization levels or other faults.

## The behavior under test

`tk_wup_tsk()` in `kernel/task.c` rejects a task that wakes itself with
`E_OBJ`, whether interrupts are enabled or disabled. The rule depends on the
ISR state:

- An ISR (interrupt service routine) is code that runs inside one of the
  kernel's trap entries. The RV32 trap entries count their nesting in the ISR
  depth, `tkmc_isr_depth`, and `tkmc_in_isr()` uses that depth to tell a task
  from an ISR. See [Kernel behavior](../../kernel/behavior.md#terms-used-on-this-page).
- `MIE` is the global interrupt-enable bit of the `mstatus` register.
  `DI(intsts)` clears it and saves the previous state in `intsts`, `EI(intsts)`
  puts that state back, and `isDI(intsts)` is true when the saved state had
  interrupts disabled. See [Interrupts](../../kernel/interrupts.md).
- `current` is the running task.

A timer ISR can queue a wake-up request for the task it interrupted, or wake
another task that sleeps. Disabling interrupts in a task does not give the task
the permissions of an ISR.

The self-wakeup check, after `DI(intsts)`, is:

```c
if (tcb == current && !tkmc_in_isr()) {
  ercd = E_OBJ;
}
```

When a call is permitted and the target task is running or ready, a wake-up
count below 4095 increases by one, and a full count returns `E_QOVR`. A
sleeping target instead becomes ready with `E_OK`, without a queued request. A
refused call leaves the task and queue state unchanged, and every call restores
its caller's interrupt state.

Three test sets check this:

- The host tests, the PC unit tests under `test/host/`, check these cases on a
  simulated machine (`test/host/fakes.c`) on which the test sets the ISR depth.
- The `wakeup-context` QEMU suite calls `tk_wup_tsk()` through the real timer
  trap, by a link-time wrap of the C timer handler `tkmc_timer_handler()`. It
  records the state before and after the call, then checks and consumes the
  requests after the trap has returned to the task.
- The `task-timer` QEMU suite checks self-wakeup from a task with interrupts
  enabled and disabled.

## The wrong versions

M0, M1 and M2 label the three wrong versions; M stands for mutation, a
deliberate change to the code that a test should detect. Each one was applied
on its own to a copy of the tracked sources, and only `tk_wup_tsk()` changed.

### M0: decide by the saved interrupt flag

Replace `tcb == current && !tkmc_in_isr()` with:

```c
tcb == current && !isDI(intsts)
```

A task with interrupts disabled then wrongly gets permission to queue its own
request. An ISR that has `MIE` enabled wrongly loses permission to wake the
task it interrupted. The QEMU case `isr_with_mie_on_is_still_an_isr` covers
the second effect: it turns `MIE` on in the timer ISR, with every interrupt
source masked so that no trap can nest, and it records the ISR depth separately
from the interrupt flag.

### M1: reject every wake-up of the current task

Replace `tcb == current && !tkmc_in_isr()` with:

```c
tcb == current
```

This also rejects the valid wake-up that an ISR makes for the task it
interrupted, including the full-count case that should return `E_QOVR`.

### M2: reject every call made with interrupts disabled

Immediately after `DI(intsts)` in `tk_wup_tsk()`, insert:

```c
if (isDI(intsts)) {
  EI(intsts);
  return E_CTX;
}
```

This wrongly rejects a wake-up of another task made with interrupts disabled,
which includes every ordinary wake-up from an ISR. The inserted code restores
the saved interrupt state before it returns, so the experiment tests the wrong
context rule alone and does not also corrupt the interrupt state.

## Method

- The host build and the RV32 Debug build used separate build directories.
- The correct implementation passed before the changes and after the
  restoration.
- The content hashes of the original tracked files and of the staged diff were
  identical before and after the experiments.

## Results

Every host run completed all 128 cases. Each QEMU run completed all 11 cases of
`task-timer` and all 5 cases of `wakeup-context`, on both clocks: the
instruction-count clock and the host clock (see
[Guest clock and host-clock runs](run_qemu.md#guest-clock-and-host-clock-runs)).
The QEMU columns count failures per clock; the two clocks gave identical
results.

| Implementation | Host failures | task-timer failures | wakeup-context failures |
| --- | --- | --- | --- |
| Correct, before the changes | 0 | 0 | 0 |
| M0 | 2 | 1 | 1 |
| M1 | 1 | 0 | 4 |
| M2 | 4 | 1 | 4 |
| Correct, restored | 0 | 0 | 0 |

Each expected failure was an assertion in its named case, and no run timed
out. With M2, `isr_wakes_a_sleeping_task` joined its child after the child's
bounded sleep expired, reached its assertion on the wake-up result, and
completed the suite. For every change, the last case of each suite ran on both
clocks.

The host failures were:

- M0 failed `test_a_task_cannot_wake_itself_with_interrupts_on_or_off` with
  expected `E_OBJ`, actual `E_OK`, and
  `test_wakeup_count_is_bounded_for_the_running_task_in_an_isr` with expected
  `E_OK`, actual `E_OBJ`.
- M1 failed `test_wakeup_count_is_bounded_for_the_running_task_in_an_isr`
  with expected `E_OK`, actual `E_OBJ`.
- M2 failed `test_wakeup_count_is_bounded_for_ready_and_non_sleep_waiting_tasks`,
  `test_wakeup_count_is_bounded_for_the_running_task_in_an_isr`, and
  `test_a_sleeper_is_woken_in_every_context` with expected `E_OK`, actual
  `E_CTX`. It also failed
  `test_a_task_cannot_wake_itself_with_interrupts_on_or_off` with expected
  `E_OBJ`, actual `E_CTX`.

The QEMU failures were:

- M0 failed `task_cannot_wake_itself` with expected `E_OBJ`, actual `E_OK`, and
  `isr_with_mie_on_is_still_an_isr` with expected `E_OK`, actual `E_OBJ`.
- M1 failed `isr_queues_a_wakeup_for_the_interrupted_task`,
  `isr_queues_the_last_wakeup_slot`, and `isr_with_mie_on_is_still_an_isr`
  with expected `E_OK`, actual `E_OBJ`. It also failed
  `isr_refuses_a_wakeup_past_the_limit` with expected `E_QOVR`, actual `E_OBJ`.
- M2 failed `task_cannot_wake_itself` with expected `E_OBJ`, actual `E_CTX`.
  It failed `isr_queues_a_wakeup_for_the_interrupted_task`,
  `isr_queues_the_last_wakeup_slot`, and `isr_wakes_a_sleeping_task` with
  expected `E_OK`, actual `E_CTX`. It also failed
  `isr_refuses_a_wakeup_past_the_limit` with expected `E_QOVR`, actual `E_CTX`.

## Repeating the experiment

Use a clean, disposable checkout with the tools that [Testing](../testing.md)
describes. From its root, run the following commands for the correct
implementation, then again after each change:

```shell
cmake --preset host
cmake --preset debug
cmake --build --preset host
ctest --preset host -V
cmake --build --preset debug
ctest --test-dir ../build/debug -R "^qemu-task-timer" -V --output-on-failure --no-tests=error
ctest --test-dir ../build/debug -R "^qemu-wakeup-context" -V --output-on-failure --no-tests=error
```

Keep `QEMU_ICOUNT_SHIFT=5`, the default, so that CTest registers both clocks.
Check the named assertion failures and the suite summaries. A build error,
zero tests, an unrelated failure or a timeout does not show that a change was
detected. Restore the function before you apply the next change, then rebuild
and rerun the correct version, and check that the source diff is empty.

## Environment and limits

- Windows x64, clang 21.1.8, CMake 4.3.1-msvc1, Unity 2.6.1 (the C unit-test
  framework the tests use), and QEMU 9.2.50 (a development build).
- The host build enabled AddressSanitizer and UBSan.
- The RV32 build used Debug, `rv32imac` and `ilp32`.
- QEMU used the `virt` machine with instruction-count shift 5, and a separate
  host-clock run.

With the correct implementation, the main working copy also passed the RV32
Debug build and all 82 CTest tests, including 33 host-clock tests.

These experiments cover the three faults described above. They did not test
real hardware, the full matrix of optimization levels and LTO, or other
possible faults.
