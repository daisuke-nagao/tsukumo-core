# Interrupt-disabled time of a mailbox send into a long queue

This note sets the instruction budget for the one mailbox send whose cost
grows with the number of queued messages, says how many queued messages the
budget holds for, and records the counts. Read it before you change
`tk_snd_mbx()` or `insert_msg_pri()` in [mailbox.c](../../../kernel/mailbox.c),
or when you need to know how long a mailbox call keeps interrupts disabled.

## Summary

- The budget is 31250 instructions for one send, the same budget as the timer
  handler's in [timer-delay-budget.md](timer-delay-budget.md): a tenth of a
  10 ms kernel tick when QEMU runs with `-icount shift=5`, under which each
  instruction counts as 32 ns of guest time.
- The worst send is a message to a `TA_MPRI` mailbox (one that queues messages
  in priority order) that goes just before the last of 256 queued messages. It
  took 7167 instructions (23% of the budget) in the `debug` preset and 1088
  (3.5%) in `minsizerel`. (`debug` and `minsizerel` are CMake presets in
  `CMakePresets.json` with the Debug and MinSizeRel build types.)
- The budget holds for 256 messages in one mailbox in both presets under QEMU.
  That is the number the kernel promises. By calculation, the worst send stays
  inside the budget up to about 1147 queued messages in `debug` and 7796 in
  `minsizerel`.
- The kernel does not limit the number of queued messages. Past 256, only the
  promise about the time ends.
- The other calls that the suite measures (the send behind the last message,
  the send ahead of every message, and `tk_del_mbx()`) take a time that does
  not grow with the queue. The suite allows each of them at most 100
  instructions more with 256 queued messages than with none, and the two sends
  took at most 41 more.
- These are QEMU instruction counts. Hardware latency requires
  target-specific measurement.

## The send that scans the queue

`tk_snd_mbx()` runs with interrupts disabled from after the check of its
arguments to its return. For a `TA_MPRI` mailbox it queues the message behind
the messages of the same or a higher priority and ahead of the messages of a
lower one (a lower `msgpri` value is a higher priority). Most sends take a
constant number of steps. One does not: a message that goes between two queued
messages makes `insert_msg_pri()` visit the messages ahead of it one by one, so
the time that interrupts stay off grows with the length of the queue.

The other mailbox calls take a time that does not grow with the queue, and
`qemu-mailbox-cost` checks that. Each of them takes at most 100 instructions
more with 256 queued messages than with none. They are the send behind the
last message, the send ahead of every message, and `tk_del_mbx()`. The two
sends are also checked with 1 queued message.

## The budget

The send, by itself, may take at most a tenth of a kernel tick: 31250
instructions. It is the budget of the timer handler, so the kernel has one
budget for the time that interrupts stay off. As there, the budget is that
count at any `-icount` shift.
[How instructions are counted](timer-delay-budget.md#how-instructions-are-counted)
explains the tick, the shift and the `minstret` counter.

The count is the instructions between two reads of `minstret`, the RISC-V
counter of retired instructions, around the call.
The call runs in the context of a task, so it has no trap entry to leave out.

The kernel accepts queues longer than 256 messages, and the budget is promised
for 256 only. The μT-Kernel 3.0 Specification (December 2019, section 4.4.3,
"Additional Notes", in the
[synchronization and communication chapter](https://tron-forum.github.io/mtk3_spec_jp/synchronzation_and_communication_functions.html))
says that, because the application allocates the message headers, the number
of messages that can be queued has no limit, and `tk_snd_mbx()` has no error
for a full queue. So the kernel refuses nothing, and a program that queues
more still works. Only the promise about the time ends at 256, and the
[results](#results) say how far past it the budget still holds.

## What is measured

Each case starts on a tick edge, so that no tick handler runs inside the
measurement. It queues messages into a `TA_TFIFO | TA_MPRI` mailbox (waiting
tasks in FIFO order, messages in priority order) and reads `minstret` around
one `tk_snd_mbx()`. The queued messages are of priority 5, then of priority 7.
The message that is sent has the priority that puts it where the case means.

| Where the message goes | Queue | Priority of the message | Messages the scan visits |
| --- | --- | --- | --- |
| ahead of every message | 256 of priority 5 | 4 | 1 |
| into the middle | 128 of priority 5, then 128 of priority 7 | 6 | 129 |
| before the last message (the worst) | 255 of priority 5, then 1 of priority 7 | 6 | 256 |
| behind the last message | 256 of priority 5 | 5 or 6 | 0 |

The last column counts the message the scan stops at. A send behind the last
message returns before the scan starts.

The sends ahead of every message and behind the last one are measured with 1
and with 256 queued messages. The worst case is also measured with 1 queued
message, so that the two counts give the cost of one more message. A send into
the middle needs a message on each side of it, so with 1 queued message it is
one of the other positions; it is measured with 256 only.

- The sends ahead of every message and behind the last one take a constant
  number of steps: a comparison with the last message, and for the send ahead,
  one visit. They must stay within 100 instructions of the same send into an
  empty queue.
- The sends into the middle and before the last message must take at most
  31250 instructions.

After a send, the case receives every message of the mailbox and checks that
the message that was sent came out at the place the table gives. Without that
check, a case that measured another path than the one it names could pass.

## Results

Instructions of one send, under QEMU `-icount shift=5`. The budget is 31250.
The share of the budget is in brackets.

| Case | `debug` | `minsizerel` |
| --- | --- | --- |
| before the last message, 1 queued | 282 (0.9%) | 68 (0.2%) |
| into the middle, 256 queued | 3738 (12%) | 580 (1.9%) |
| before the last message, 256 queued | 7167 (23%) | 1088 (3.5%) |

The sends that take a constant number of steps cost at most 41 instructions
more than the same send into an empty mailbox in `debug` (the send ahead of
every message; 16 for the send behind the last) and at most 8 in `minsizerel`
(3), with 1 or with 256 queued messages. The limit is 100.

The send that scans takes 27 instructions per visited message in `debug` and
4 in `minsizerel`. The three counts of each preset lie on one line, so the
cost of a send that visits `n` messages is 255 + 27 `n` instructions in
`debug` and 64 + 4 `n` in `minsizerel`. The middle send visits 129 messages,
and the formula gives 3738 and 580, the counts above.

By calculation, not by measurement, the worst send stays inside the budget
for a queue of up to:

| | `debug` | `minsizerel` |
| --- | --- | --- |
| messages | about 1147 | about 7796 |

In `debug`, that is 4.5 times the 256 that the kernel promises.

### Why insert_msg_pri() still scans

The worst send takes 23% of the budget with 256 messages, in the preset that
costs the most, so `insert_msg_pri()` stays as it is. The alternatives that
would make the send constant stay rejected for the reasons in its comment: one
list per priority needs a bound on `msgpri`, which the specification does not
give, and enabling interrupts during the scan lets the queue change under the
cursor.

If a later change to the scan, or a build that costs more per message, takes
the worst send over the budget, the promised number of messages comes down to
what the budget allows, and this note and the suite change with it.

## How to reproduce the counts

1. From the root of the repository, build with the `debug` or the
   `minsizerel` preset:

   ```shell
   cmake --preset debug
   cmake --build --preset debug
   ```

2. Run the suite and read the lines that start with `mailbox-cost:`. CTest
   prints the log of a passing test only with `-V`:

   ```shell
   ctest --test-dir ../build/debug -R qemu-mailbox-cost -V
   ```

   For `minsizerel`, use that preset name and `../build/minsizerel`.

The suite is registered only when CMake finds QEMU at configure time and
`QEMU_ICOUNT_SHIFT` is a number of 8 or less. [run_qemu.md](run_qemu.md)
describes the QEMU suites.

## Limits of the measurement

- It counts instructions, not time, and 312500 instructions per tick is a
  property of the `-icount` setting, so a board will differ (see
  [How instructions are counted](timer-delay-budget.md#how-instructions-are-counted)).
- Only the shift 5 counts were measured. The count of a send should not depend
  on the shift, for the same reason as the timer handler's, but no other shift
  was run. The suite is registered for shifts 0 to 8.
- The queues hold two priorities. The scan compares one field per message and
  stops at the first message with a lower priority, so a queue of many
  priorities should cost the same per visited message. It was not run.
- A send that makes a waiting task ready does not queue the message, and its
  cost does not depend on the queue. The queue of a mailbox with waiting tasks
  has no messages. The wait queue of a `TA_TPRI` mailbox (waiting tasks in
  priority order) is scanned when a task waits (`tk_rcv_mbx()`), and holds at
  most the tasks the kernel allows (`CFN_MAX_TSKID`), so that scan is bounded.
  It was not measured.
- These counts measure a send from a task and leave out interrupt entry. The
  measurement of the stack that handlers use is in
  [interrupts.md](../../kernel/interrupts.md#measured-stack-use).
- The suite runs with one mailbox at a time. The scan touches only the queue
  of the mailbox that is sent to, so other mailboxes do not add to it.

## Measurement record

The counts were rechecked on 2026-11-09 (JST) with QEMU `-icount shift=5` in
`debug` and `minsizerel`, after `EI()` was changed to restore the interrupt
state that `DI()` saved (see
[What EI restores](../../kernel/interrupts.md#what-ei-restores)). The 31250
budget held in both presets after that change. The counts do not depend on
host timing, and before the change two runs of each preset gave the same
counts.
