# Interrupt-disabled time of a mailbox send into a long queue

This note records the instruction budget for the one mailbox send whose cost
grows with the number of queued messages, the existing tested 256-message
threshold, and per-build measurement results. Read it before you change
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
- The checked-in regression still tests and promises the budget for 256
  messages. An isolated sweep measured the adjacent budget boundary in all six
  CMake presets, but those per-build observations do not extend that promise.
- Earlier linear estimates are compared with direct adjacent-point
  measurements in [Results](#results).
- The kernel does not limit the number of queued messages. Past 256, only the
  existing time promise ends; the measured boundaries are not a cap or a new
  guarantee.
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

The measured task-context interval is the `minstret` difference around the
call. It includes call-boundary and counter-measurement overhead, not just the
DI-to-EI section. There is no trap entry in the interval.

The kernel accepts queues longer than 256 messages, and the budget is promised
for 256 only. The μT-Kernel 3.0 Specification (December 2019, section 4.4.3,
"Additional Notes", in the
[synchronization and communication chapter](https://tron-forum.github.io/mtk3_spec_jp/synchronzation_and_communication_functions.html))
says that, because the application allocates the message headers, the number
of messages that can be queued has no limit, and `tk_snd_mbx()` has no error
for a full queue. So the kernel refuses nothing, and a program that queues
more still works. The tested time promise ends at 256; the [results](#results)
record measurements beyond it without extending that promise.

## What is measured

Each case starts on a tick edge, so that no tick handler runs inside the
measurement. It queues messages into a `TA_TFIFO | TA_MPRI` mailbox (waiting
tasks in FIFO order, messages in priority order) and reads `minstret` around
one `tk_snd_mbx()`. The queued messages are of priority 5, then of priority 7.
The message that is sent has the priority that puts it where the case means.
The checked-in fixture tests up to 256 queued messages. The six-preset boundary
sweep below used a temporary extension of that fixture in a disposable source
copy; it is measurement data, not a new regression threshold.

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

The checked-in `qemu-mailbox-cost` checks passed in all six presets under QEMU
`-icount shift=5` before the temporary measurement case was added. The
extended suite also passed in all six presets. The checked-in 256-message
budget checks remain the guarantee; the values below describe these measured
binaries and fixture only.

The isolated sweep measured adjacent points `N` within the 31250 instruction
budget and `N+1` above it. Each endpoint was measured five times with a fresh
mailbox; all five repeats produced the listed count.
All counts in the table are instructions.
The stock column is the checked-in fixture's count at 1 and 256 queued
messages. The wrapper delta is the extra count in the temporary measurement
fixture versus stock, measured only at 1 and 256. Append counts are for the
priority-8 control at the measured boundary; they were also flat at queue
lengths 1, 256, and 512.

| Preset | Stock count at 1 / 256 | Wrapper delta at 1 / 256 | `N` (instructions) | `N+1` (instructions) | Append at `N` / `N+1` |
| --- | ---: | ---: | ---: | ---: | ---: |
| `debug` | 282 / 7167 | 0 / 0 | 1147 (31224) | 1148 (31251) | 257 / 257 |
| `relwithdebinfo` | 61 / 1081 | 1 / 1 | 7798 (31250) | 7799 (31254) | 57 / 57 |
| `minsizerel` | 68 / 1088 | 2 / 2 | 7796 (31250) | 7797 (31254) | 65 / 65 |
| `debug-lto` | 274 / 7159 | 0 / 0 | 1148 (31243) | 1149 (31270) | 249 / 249 |
| `relwithdebinfo-lto` | 50 / 1070 | 1 / 1 | 7800 (31247) | 7801 (31251) | 46 / 46 |
| `minsizerel-lto` | 68 / 1088 | 2 / 2 | 7796 (31250) | 7797 (31254) | 65 / 65 |

The earlier `debug` and `minsizerel` formulas, `255 + 27n` and `64 + 4n`,
were fits to the stock fixture's 1- and 256-message sends plus its middle
insertion at 256 (129 messages scanned, 3738 and 580 instructions). They
estimated boundaries of about 1147 and 7796 messages. The table now directly
measures the adjacent within-budget `N` and over-budget `N+1` for those two
builds. The formula values remain estimates for other builds and targets.

### Interpretation

The stock 256-message case passed in all six presets; its largest measured
send was 7167 of the 31250-instruction budget. These results give no evidence
that the existing 256-message envelope needs a redesign. A per-priority list
would need a bound on `msgpri` that the specification does not supply, while
enabling interrupts during the scan could invalidate its cursor. For longer
workloads, establish the queue depth and measure latency on the target. These
QEMU results add no queue cap or timing guarantee beyond 256.

## How to reproduce the counts

Use a disposable copy of the repository. The measurement temporarily extends
the existing `test/suites/mailbox_cost.c` fixture. From the repository root in
PowerShell, create a fresh copy of tracked files:

```powershell
$stem = Join-Path $env:TEMP ("mailbox-budget-" + [guid]::NewGuid().ToString("N"))
$archive = "$stem.zip"
$copy = "$stem-source"
git archive --format=zip "--output=$archive" HEAD
if ($LASTEXITCODE -ne 0) { throw "archive failed" }
New-Item -ItemType Directory -Path $copy | Out-Null
Expand-Archive -LiteralPath $archive -DestinationPath $copy
Set-Location $copy
```

From inside that disposable copy, run the same build-and-test block once with
the stock fixture, then again after adding the temporary measurement case:

```powershell
$qemu = (Get-Command qemu-system-riscv32 -ErrorAction Stop).Source
$presets = @("debug", "relwithdebinfo", "minsizerel", "debug-lto", "relwithdebinfo-lto", "minsizerel-lto")
foreach ($preset in $presets) {
  $build = Join-Path $copy "build-$preset"
  cmake --preset $preset -B $build "-DQEMU_EXECUTABLE=$qemu" -DQEMU_ICOUNT_SHIFT=5
  if ($LASTEXITCODE -ne 0) { throw "configure failed: $preset" }
  cmake --build $build --target tsukumo-core-test-mailbox-cost.elf --parallel 4
  if ($LASTEXITCODE -ne 0) { throw "build failed: $preset" }
  ctest --test-dir $build -V --no-tests=error -R '^qemu-mailbox-cost$'
  if ($LASTEXITCODE -ne 0) { throw "stock test failed: $preset" }
}
```

In the disposable copy, add a temporary measurement case to
`test/suites/mailbox_cost.c` with a dedicated 10001-entry message array and a
maximum `N` of 10000, following the existing mailbox setup, send,
counter-read, drain, and order-check patterns. For each `N`, queue `N-1`
messages at priority 5 followed by one at priority 7; after `tk_dly_tsk(1)`
starts the sample at a tick edge, read `minstret`, send a priority-6 probe,
then read `minstret` again. Assert `E_OK`, drain exactly `N+1` messages, check
that the probe is at index `N-1`, and delete the mailbox. Keep setup and drain
outside the measured interval. Assert each measured interval is below 312500
instructions (ten times the 31250 budget, or one full 10 ms tick at shift 5)
so a timer handler cannot enter the sample.

Sweep queue lengths 1, 256, 512, 768, 1024, 1536, 2048, 4096, 8192, and
10000; assert the near-tail count increases and use the first over-budget point
to bracket a binary search for adjacent in-budget `N` and over-budget `N+1`.
Measure each endpoint five times, each with a fresh mailbox. Also send a
priority-8 append control into queues of length 1, 256, 512, `N`, and `N+1`,
checking that it is received at index `N`.

The tested environment was Clang 21.1.8 targeting `riscv32-unknown-none-elf`
(`rv32imac`, `ilp32`), Visual Studio CMake 4.3.1-msvc1, Ninja 1.13.2, and
QEMU 9.2.50. QEMU used
`virt`, no BIOS, no graphics, and `-icount shift=5,sleep=off`. The regular
CMake configuration fetches the project's pinned Unity 2.6.1 dependency when
it is not already available.

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
- The full interval measures a send from a task and leaves out interrupt
  entry; it is not the exact DI-to-EI section. The measurement of the stack
  that handlers use is in
  [interrupts.md](../../kernel/interrupts.md#measured-stack-use).
- The suite runs with one mailbox at a time. The scan touches only the queue
  of the mailbox that is sent to, so other mailboxes do not add to it.

## Measurement record

The original two-preset counts were rechecked on 2026-11-09 (JST) with QEMU
`-icount shift=5` in `debug` and `minsizerel`, after `EI()` was changed to
restore the interrupt state that `DI()` saved (see
[What EI restores](../../kernel/interrupts.md#what-ei-restores)). The isolated
six-preset sweep was run on 2026-11-21 (JST). These QEMU instruction counts do
not depend on host timing in this setup, but compiler, build options, QEMU
behavior, and target hardware can change the result.
