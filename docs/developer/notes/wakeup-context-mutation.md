# Wake-up context checks and the faults they detect

Run on 2026-11-08 (JST).

## Contract under test

`tk_wup_tsk()` in `kernel/task.c` rejects a task waking itself with `E_OBJ`,
with interrupts enabled or disabled. The RV32 trap entries maintain ISR depth,
and `tkmc_in_isr()` uses that depth to distinguish a task from an ISR. A timer
ISR can queue a request for the task it interrupted, or wake another sleeping
task. Disabling interrupts in a task does not give it ISR permissions.

The self-wakeup check, after `DI(intsts)`, is:

```c
if (tcb == current && !tkmc_in_isr()) {
  ercd = E_OBJ;
}
```

For a permitted notification to a running or ready task, a count below 4095
increases by one. A full count returns `E_QOVR`. A sleeping task instead becomes
ready with `E_OK`, without accumulating a request. Refusal preserves task and
queue state, and every call restores its caller's interrupt state.

The host tests check these cases using a simulated machine and ISR depth. The
`wakeup-context` QEMU suite calls the API through the real timer trap with a
link-time wrapper around the C timer handler. It observes state before and
after the call, then checks and consumes requests after returning to the task.
The `task-timer` suite checks self-wakeup from a task with interrupts on and off.

## Isolated changes

Each change below was applied separately to a copy of the tracked sources.
Only `tk_wup_tsk()` was changed. Host and RV32 Debug builds used separate build
directories. The correct implementation passed before the changes and after
restoration. Content hashes of the original tracked files and the staged diff
were identical before and after the experiments.

### M0, decide by the saved interrupt flag

Replace `tcb == current && !tkmc_in_isr()` with:

```c
tcb == current && !isDI(intsts)
```

A task with interrupts disabled incorrectly gets permission to queue its own
request. An ISR whose MIE is enabled incorrectly loses permission to notify
the interrupted task. The QEMU MIE-enabled ISR probe masks interrupt sources
while enabling MIE, and records ISR depth independently of the interrupt flag.

### M1, reject every notification to current

Replace `tcb == current && !tkmc_in_isr()` with:

```c
tcb == current
```

This also rejects the valid ISR notification to the task it interrupted,
including the full-count case that should return `E_QOVR`.

### M2, reject every call with saved interrupts disabled

Immediately after `DI(intsts)` in `tk_wup_tsk()`, insert:

```c
if (isDI(intsts)) {
  EI(intsts);
  return E_CTX;
}
```

This incorrectly rejects notifications to another task made with interrupts
disabled, including ordinary ISR notifications. Restoring the saved interrupt
state makes this experiment test the wrong context policy independently of
interrupt-state corruption.

## Recorded results

Every host run completed all 128 cases. Each QEMU run completed all 11 cases
of `task-timer` and all five cases of `wakeup-context`, on both clocks. Counts
in the QEMU columns are failures per clock, with identical results for the
instruction-count and host-clock runs.

| Implementation | Host failures | task-timer failures | wakeup-context failures |
| --- | --- | --- | --- |
| Correct, before the changes | 0 | 0 | 0 |
| M0 | 2 | 1 | 1 |
| M1 | 1 | 0 | 4 |
| M2 | 4 | 1 | 4 |
| Correct, restored | 0 | 0 | 0 |

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

Each expected failure was an assertion in its named case. No run timed out.
With M2, `isr_wakes_a_sleeping_task` joined its child after the child's bounded
sleep expired, reached its notification-result assertion, and completed the
suite. The last case of each suite ran on both clocks for every change.
The existing tests detected all three changes without test additions.

## Repeating the experiment

Use a clean, disposable checkout with the tools described in the
[README](../../../README.md). Configure the `host` and `debug` presets. Run
the following commands from its root for the correct implementation, then
after each isolated change:

```shell
cmake --preset host
cmake --preset debug
cmake --build --preset host
ctest --preset host -V
cmake --build --preset debug
ctest --test-dir ../build/debug -R "^qemu-task-timer" -V --output-on-failure --no-tests=error
ctest --test-dir ../build/debug -R "^qemu-wakeup-context" -V --output-on-failure --no-tests=error
```

Keep `QEMU_ICOUNT_SHIFT=5`, the default, so CTest registers both clocks. Verify
the named assertion failures and the suite summaries. A build error, zero
tests, an unrelated failure or a timeout does not establish detection.
Restore the function before applying the next change, then rebuild and rerun
the correct version. Check that the source diff is empty after restoration.

## Environment and limits

The experiment used Windows x64, clang 21.1.8, CMake 4.3.1-msvc1, Unity 2.6.1,
and QEMU 9.2.50 (development build). The host build enabled AddressSanitizer
and UBSan. The RV32 build used Debug, `rv32imac` and `ilp32`. QEMU used the
`virt` machine with instruction-count shift 5 and a separate host-clock run.

The main working copy also passed the RV32 Debug build and all 82 CTests,
including 33 host-clock tests, with the correct implementation. These
experiments cover the three specified context-policy faults. Real hardware,
the full optimization/LTO matrix, and other possible faults were not tested
in this experiment.
