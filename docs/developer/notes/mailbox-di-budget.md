# Interrupt-disabled time of a mailbox send into a long queue

Measured on 2026-11-06 (JST).

## Purpose

`tk_snd_mbx()` in [mailbox.c](../../../kernel/mailbox.c) runs with interrupts
disabled from after the check of its arguments to its return. For a mailbox with
`TA_MPRI` it queues the message behind the messages of the same or a higher
priority and ahead of the messages of a lower one. Most sends take a constant
number of steps. One does not: a message that goes between two queued messages
makes `insert_msg_pri()` visit the messages ahead of it one by one, so the time
that interrupts stay off grows with the length of the queue. This note sets a
budget for that time, says how many queued messages it holds for, and records
the counts.

The other mailbox calls take a time that does not grow with the queue, and
`qemu-mailbox-cost` checks that. Each call takes at most 100 instructions more
with 256 queued messages than with none. They are the send behind the last
message, the send ahead of every message, and `tk_del_mbx()`. The two sends are
also checked with 1 queued message.

## The budget

The send, by itself, may take at most a tenth of a kernel tick. A tick is
10 ms and one instruction takes 2^shift ns under `-icount shift=<shift>`, so a
tick holds 312500 instructions at the default shift 5, and the budget is 31250
instructions. It is the budget of the timer handler in
[timer-delay-budget.md](timer-delay-budget.md), so the kernel has one budget for
the time that interrupts stay off. As there, the budget is that count, whatever
the shift.

The count is the instructions between two reads of `minstret` around the call.
The call runs in the context of a task, so it has no trap entry to leave out.

In the measured QEMU `debug` and `minsizerel` configurations, the budget holds
for 256 messages in one mailbox. 256 is the number the kernel promises it for.
These instruction counts are a measured reference, not a hardware-wide timing
guarantee. It is not a
limit. The μT-Kernel 3.0 Specification (December 2019, section 4.4.3, "Additional
Notes") says that, because the application allocates the message headers, the
number of messages that can be queued has no limit, and `tk_snd_mbx()` has no
error for a full queue. So the kernel refuses nothing, and a program that queues
more still works. Only the promise about the time ends at 256, and the table
below says how far past it the budget still holds.

## How to repeat it

1. Build with the `debug` or the `minsizerel` preset.
2. Run the suite and read the lines that start with `mailbox-cost:`:

   ```shell
   ctest --test-dir ../build/debug -R qemu-mailbox-cost -V
   ```

The suite is registered only when QEMU is found at configure time and
`QEMU_ICOUNT_SHIFT` is a number of 8 or less. The counts do not depend on host
timing. They were the same in two runs of each preset.

## What is measured

Each case starts on a tick edge, so that no tick handler runs inside the
measurement, queues messages into a `TA_TFIFO | TA_MPRI` mailbox, and reads
`minstret` around one `tk_snd_mbx()`. Queued messages are of priority 5, then of
priority 7. The message that is sent has the priority that puts it where the case
means.

| Where the message goes | Queue | Priority of the message | Messages the scan visits |
|---|---|---|---|
| ahead of every message | 256 of priority 5 | 4 | 1 |
| into the middle | 128 of priority 5, then 128 of priority 7 | 6 | 129 |
| before the last message (the worst) | 255 of priority 5, then 1 of priority 7 | 6 | 256 |
| behind the last message | 256 of priority 5 | 5 or 6 | 0 |

The last column counts the message the scan stops at. A send behind the last
message returns before the scan starts.

The sends ahead of every message and behind the last one are measured with 1 and
with 256 queued messages. The worst case is also measured with 1 queued message,
so that the two counts give the cost of one more message. A send into the middle
needs a message on each side of it, so with 1 queued message it is one of the
other positions; it is measured with 256 only.

- **Ahead of every message and behind the last one** take a constant number of
  steps: a comparison with the last message, and for the send ahead, one visit.
  They must stay within 100 instructions of the same send into an empty queue.
- **Into the middle and before the last message** must take at most 31250
  instructions.

After a send, the case receives every message of the mailbox and checks that the
message that was sent came out at the place the table gives. Without that check a
case that measured another path than the one it names could pass.

## Results

Instructions of one send, under QEMU `-icount shift=5`. The budget is 31250. The
share of the budget is in brackets.

| Case | `debug` | `minsizerel` |
|---|---|---|
| before the last message, 1 queued | 293 (0.9%) | 76 (0.2%) |
| into the middle, 256 queued | 3749 (12%) | 588 (1.9%) |
| before the last message, 256 queued | 7178 (23%) | 1096 (3.5%) |

The sends that take a constant number of steps cost at most 41 instructions more
than the same send into an empty mailbox in `debug` (the send ahead of every
message; 16 for the send behind the last) and at most 9 in `minsizerel` (3), with
1 or with 256 queued messages. The limit is 100.

The send that scans takes 27 instructions per visited message in `debug` and 4
in `minsizerel`. The three counts of each preset lie on one line, so the cost of
a send that visits `n` messages is 266 + 27 `n` instructions in `debug` and
72 + 4 `n` in `minsizerel`. The middle send visits 129 messages and the formula
gives 3749 and 588, the counts above.

By calculation, not by measurement, the worst send stays inside the budget for
a queue of up to:

| | `debug` | `minsizerel` |
|---|---|---|
| messages | about 1147 | about 7794 |

That is 4.5 times the 256 that the kernel promises in `debug`.

### Why the kernel did not change

The worst send takes 23% of the budget with the 256 messages, in the preset that
costs the most. So `insert_msg_pri()` stays as it is. The alternatives that
would make the send constant stay rejected for the reasons in its comment: one
list per priority needs a bound on `msgpri`, which the specification does not
give, and enabling interrupts during the scan lets the queue change under the
cursor.

If a later change to the scan, or a build that costs more per message, takes the
worst send over the budget, the promised number of messages comes down to what
the budget allows, and this note and the suite change with it.

## Limits of the measurement

- It counts instructions, not time. QEMU models no cache and no memory delay,
  and 312500 instructions per tick is a property of the `-icount` setting, so a
  board will differ.
- Only the shift 5 counts were measured. The count of a send should not depend
  on the shift, as the count of the timer handler did not, but no other shift
  was run. The suite is registered for shifts 0 to 8.
- The queues hold two priorities. The scan compares one field per message and
  stops at the first message with a lower priority, so a queue of many
  priorities should cost the same per visited message. It was not run.
- A send that makes a waiting task ready does not queue the message, and its
  cost does not depend on the queue. The queue of a mailbox with waiting tasks
  has no messages. The wait queue of a `TA_TPRI` mailbox is scanned when a task
  waits (`tk_rcv_mbx()`), and holds at most the tasks the kernel allows
  (`CFN_MAX_TSKID`), so that scan is bounded. It was not measured.
- The sends were made from a task. This kernel returns `E_CTX` from an interrupt
  handler for `tk_snd_mbx()` by policy, so no handler sends a message and the
  trap entry does not belong to this cost. A handler that wakes a task uses
  `tk_sig_sem()` or `tk_set_flg()`.
- The suite runs with one mailbox at a time. The scan touches only the queue of
  the mailbox that is sent to, so other mailboxes do not add to it.
