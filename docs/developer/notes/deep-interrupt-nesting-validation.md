# Deep interrupt nesting validation

The [interrupt guide](../../kernel/interrupts.md#stack-size-and-nesting) sizes
the 2048-byte interrupt stack for nesting as deep as the PLIC has priorities.
This record shows how external interrupts were nested three to seven levels
deep on QEMU `virt`, what each level was checked for, and how much of the
stack the tested handlers used.

## Scope

The tests cover a single hart on QEMU `virt`, PLIC context 0, priorities 1
to 7, the 32-byte external-entry record and the 2048-byte interrupt stack.

Level k of a chain is served at priority k, so seven levels is the deepest
chain whose priorities rise by one per level. A chain of exactly `NEST_DEPTH`
levels was built for each depth from 3 to 7.

The results apply to the handlers in `test/suites/interrupt_depth.c`. They do
not guarantee the stack use of an arbitrary handler, or of any handler after
a compiler or option change. These are outside this record:

- real hardware;
- more than one hart or PLIC context;
- handlers that lower the threshold through MMIO;
- the nested-ISR behavior of every service call;
- stack use outside the measured interrupted-task window.

## Reproducing

Build a preset and run the depth suites with CTest:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --test-dir ../build/debug -R interrupt-depth --output-on-failure
```

Each depth registers three tests:

- `qemu-interrupt-depth-N` runs on the instruction-count clock;
- `qemu-interrupt-depth-N-hostclock` runs on the host clock;
- `qemu-interrupt-depth-N-trace` checks the PLIC completion order.

To see the stack figures, run one ELF directly; [run_qemu.md](run_qemu.md)
describes the QEMU command line. Each case prints one line before its result:

```text
STACK used=<hex> task_frame_bytes=<hex> task_painted_used=<hex> task_frame_used=<hex> task_bound_used=<hex> task_bound_left=<hex> task_mie=<hex>,... level_sp=<hex>,... ctx_sp=<hex>,...
```

- `used` is the high-water mark of the interrupt stack.
- `level_sp` gives, for each level, the distance from the top of the interrupt
  stack to the level's stack pointer.
- `ctx_sp` gives the distance from the interrupt-stack top to each context
  record. It supplies the base for the reservation calculation below.
- `task_frame_bytes` is the distance from the task SP sampled during paint
  preparation to the outermost trap frame.
- `task_painted_used` is a conservative task-stack bound from the painted
  prefix. `task_frame_used` includes the complete outer-frame reservation.
  `task_bound_used` takes the larger value and `task_bound_left` is its
  remaining space in the 2048-byte case-task stack.
- `task_mie` records the interrupt-source mask at chain entry, before and
  after paint preparation, and after the chain.

## How the chain is raised

QEMU `virt` has two interrupt sources that software can raise: the UART's
transmitter-empty interrupt (source 10) and the goldfish RTC alarm (source 11).
The chain alternates them, with source 10 on odd levels and source 11 on even
levels.

Each level runs these steps:

1. It quiets its own device, so that no source is raised again while the
   chain unwinds.
2. From level 2 on, it completes with `EndOfInt` the claim that the level
   above it holds on the source it is about to reuse.
3. It raises that source's priority to k+1 with `EnableInt`.
4. It turns interrupts on with `EI(0)`.
5. It raises the device from an assembly wait. The wait fills every free
   register with a pattern and checks the patterns after the child returns.

The deepest level starts nothing.

Handler kind is fixed per source, because `tk_def_int` cannot be called from
an ISR. Each depth runs nine cases:

| Case | Odd levels | Even levels | Outermost exit |
| --- | --- | --- | --- |
| `c_handlers_nest_to_the_full_depth` | C | C | C return |
| `asm_handlers_with_tk_ret_int_outside` | ASM | ASM | `tk_ret_int`; even levels return directly |
| `asm_handlers_with_direct_return_outside` | ASM | ASM | direct return; even levels use `tk_ret_int` |
| `c_odd_and_asm_even_levels_with_tk_ret_int` | C | ASM (`tk_ret_int`) | C return |
| `c_odd_and_asm_even_levels_with_direct_return` | C | ASM (direct) | C return |
| `asm_odd_and_c_even_levels_with_tk_ret_int` | ASM (`tk_ret_int`) | C | `tk_ret_int` |
| `asm_odd_and_c_even_levels_with_direct_return` | ASM (direct) | C | direct return |
| `a_same_priority_request_waits_for_the_deepest_level` | C | C | C return |
| `a_lower_priority_request_waits_for_the_whole_chain` | C | C | C return |

The last two cases test requests that must wait:

1. The deepest level marks the next entry as a late request.
2. It completes the claim of the level above it, which opens that source's
   gateway.
3. It raises the source at priority `NEST_DEPTH` or 1 and checks with
   `CheckInt` that the request is pending.
4. While it waits with interrupts on, the request must not get in.

The same-priority request is taken at depth `NEST_DEPTH` inside the level
above, after the deepest level returns. The lower-priority request is taken
after the outermost exit.

## What each case checks

Handlers record into per-level arrays and print nothing. The case checks the
records after it has put the devices, sources and handlers back.

- **Order.** Level k enters k-th and leaves (2·N+1−k)-th of the chain's 2·N
  entries and exits.
- **Kernel state at entry:**
  - the depth;
  - the source and depth in the active context record;
  - the record's link to the level above;
  - the record's place on the interrupt stack and its 16-byte alignment;
  - the threshold, which must equal k;
  - MIE off, with the timer and software bits of `mie` off;
  - the stack pointer, inside the interrupt stack and 16-byte aligned.

  The outermost trap frame must lie just below the task's stack pointer.
- **After the child returns:**
  - the threshold is k again;
  - interrupts are still on;
  - the full `mie` mask matches the value at entry;
  - the stack pointer is unchanged;
  - no patterned register changed.

  A trap that returned to the wrong place would fail the register check or
  the per-level exit order.
- **After the chain:**
  - the threshold is 0, the depth is 0 and no context record is active;
  - `mie` is as it was before;
  - the interrupted task kept its registers.
- **Dispatch.** Every case checks deferred dispatch; there is no separate
  case for it. Two tasks that outrank the case wait on a semaphore, one count
  each. The middle level's signal makes the first task ready, and the deepest
  level's signal makes the second ready. Both requests must wait for the
  outermost exit, and each task must run once, after that exit:
  - after a C or `tk_ret_int` outer exit, from the external trap, with exactly
    one task switch in that trap;
  - after a direct outer return, from the software trap that follows, with
    none in the external trap.
- **Completion.** Each depth's trace test records the PLIC claim/complete
  writes. `test/check_interrupt_completion.py` compares them per case, in
  order, against the expected list:
  - a plain chain completes the claims of levels 1 to N−2 from `EndOfInt`,
    then the deepest claim, then the claim of level N−1;
  - the late-request cases add one completion.

  A write outside a case, an extra write and a missing write each fail the
  check.

Only a level that calls `EndOfInt` while two records of the same source are
active can show which record it completes. `EndOfInt` searches from the
newest record, so it finds the open claim before the older record, which is
already complete. A wrong choice of the older record leaves the open claim
pending, so the next request on that source never arrives.

In a plain chain, level 4 is the first level that calls `EndOfInt` with two
such records active, so depth 5 is the shallowest plain chain that exposes
the choice. In the late-request cases, the deepest level makes that call
itself from depth 4 on. Depth 3 cannot expose it.

## Stack measurement

Before each case, the whole interrupt stack and the 1024-byte boot stack below
it are painted. The boot stack is unused after the first task starts; only the
fatal exception entry takes it again.

A case fails if any of these holds:

- a level's stack pointer falls outside the interrupt stack;
- any boot-stack word changed;
- the painted high-water mark reaches 2048 bytes.

The painted scan records writes. Boot-stack canaries detect writes below
the ISR region, while the per-phase SP checks validate entry, child arming
and child return. A frame can reserve space without writing its lowest
slots, so a separate reservation calculation covers that space.

The high-water mark includes any timer trap taken between the paint and the
chain. The fixture records task-stack usage across the interrupted wait,
trap return and deferred dispatch. Only its unused prefix is painted, with
MIE disabled, by an assembly leaf that allocates no frame and leaves the
caller's live frames intact. The bottom word is a canary and a 256-byte
unpainted gap is retained below the preparation SP.

The case-task bounds come from the runner's 512-word allocation contract.
`initial_sp` points at the initial saved-register frame, 128 bytes below the
allocation top. The measurement adds that frame before computing the bottom.
This distinction matters because painting 128 bytes below the allocation
can overwrite the adjacent runner stack. Both task views are initialized,
and a failed preparation skips the IRQ and reaches common cleanup with
defined diagnostic values.

When the painted prefix remains untouched, its scan stops at the paint
ceiling. The resulting count includes the unpainted gap and is an upper
bound that includes that gap. The outer trap frame supplies
an independent reservation bound. All task metrics are collected before
reporting and assertions, after the interrupted task resumes from dispatch.

### Reserved stack space

The final ELF disassembly is checked separately for all five depths in all
six cross presets. Starting at each recorded `ctx_sp`, add the largest fixed
stack-frame chain among handler execution, prepare, finish and `tk_ret_int`.
The outermost schedule wrapper is included too. The recorded context bases
already include suspended handlers, their register waits and nested trap
frames. The ASM entry and direct-return macro add no further frame.

Frame sizes come from each reachable function's stack adjustments. Branches
and tail calls are conservatively combined. Unresolved calls, indirect
transfers, dynamic or multiple unmodeled reservations and unrecognized SP
writes require inspection. The final 30 ELFs pass these checks. Fatal invalid
context paths and restoring SP to an existing saved frame are outside the
normal return budget.

The call from `tkmc_sem_serve_waiters` completes a waiter with `E_OK`.
`tkmc_complete_wait` re-evaluates semaphore waiters only for `E_TMOUT` or
`E_RLWAI`, so that reverse call edge is excluded for this successful signal
path. Both woken tasks must observe `E_OK`. Timer and software interrupts
stay masked during external nesting. These conditions limit the calculation
to the exercised call paths.

For example, Debug's deepest context begins 1760 bytes below the top. Its
largest call chain adds 224 bytes, giving a reservation bound of 1984 and
64 bytes left. That chain consists of the 64-byte handler, `wake_the_task`
(16), `tk_sig_sem` (32), semaphore serving (32), wait completion (32), list
append (16) and list insertion (32). The written high-water is 1972 bytes.

To inspect the same frames, build the corresponding preset and use its ELF:

```sh
llvm-objdump -d --no-show-raw-insn ../build/debug/tsukumo-core-test-interrupt-depth-7.elf
```

Trace the fixed frames below each context base through `level_handler` or
`depth_asm_callback`, `tkmc_prepare_external_interrupt`,
`tkmc_finish_external_interrupt`, `tkmc_ret_int_from_sp` and `__wrap_schedule`.
Check the source condition on each excluded error or timeout edge. Repeat
after compiler, build-option or handler changes.

## Environment and results

The runs were recorded on 2026-11-18 with Clang 21.1.8 and a QEMU 9.2.50
development build (`rv32imac`, `ilp32`).

All 121 CTests passed in each of `debug`, `debug-lto`, `relwithdebinfo`,
`relwithdebinfo-lto`, `minsizerel` and `minsizerel-lto`:

- the earlier 106 tests;
- the 15 new ones: five depth suites on the instruction-count clock and on
  the host clock, and five trace tests.

The host registration passed 180 Unity cases with ASan and UBSan and three
public-declaration consumers. The initial main baseline passed those same
host checks and all 106 Debug CTests.

The table gives the interrupt-stack high-water mark in bytes on the
instruction-count clock. A single value means all nine cases matched. A range
spans the per-case values that each case's `STACK` line printed.

| Preset | Depth 3 | Depth 4 | Depth 5 | Depth 6 | Depth 7 | Least left at depth 7 |
| --- | --- | --- | --- | --- | --- | --- |
| `debug`, `debug-lto` | 820 | 1108 | 1396 | 1684 | 1972 | 76 |
| `relwithdebinfo` | 596–600 | 852–856 | 1108–1112 | 1364–1368 | 1620–1624 | 424 |
| `relwithdebinfo-lto` | 572–576 | 828–832 | 1084–1088 | 1340–1344 | 1596–1600 | 448 |
| `minsizerel` | 608–656 | 864–928 | 1120–1200 | 1376–1472 | 1632–1744 | 304 |
| `minsizerel-lto` | 608–616 | 864–872 | 1120–1128 | 1376–1384 | 1632–1640 | 408 |

At depth 7, the cases that set each preset's largest count are:

- `debug`: all nine cases at 1972 bytes;
- `minsizerel`: the two all-ASM cases at 1744 bytes. The ASM-odd and C-odd
  mixed cases used 1696 and 1680 bytes, the late-request cases 1640, and the
  C-only case 1632;
- `relwithdebinfo`: the two late-request cases at 1624 bytes;
- `relwithdebinfo-lto`: the cases with ASM on odd levels at 1600 bytes;
- `minsizerel-lto`: the two late-request cases at 1640 bytes.

Each level adds 288 bytes in `debug` and about 256 bytes in the optimized
presets:

- the 128-byte trap frame;
- the 32-byte entry record;
- one handler frame;
- the 64-byte frame of the register-checking wait.

The reserved-stack bounds across all nine cases are below. They include
unwritten frame slots and use the conditions in the reservation method.

| Preset | Depth 3 | Depth 4 | Depth 5 | Depth 6 | Depth 7 | Least reserved space left at depth 7 |
| --- | --- | --- | --- | --- | --- | --- |
| `debug`, `debug-lto` | 832 | 1120 | 1408 | 1696 | 1984 | 64 |
| `relwithdebinfo`, `relwithdebinfo-lto` | 640 | 896 | 1152 | 1408 | 1664 | 384 |
| `minsizerel` | 688 | 960 | 1232 | 1504 | 1776 | 272 |
| `minsizerel-lto` | 640 | 896 | 1152 | 1408 | 1664 | 384 |

The task-stack bounds include all depths, nine cases and both clocks.

| Preset | Painted bound | Outermost frame bound | Least space left |
| --- | --- | --- | --- |
| `debug` | 752–768 | 656–672 | 1280 |
| `debug-lto` | 672 | 576 | 1376 |
| `relwithdebinfo` | 592 | 528 | 1456 |
| `relwithdebinfo-lto` | 576 | 512 | 1472 |
| `minsizerel`, `minsizerel-lto` | 576–608 | 512–544 | 1440 |

`task_frame_bytes` is 160 bytes in Debug and Debug-LTO and 192 bytes in the
optimized presets. It is measured from the paint-preparation SP. The total
task bounds above include the caller's already-live frames as well.

The fixture keeps one C handler frame on each level, including ASM callbacks.
The out-of-line boundary checker avoids retaining additional observation
locals on every suspended level. The measured margin belongs to this
fixture. Additional handler frames require a new reservation calculation.

## Fault detection

Each fault was introduced in a separate isolated source and build tree. Each one failed the cases
named below through the check described, not through a build failure or a
runner timeout. All cases passed again after each fault was reverted.

| Fault | Where | What detected it |
| --- | --- | --- |
| `EndOfInt` completes the oldest matching record | `kernel/intctl.c` | Depth 5: all nine cases fail because level 5 never arrives (bounded wait). Depth 3 does not exercise the repeated-record choice. |
| C and `tk_ret_int` exits restore threshold 0 | `kernel/intctl.c` | Depth 3: all nine cases fail on the threshold after the child returned. |
| Direct ASM return restores threshold 0 | `include/tk/asm/rv32/interrupt.h` | Depth 3: exactly the four cases that have a direct-return child fail on the threshold after the child returned. |
| A non-inlined deepest-level helper writes a 512-byte local array | test fixture | Depth 7 `debug`: all nine cases fail on the boot-stack check. They also print `used=00000800` (2048 bytes), so the limit is reached too. The boot-stack assertion runs first. |
| A boot-stack word is overwritten | test fixture | Depth 3: all nine cases fail on the boot-stack check. |
| Task-stack preparation fails before assigning metrics | test fixture | Depth 3: all nine cases report the preparation assertion, print defined metrics, recover the children and reach a result. |

The first fault also exercises recovery. Level 5 never arrives, so level 4's
bounded wait ends. The level then quiets and disables the next source and
unwinds. The case collects its woken tasks and fails. Once the chain has unwound,
cleanup quiets both devices, disables both sources and calls ClearInt for
each latched request before unregistering the handlers. All nine faulted
cases then enter through the intended register wait, report nonzero ISR
high-water marks and reach a result. The Debug task-frame distance is 160
bytes in every case. These observations also catch an IRQ delivered during
the next case's setup, before its paint and arming store.

## Limits

The two QEMU sources allow only an alternating chain. A chain with three or
more distinct active sources, or one that skips priorities, is not covered.

The figures depend on the fixture handlers and the compiler. A different
handler, compiler version or option needs a new measurement. The `debug`
margin at seven levels is small. Handler or compiler changes require a new
measurement of writes and reserved space before relying on that depth.
