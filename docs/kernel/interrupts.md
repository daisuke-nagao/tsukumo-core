# Interrupts

This document explains how interrupts work in tsukumo-core: the `DI()` and
`EI()` macros, external interrupt handlers, the stack that handlers run on,
and nested interrupts. Read it before you write a device driver or an
interrupt handler, or before you change the kernel's trap code. The rules for
which kernel calls are valid in which calling context are in
[behavior.md](behavior.md).

The specification cited here is the official Japanese
[μT-Kernel 3.0 specification Ver.3.00.01](https://tron-forum.github.io/mtk3_spec_jp/index.html).

## Rules at a glance

- Declare a `UINT intsts` variable before you call `DI(intsts)`, and pass the
  same variable to `EI(intsts)`.
- Define a handler with `tk_def_int()` as a C function
  `void inthdr(UINT intno)` with the attribute `TA_HLNG`, then enable its
  source with `EnableInt(intno, level)`. Assembly entries use `TA_ASM` and
  the [assembly entry conventions](assembly-entry.md).
- In the handler, make the device stop asserting the interrupt. Use
  `EndOfInt(intno)` for explicit completion, or let the kernel complete the
  source when the handler exits.
- Keep the handler short. Every handler shares one 2048-byte interrupt stack,
  and nothing detects an overflow.
- If the handler turns interrupts on, it must turn them off again before it
  returns.
- `CheckInt()` reports a source's PLIC pending bit without claiming it.
- `ClearInt()` discards one pending controller request for a selected source.
- `SetIntMode()` is unavailable.

## Disabling and restoring interrupts with DI and EI

`DI(intsts)` disables interrupts and saves the previous interrupt state in the
variable `intsts`. `EI(intsts)` puts back the state that `DI()` saved. On this
RV32 kernel the interrupt state is `mstatus.MIE`, the machine-mode
interrupt-enable bit of the `mstatus` register. `isDI(intsts)` tells whether
interrupts were already disabled when `DI()` saved `intsts`. The
specification describes these macros in its
[μT-Kernel/SM interrupt management chapter](https://tron-forum.github.io/mtk3_spec_jp/interrupt_management_functions_sm.html).

### Declare intsts before calling DI

The caller must declare `intsts` before it calls `DI()` or `EI()`. The
specification does not say whether the caller declares it or `DI()` manages
it, but its usage example of `isDI()` declares it in the caller. Code that
does not declare it can fail to compile or behave in an undefined way.

This function declares `intsts` first:

```c
void foo() {
    UINT intsts; // User must declare intsts
    DI(intsts);
    if (isDI(intsts)) {
        // Interrupts were already disabled
    } else {
        // Interrupts were enabled before DI()
    }
    EI(intsts);
}
```

This function does not declare it:

```c
void foo() {
    DI(intsts); // Error: intsts is not declared
    EI(intsts);
}
```

### What EI restores

`EI(intsts)` restores the MIE state that `DI(intsts)` recorded, whatever the
MIE state is when `EI()` runs. Pass it either the value that `DI()` saved or
zero. `EI(0)` enables interrupts. The saved value is the token that `DI()`
writes, and `isDI()` tests the saved MIE bit in it. `EI()` changes only
`mstatus.MIE`. The other `mstatus` bits keep their values, and so does `mie`,
the register that enables each kind of interrupt source.

A section that turns interrupts on in the middle therefore still ends with
them off if they were off before it:

```c
UINT intsts;
DI(intsts); // Interrupts were off, and intsts records it.
EI(0);      // On for a while.
/* ... */
EI(intsts); // Off again.
```

For nested critical sections, keep a separate token at each level. The inner
`EI()` restores the disabled state saved by its `DI()`, and the outer `EI()`
restores the state from before the outer `DI()`:

```c
UINT outer, inner;
DI(outer);
DI(inner);
EI(inner); // Still disabled.
EI(outer); // Restores the state from before the outer DI().
```

## Traps and the ISR state

A trap is the CPU's jump into the kernel on an interrupt. The kernel has trap
entries for the timer interrupt, the external interrupt, and the software
interrupt that it raises to switch tasks (software dispatch). While the CPU is
inside any of them, the kernel is in the ISR state
(ISR: interrupt service routine). `tkmc_isr_depth` counts the traps that are
running, and the ISR state, which `tkmc_in_isr()` reports, holds while that
count is not 0.

## External interrupts

External interrupts come through the PLIC (platform-level interrupt
controller) of the QEMU `virt` machine. The interrupt number of a source is
its PLIC source ID:

| Source | Interrupt number |
| --- | --- |
| virtio devices | 1 to 8 |
| UART | 10 |
| goldfish RTC (real-time clock) | 11 |

The kernel keeps a handler for each number from 1 to `CFN_MAX_INTNO - 1`.
`CFN_MAX_INTNO` is set in `include/sys/config.h` (32 at present). Number 0
does not exist.

### Defining a handler and enabling its source

| Call | Behavior |
| --- | --- |
| `tk_def_int(intno, pk_dint)` | Defines the handler of `intno`, replacing an earlier one. `pk_dint == NULL` cancels the definition. |
| `EnableInt(intno, level)` | Sets the PLIC priority of the source to `level` (1 to 7) and enables it. |
| `DisableInt(intno)` | Disables the source. |
| `EndOfInt(intno)` | Completes the newest matching active claim once, preserving its context until exit. |
| `ClearInt(intno)` | Discards one pending request for the selected source and restores controller settings. |
| `CheckInt(intno)` | Returns whether the source's PLIC pending bit is set at the time of one pending-register read. |

`TA_HLNG` selects a C function `void inthdr(UINT intno)`. `TA_ASM` selects
an assembly entry with the [RV32 assembly contract](assembly-entry.md).
`tk_def_int` returns these errors:

| Error | When |
| --- | --- |
| `E_CTX` | The call comes from an ISR, as the specification says, or runs with interrupts disabled, which the specification does not say. This check comes first: `tk_def_int(0, p)` with interrupts disabled returns `E_CTX`, not `E_PAR`. |
| `E_PAR` | `intno` is 0 or beyond the table, or `pk_dint` has a NULL `inthdr`. |
| `E_RSATR` | `intatr` is anything other than exactly `TA_HLNG` or `TA_ASM`. |

`E_NOMEM` is never returned, because the handler table is static.

`EnableInt` and `DisableInt` have no error return, and the specification does
not define a bad argument. In this kernel a bad `intno`, or a `level` outside
1 to 7, changes nothing.

`CheckInt` returns `FALSE` without reading a register for `intno == 0` or
`intno >= CFN_MAX_INTNO`. For a valid source it reads its PLIC pending word
once. The result is a snapshot of that bit: source enable, priority, threshold
and handler registration do not affect it. A claimed source normally has its
pending bit cleared, so `CheckInt` can return `FALSE` while its handler is
running. The call does not claim or complete the source and does not change
interrupt state.

### Clearing a pending request

`ClearInt(UINT intno)` discards at most one pending request for the selected
PLIC source. It is available in tasks and handlers, including with interrupts
already disabled and without a current task. Invalid numbers, including 0
and numbers at or above `CFN_MAX_INTNO`, return without MMIO.

The call disables CPU interrupts, saves all context-0 source-enable words,
and temporarily enables only the selected source. This covers all 96 PLIC
source positions, including sources outside the kernel's handler table.
If the selected priority is 0, it temporarily becomes 1. The threshold is
temporarily set to 0 after isolation. A single claim read
then consumes the selected pending request, if present. A nonzero claim is
completed while its source is enabled. The priority, all enable words and
the caller's MIE are restored before returning. After all enable words are
restored, the call restores the saved threshold to refresh QEMU notification
state, as `EnableInt` does. The caller's threshold value is restored and `mie`
remains unchanged. Other sources keep their requests. The PLIC specification
defines claim selection independently of threshold. The tested QEMU build
filters claims by threshold, which requires the temporary zero value. MIE
remains off throughout the isolated claim and controller restoration.

The call preserves all active handler contexts and their completion flags,
including when it clears a new request for a source whose earlier handler
has called `EndOfInt`. It does not complete an outstanding handler claim or
request dispatch. Later C and ASM exits retain their completion behavior.

The driver clears the device's interrupt cause. A device whose cause remains
asserted can request another interrupt after completion. A request that arrives
before the one claim read can be discarded by this call. A later request can
remain pending. The function returns after that single attempt.

### How the kernel runs a handler

A handler runs in the ISR state, on the interrupt stack (see
[Interrupt stack](#interrupt-stack)), with interrupts disabled unless it turns
them on (see [Nested interrupts](#nested-interrupts)).

The kernel claims the source before it calls the handler. Completion lets the
PLIC accept a new request from that source. `EndOfInt` can complete the claim
during the handler. The normal C or assembly exit completes it automatically
when explicit completion has not occurred.

- A handler must silence its device. Completion permits a new request from a
  source whose device condition is still asserted.
- A handler should be short and use little stack. The interrupt stack is 2048
  bytes, shared by every function the trap calls, and nothing checks its depth.
- A handler may call `DisableInt` on its own source to mask it until deferred
  work is done. The kernel still completes the source, which the PLIC would
  otherwise ignore because the source is disabled, and the source stays
  disabled.
- A task that a handler wakes runs at the outermost exit for `TA_HLNG` and
  `tk_ret_int`. A direct ASM return preserves the dispatch request for the
  subsequent software interrupt.

### Explicit completion

`void EndOfInt(UINT intno)` searches the active context chain from its head
and uses the first record with that source number, including an enclosing
handler's record. It issues EOI synchronously and marks that record completed.
A repeated call stops at the same completed record. It preserves the source's
enabled/disabled state and restores the caller's MIE state.

Valid numbers are `1 <= intno < CFN_MAX_INTNO`. An invalid number, an inactive
source or an already-completed matching record causes no controller access.
The operation is available in task and ISR contexts and requires no current
task. A task with no active matching claim simply returns.

Complete the device's work before calling `EndOfInt`. EOI permits the gateway
to report another request, so a device that continues asserting its condition
can become pending again. Pending-source queries are described above.

The context stays linked with its original depth and threshold until its real
exit. C return, `tk_ret_int` and `TKMC_RET_INT_ASM` still restore threshold,
remove the context and restore the frame. Each retains its dispatch policy.
Their completion block runs only for a claim that needs EOI. The kernel also
handles a source disabled by its handler by briefly enabling it for EOI and
restoring its disabled state.

### Sources without a handler

The default handler switches off a source that has no handler: the kernel
completes it, clears its enable bit and does nothing else. Leaving it enabled
would raise it again at once for as long as the device asserts it.

### EnableInt and a source that is already pending

The specification says `EnableInt` generates the interrupt of a source that is
already pending. The tested QEMU build does not raise a source that was already pending
when its enable bit was set until the PLIC looks for a source again. Writing
the PLIC threshold makes it look, so `EnableInt` also writes the threshold,
with the value it already has.

## Calls from a handler

An interrupt handler runs in what the specification calls the
task-independent portion: code that runs outside any task. The specification
lists a set of calls that must be callable there and makes the availability of
other calls implementation-dependent. Its
[System Calls](https://tron-forum.github.io/mtk3_spec_jp/system_calls.html#system_call_possible_from_task_independent_portion)
and [API Notation](https://tron-forum.github.io/mtk3_spec_jp/api_notation.html#api_notation_context)
sections describe this distinction. This kernel accepts the required calls it
implements and applies the policy in the table to the others:

| Call | In a handler |
| --- | --- |
| `tk_sta_tsk`, `tk_rel_wai`, `tk_wup_tsk`, `tk_sig_sem`, `tk_set_flg` | Required callable APIs; allowed by this kernel. |
| `tk_cre_tsk`, `tk_cre_sem`, `tk_cre_flg`, `tk_cre_mbx`, `tk_del_sem`, `tk_del_mbx`, `tk_clr_flg`, `tk_snd_mbx` | The specification makes availability implementation-dependent. This kernel allows these calls with valid arguments and their normal error returns. |
| `tk_slp_tsk`, `tk_dly_tsk`, `tk_wai_sem`, `tk_wai_flg`, `tk_rcv_mbx`, `tk_def_int`, `tk_set_utc`, `tk_get_utc` | Return `E_CTX` and change nothing (see the rules of each call in [behavior.md](behavior.md)). |
| `tk_ext_tsk`, `tk_exd_tsk` | Stop the kernel. |

The rules in this table are verified for calls from a single-level handler,
one that does not run nested inside another handler. Calls from a nested
handler need separate validation. This kernel has no quasi-task portion,
another context that the API Notation section defines, so the specification's
rules for that context do not apply.

## Interrupt stack

Each trap (software dispatch, timer and external interrupt) pushes its
128-byte frame of saved registers on the stack of the task it interrupted,
then moves to the interrupt stack for its C code: the handler and
`schedule()`. A nested external interrupt is already on the interrupt stack,
so it pushes its frame there and stays. A task stack therefore needs room for
that frame and nothing more, however deep a handler calls.

The interrupt stack is the `.isr_stack` section of `linker.ld`, 2048 bytes,
placed after the boot stack (`.stack`, the stack the kernel starts on). Stacks
grow down, so an overflow runs into the boot stack, which nothing uses after
boot, and not into the kernel's tables. The two stacks are also separate
because the fatal exception entry takes the boot stack anew. `tkmc_start`
runs on the boot stack with interrupts still off from reset. They come on only
as the first task starts, so no trap is taken there.

Nothing detects an overflow. RAM is flat and the kernel uses no PMP (the
RISC-V physical memory protection unit), so the interrupt stack runs into the
boot stack below it.

### Stack size and nesting

The stack held seven levels of the tested handlers in each of the six cross
presets the record names (`release` was not run). A trap that starts inside a handler pushes a 128-byte frame, a
32-byte external-entry record and its own handler's frames on the same stack.
The PLIC has 7 priorities. A chain admitted only at strictly increasing priorities can
therefore reach seven levels. Handler call frames also consume this stack.
The [deep nesting record](../developer/notes/deep-interrupt-nesting-validation.md)
gives the tested handlers' written high-water marks and separate bounds on
reserved stack space for each build configuration.

### Measured stack use

The `interrupt` QEMU suite (`test/suites/interrupt.c`; QEMU suites are
described in [run_qemu.md](../developer/notes/run_qemu.md)) measures the most
of the interrupt stack that its paths use, its high-water mark, and fails
above a limit. The counts below were measured on 2026-11-14 in the `debug`
build preset (`CMakePresets.json`) using the instruction-count clock.

| Paths | Observed bytes | Test fails above |
| --- | --- | --- |
| One level with external/timer work and a task switch | 196 | 768 |
| Two levels with high-level UART and RTC handlers | 436 | 1024 |
| ASM outer handler with a high-level inner handler | 420 | 1024 |
| High-level outer handler with a direct-return ASM inner handler | 452 | 1024 |
| High-level outer handler with an ASM inner handler using `tk_ret_int` | 452 | 1024 |

The suite checks the limits but does not print the counts. Diagnostic-only
copies of the suite recorded them. They are observed counts, and interrupt
timing can affect them. The suite covers its tested paths, not the worst case
of a handler you write.

The `interrupt-depth-3` to `interrupt-depth-7` suites
(`test/suites/interrupt_depth.c`) nest three to seven levels by alternating
the two sources that the QEMU `virt` board lets software raise. They fail if
a level's stack pointer leaves the interrupt stack, if the boot stack below
it changes, or if the high-water mark reaches 2048 bytes. On 2026-11-18 the
largest seven-level count was 1972 bytes in `debug` and 1600 to 1744 bytes in
the `relwithdebinfo` and `minsizerel` presets and their LTO variants. The
[deep nesting record](../developer/notes/deep-interrupt-nesting-validation.md)
lists every depth and preset, and the limits of these figures.

## Nested interrupts

A handler may turn interrupts on with `EI()`. Only a source of a higher
priority can then interrupt it. Before the kernel calls a handler, it raises
the PLIC threshold to the priority of the source it serves, and it puts the
threshold back after the handler returns. The PLIC raises only a source whose
priority is above the threshold, so a source of the same or a lower priority
waits until the handler returns. Nesting is at most 7 deep, the number of
priorities.

- A handler must turn interrupts off again with `DI()` before it returns. The
  kernel does not do it: it puts back the threshold, completes the source and
  restores the trap frame with interrupts as the handler left them, and an
  interrupt in the middle would find that work half done. `EI()` with the value that a `DI()`
  saved on entry, when interrupts were off, turns them off as well (see
  [What EI restores](#what-ei-restores)).
- The timer and software interrupts do not nest. The kernel masks them (it
  clears `MTIE` and `MSIE`, their enable bits in `mie`) from the entry of the
  outermost external
  interrupt trap until its exit, whether the handler turns interrupts on or
  not. A tick that falls due meanwhile is served once the handler has
  returned, and the software interrupt that a task switch asks for never
  happens inside a handler.
- Nested traps defer a requested task switch to the outermost exit.
  `TA_HLNG` and `tk_ret_int` service that request at the outermost external
  exit. A direct ASM exit leaves it to the subsequent software trap.
- The calls that turn interrupts off and on themselves (`EnableInt`,
  `DisableInt`, and every kernel call that disables interrupts for a moment)
  put the interrupt flag back as they found it, so they are safe in a handler
  that has interrupts on.

## Interrupt calls that are not provided

The kernel provides `EnableInt`, `DisableInt`, `CheckInt`, `ClearInt` and
`EndOfInt` under `TK_SUPPORT_INTCTRL`. `TK_HAS_ENAINTLEVEL` selects the
priority argument to `EnableInt`. `SetIntMode` is unavailable and
`TK_SUPPORT_INTMODE` is false.

## How the interrupt behavior is tested

The [ClearInt validation record](../developer/notes/clearint-validation.md)
records target isolation, controller-state restoration and fault detection.

The [completion validation record](../developer/notes/interrupt-completion-validation.md)
records explicit EOI cases, actual MMIO counts and isolated fault detection.

The [CheckInt validation record](../developer/notes/checkint-validation.md)
records its host and real-ISR checks, public consumer linkage and fault
experiments.

Two kinds of test cover this document. QEMU suites run on the emulated RV32
machine (see [run_qemu.md](../developer/notes/run_qemu.md)). PC unit tests
compile the kernel sources for the development PC and run them on a fake
machine, without QEMU (see [testing.md](../developer/testing.md)).

- `DI()` and `EI()`: the `interrupt-state` QEMU suite
  (`test/suites/interrupt_state.c`) restores a saved MIE state, enabled and
  disabled, after each of the two MIE states just before `EI()`, and checks
  `EI(0)`. It checks that restoring a disabled state does not briefly take a
  pending software interrupt, and it runs nested `DI()`/`EI()` with interrupts
  initially on and off. The PC regression test is
  `test/host/test_interrupt_state.c`.
- External interrupts: the `interrupt` QEMU suite tests the calls with the
  UART transmitter-empty interrupt, and nesting with the UART and the RTC
  alarm. Software raises both. It raises the RTC by an alarm at a time that
  has already passed, which does not depend on the clock the machine runs on.
- What QEMU cannot be made to produce (a claim with no source, a source beyond
  the table) and the PLIC threshold are tested by the PC unit tests in
  `test/host/test_intctl.c`.
