# Interrupts

## Interrupt status (`intsts`) declaration in `DI/EI` usage

### Overview

In uT-Kernel, the `DI()` macro disables interrupts and the `EI()` macro puts back the state `DI()` saved. However, these macros require an `intsts` variable to store the previous interrupt status. The specification does not explicitly state whether the caller must declare `intsts` or if it is automatically managed by `DI()`.

From the `isDI()` usage example provided in the specification, it becomes evident that `intsts` must be declared by the caller before invoking `DI()` and `EI()`. This document clarifies this requirement to avoid ambiguity and ensure correct usage.

### Requirement for `intsts` Declaration

The caller is responsible for declaring `intsts` before calling `DI()` or `EI()`. Failing to do so may result in undefined behavior or compilation errors.

#### Correct Usage Example

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

#### Incorrect Usage Example

```c
void foo() {
    DI(intsts); // Error: intsts is not declared
    EI(intsts);
}
```

### What `EI()` puts back

`EI(intsts)` restores the MIE state recorded by `DI(intsts)`, regardless of the
MIE state when `EI()` runs. Pass either the value returned by `DI()` or zero.
`EI(0)` enables interrupts. The saved value keeps the existing DI token format
and `isDI()` continues to test its saved MIE bit. EI changes only
`mstatus.MIE`; other `mstatus` bits and the interrupt-source mask in `mie` are
preserved. A section that turns interrupts on in the middle therefore still
ends with them off if they were off before it:

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

The `interrupt-state` QEMU suite (`test/suites/interrupt_state.c`) checks saved
MIE enabled and disabled after either possible MIE state immediately before
`EI()`, plus `EI(0)`. It checks that restoring a disabled state does not
briefly take a pending software interrupt, and exercises nested `DI()`/`EI()`
with interrupts initially on and off. The host regression is in
`test/host/test_interrupt_state.c`.

### References

- uT-Kernel 3.0 Specification
- Example usage in existing uT-Kernel documentation

By explicitly stating this requirement in the documentation, developers can avoid confusion and ensure proper interrupt management in their applications.

## External interrupts

External interrupts come through the PLIC of the QEMU `virt` machine. The
interrupt number of a source is its PLIC source ID: the UART is 10, the
goldfish RTC is 11 and the virtio devices are 1 to 8. The kernel keeps a handler
for each number from 1 to `CFN_MAX_INTNO - 1` (`include/sys/config.h`); number 0
does not exist.

| Call | Behavior |
| --- | --- |
| `tk_def_int(intno, pk_dint)` | Defines the handler of `intno`, replacing an earlier one. `pk_dint == NULL` cancels the definition. |
| `EnableInt(intno, level)` | Sets the PLIC priority of the source to `level` (1 to 7) and enables it. |
| `DisableInt(intno)` | Disables the source. |

`tk_def_int` returns `E_PAR` for an `intno` that is 0 or beyond the table, and
for a `pk_dint` with a NULL `inthdr`. It returns `E_RSATR` unless `intatr` is
exactly `TA_HLNG`: the handler is a C function `void inthdr(UINT intno)`.
`E_NOMEM` is never returned, because the table is static. It returns `E_CTX`
from an ISR, as the specification says, and also with interrupts disabled,
which the specification does not say. That check comes first:
`tk_def_int(0, p)` with interrupts disabled returns `E_CTX`, not `E_PAR`. `EnableInt` and `DisableInt` have no error
return, and the specification does not define a bad argument. Here a bad
`intno` or a `level` outside 1 to 7 changes nothing.

A handler runs in the ISR state, with interrupts disabled unless it turns them
on (see "Nested interrupts"), on the interrupt stack (see below). The kernel
claims the source before the call and completes it after the call, so a handler
does not complete it and `EndOfInt` is not provided. A handler may call
`DisableInt` on its own source to mask it until deferred work is done; the
kernel still completes the source, which the PLIC would otherwise ignore, and
the source stays disabled. A task that a handler wakes runs after the outermost
handler returns, at the exit of the same trap.

A handler should be short and use little stack: the interrupt stack is 2048
bytes, shared by every function the trap calls, and nothing checks its depth. A
handler must also silence its device: the kernel completes the source when the
handler returns, and a source that is still asserted is taken again at once.

A source with no handler is switched off by the default handler: the kernel
completes it, clears its enable bit and does nothing else. Leaving it enabled
would raise it again at once for as long as the device asserts it.

`EnableInt` also writes the PLIC threshold, with the value it already has. QEMU
11.1.0 does not raise a source that was already pending when its enable bit was
set, until the PLIC looks for a source again, which a threshold write makes it
do. The specification says `EnableInt` generates the interrupt of such a source.

## Calls from a handler

The specification requires a set of calls from the task-independent portion
(a handler) and makes availability of other calls implementation-dependent.
This kernel accepts the required calls it implements and applies the policy in
the table to the others:

| Call | In a handler |
| --- | --- |
| `tk_sta_tsk`, `tk_rel_wai`, `tk_wup_tsk`, `tk_sig_sem`, `tk_set_flg` | Required callable APIs; allowed by this kernel. |
| `tk_cre_tsk`, `tk_cre_sem`, `tk_cre_flg`, `tk_cre_mbx`, `tk_del_sem`, `tk_del_mbx`, `tk_clr_flg`, `tk_snd_mbx` | The specification makes availability implementation-dependent. This kernel allows these calls with valid arguments and their normal error returns. |
| `tk_slp_tsk`, `tk_dly_tsk`, `tk_wai_sem`, `tk_wai_flg`, `tk_rcv_mbx`, `tk_def_int` | Return `E_CTX` and change nothing (see their own rules). |
| `tk_ext_tsk`, `tk_exd_tsk` | Stop the kernel. |

The μT-Kernel 3.0.01 specification requires the first group to be callable from
the task-independent portion. Availability of the eight calls in the second
group is implementation-dependent. Its [System
Calls](https://tron-forum.github.io/mtk3_spec_jp/system_calls.html#system_call_possible_from_task_independent_portion)
and [API Notation](https://tron-forum.github.io/mtk3_spec_jp/api_notation.html#api_notation_context)
sections describe this distinction. The verified API availability contract
covers calls from a single-level handler. Nested-handler support requires
separate validation. The kernel has no quasi-task portion, so that distinction
does not arise.

## Interrupt stack

Each trap (software dispatch, timer and external interrupt) pushes its 128-byte
frame on the stack of the task it interrupted, then moves to the interrupt
stack for the C code: the handler and `schedule()`. A nested external interrupt
is already on the interrupt stack, so it pushes its frame there and stays. A
task stack therefore needs
room for that frame and nothing more, however deep a handler calls. The
interrupt stack is the `.isr_stack` section of `linker.ld`, 2048 bytes, placed
after the boot stack (`.stack`). It is separate from the boot stack because
`tkmc_start` still runs on the boot stack between enabling interrupts and
launching the first task, and a trap in that window would overwrite its frame.

Nothing detects an overflow: RAM is flat and the kernel uses no PMP, so the
stack runs into the boot stack below it. The `interrupt` QEMU suite measures
the stack high-water across its normal paths, including a handler sending a
message to a waiting higher-priority task, the end of a timed wait, and a task
switch. Rechecked on 2026-11-09, the high-water was 212 bytes with `debug` and
56 bytes with `minsizerel`. These are observed counts and can depend on
interrupt timing. The test fails above 768 bytes. The suite covers its tested
paths, not the worst case of a handler you write.

`tkmc_isr_depth` counts the traps that are running. The ISR state
(`tkmc_in_isr()`) holds while it is not 0.

The stack is sized for nesting: a trap that starts inside a handler pushes a
128-byte frame and its own handler's frames on the same stack. The PLIC has 7
priorities, which is the deepest nesting there can be, so 2048 bytes leave about
290 bytes for each level. Rechecked on 2026-11-09, two levels (the UART
interrupted by the RTC) used 468 bytes with `debug` and 256 bytes with
`minsizerel`; the test fails above 1024. Diagnostic-only copies of the QEMU
suite recorded these high-water counts, and interrupt timing can affect them.
Deeper nesting is not measured,
because the QEMU `virt` board has only two
sources that software can raise.

## Nested interrupts

A handler may turn interrupts on with `EI()`. It is then interrupted by a source
of a higher priority, and by no other. The kernel raises the PLIC threshold to
the priority of the source it serves before it calls the handler and puts it
back after, so a source of the same or a lower priority waits until the handler
returns. The depth is at most 7, the number of priorities.

- A handler must turn interrupts off again with `DI()` before it returns. The
  kernel does not do it: the threshold, the completion and the trap frame are
  put back with interrupts as the handler left them, and an interrupt in the
  middle would find them half done. `EI()` with the value that a `DI()` saved
  on entry, when interrupts were off, turns them off as well.
- The timer and software interrupts do not nest. The kernel masks them (the
  `MTIE` and `MSIE` bits of `mie`) from the entry of the outermost external
  trap until its exit, whether the handler turns interrupts on or not. A tick
  that falls due meanwhile is served once the handler has returned, and the
  software interrupt that a task switch asks for never happens inside a
  handler.
- The task switch is made only at the exit of the outermost trap. A task that a
  nested handler wakes runs after the outermost handler returns, once: a nested
  trap leaves the request to the outer one.
- The calls of a handler that turn interrupts on and off themselves
  (`EnableInt`, `DisableInt`, and every kernel call that disables interrupts for
  a moment) put the interrupt flag back as they found it, so they are safe in a
  handler that has interrupts on.

Not supported: the calls `ClearInt`, `EndOfInt`, `CheckInt` and `SetIntMode`.
The `interrupt` QEMU suite tests the calls with the UART transmit-empty
interrupt and the nesting with the UART and the RTC alarm; both are raised by
software, the RTC by an alarm at a time that has already passed, which does not
depend on the clock the machine runs on. The claims that QEMU cannot be made to
give (no source, a source beyond the table) and the threshold are tested on the
PC.
