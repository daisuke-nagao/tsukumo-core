# Interrupts

## Interrupt status (`intsts`) declaration in `DI/EI` usage

### Overview

In uT-Kernel, the `DI()` and `EI()` macros are used to disable and enable interrupts, respectively. However, these macros require an `intsts` variable to store the previous interrupt status. The specification does not explicitly state whether the caller must declare `intsts` or if it is automatically managed by `DI()`.

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
| `tk_cre_tsk`, `tk_cre_sem`, `tk_cre_flg`, `tk_cre_mbx`, `tk_del_sem`, `tk_del_mbx`, `tk_clr_flg`, `tk_snd_mbx` | This kernel's policy returns `E_CTX` and changes nothing. In particular, `tk_snd_mbx` from an ISR returns `E_CTX`; notify a task with `tk_sig_sem` or `tk_set_flg` and let it send the message. |
| `tk_slp_tsk`, `tk_dly_tsk`, `tk_wai_sem`, `tk_wai_flg`, `tk_rcv_mbx`, `tk_def_int` | Return `E_CTX` and change nothing (see their own rules). |
| `tk_ext_tsk`, `tk_exd_tsk` | Stop the kernel. |

To pass a message to a task from a handler, signal a semaphore or set an event
flag, and let the task send or receive the message.

The test is `tkmc_in_isr()` alone, so interrupts off is not a reason to refuse:
a task inside `DI()` and the startup code, which runs before the first task,
still create objects and send messages. The check comes after the argument
checks that need no object state and before every check that does:

| Call | Order of errors |
| --- | --- |
| `tk_cre_*` | `E_PAR` or `E_RSATR`, `E_CTX`, `E_LIMIT` |
| `tk_del_sem`, `tk_del_mbx`, `tk_clr_flg` | `E_ID`, `E_CTX`, `E_NOEXS` |
| `tk_snd_mbx` | `E_ID`, `E_PAR` (NULL message), `E_CTX`, `E_NOEXS`, `E_PAR` (priority) |

The specification says calls beyond its required list may be available
depending on the implementation; its API notation also explains that `×`
context cells can represent implementation-dependent availability. This
kernel's `E_CTX` result for the eight calls above is therefore a policy choice,
with a compatibility consequence for applications that sent mail from an
ISR. See the specification's [System Calls](https://tron-forum.github.io/mtk3_spec_en/system_calls.html#apis-possible-from-task-independent-portion)
and [API Notation](https://tron-forum.github.io/mtk3_spec_en/api_notation.html#valid-context).
The kernel has no quasi-task portion, so the distinction does not arise.

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
how much of the stack the deepest paths use (a handler that signals a semaphore
that a higher-priority task waits for, the end of a timed wait, and a task
switch): 196 bytes with `debug` and 64 bytes with `minsizerel`. The test fails
above 768 bytes. It covers those paths, not the worst case of a handler you
write.

`tkmc_isr_depth` counts the traps that are running. The ISR state
(`tkmc_in_isr()`) holds while it is not 0.

The stack is sized for nesting: a trap that starts inside a handler pushes a
128-byte frame and its own handler's frames on the same stack. The PLIC has 7
priorities, which is the deepest nesting there can be, so 2048 bytes leave about
290 bytes for each level. Two levels (the UART interrupted by the RTC) use 468
bytes with `debug` and 240 bytes with `minsizerel`; the test fails above 1024.
Deeper nesting is not measured, because the QEMU `virt` board has only two
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
  middle would find them half done.
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
