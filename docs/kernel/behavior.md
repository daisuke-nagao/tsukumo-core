# Kernel behavior

This page is for application and board developers. It describes what this
kernel does when startup fails, which calls work in which context, and how long
mailbox calls keep interrupts disabled. Where the specification leaves a choice
to the implementation, the page states the choice this kernel makes. The
specification is the official Japanese [μT-Kernel 3.0 specification
Ver.3.00.01](https://tron-forum.github.io/mtk3_spec_jp/index.html). For a
call-by-call review against it, see the
[specification review](spec-conformance-review.md). For interrupt handlers, see
[interrupts.md](interrupts.md).

## Terms used on this page

| Term | Meaning |
| --- | --- |
| Current task | The task that is running. There is none before the first task starts. |
| Interrupts enabled | The `MIE` bit of the `mstatus` register is set. `MIE` is the global interrupt-enable bit of RISC-V machine mode. `DI()` clears it, and `EI()` puts back the state that `DI()` saved. |
| `mie` | A separate RISC-V register with one enable bit for each interrupt source (timer, software, external). It is not the `MIE` bit. |
| ISR state | The CPU is inside one of the kernel's trap entries: the timer interrupt, an external interrupt, or the software interrupt that switches tasks. ISR stands for interrupt service routine. The trap entries count themselves in `tkmc_isr_depth`, and `tkmc_in_isr()` is true while that count is not 0. Each entry takes its count off before the interrupted task resumes. |
| Task-independent portion | The specification's name for code that runs outside any task, such as an interrupt handler. In this kernel it is code that runs in the ISR state. See [System Calls](https://tron-forum.github.io/mtk3_spec_jp/system_calls.html#system_call_possible_from_task_independent_portion) in the specification. |
| Weak hook | A function that the kernel defines with the `weak` attribute. A board replaces it by defining a function of the same name; the linker then uses the board's version. |
| `wfi` | The RISC-V "wait for interrupt" instruction. The default hooks stop the system in a loop of `wfi`. |
| `tkmc_` prefix | Marks a name that belongs to this kernel's own interface and is not part of the μT-Kernel API. |

The ISR state does not depend on `MIE`. A handler that turns interrupts on with
`EI()` is still in the ISR state, and a task that calls `DI()` is still a task.
The kernel supports a single hart (RISC-V hardware thread) only.

## Startup failure

Startup stops if the kernel cannot create or start the initial task (the task
that calls `usermain()`) or the idle task. It stops with interrupts disabled,
before scheduling starts and before the timer starts. The kernel does not undo
the steps that succeeded and does not retry.

To find out which step failed, read `tkmc_last_startup_failure` in
[`kernel/panic.h`](../../kernel/panic.h) with a debugger. It holds the stage
that failed and the error code that the failing call returned:

| `stage` value | Failed step |
| --- | --- |
| `TKMC_STARTUP_NONE` | No startup failure was recorded. |
| `TKMC_STARTUP_INITIAL_CREATE` | Creating the initial task |
| `TKMC_STARTUP_INITIAL_START` | Starting the initial task |
| `TKMC_STARTUP_IDLE_CREATE` | Creating the idle task |
| `TKMC_STARTUP_IDLE_START` | Starting the idle task |

This record is separate from `tkmc_last_trap`, which records a fatal CPU
exception.

After recording the failure, the kernel calls the weak hook
`tkmc_startup_panic(stage, error)`. The default waits forever in a `wfi` loop.
A board can replace it to report the failure. The replacement is entered with
interrupts disabled and must keep them disabled, must not call task APIs, and
must not return.

Tests: the six `startup-*` QEMU suites. Five of them make one startup call fail
through test-only link wrappers, which redirect a call to a test version in
that test program only (the linker's `--wrap` option). They check the record
and that no later startup step runs. `startup-idle-start-mie` injects the
failure with interrupts enabled, and `startup-success` checks normal startup
through the same wrappers. Each passes only when it prints `STARTUP FAILURE OK`
or `STARTUP SUCCESS OK`.

## Context rules for wait calls

The wait calls are `tk_slp_tsk`, `tk_dly_tsk`, `tk_wai_sem`, `tk_wai_flg` and
`tk_rcv_mbx`. Each needs all of the following when it is called:

- a current task,
- interrupts enabled,
- the CPU outside the ISR state.

If any of these is missing, the call returns `E_CTX` after its argument checks.
It changes no task or object state and writes no output parameter. This holds
even when the call would not have waited: a poll (`TMO_POL`), a request that
could succeed at once, or a zero delay (`tk_dly_tsk(0)`).

Because the ISR state is independent of `MIE`, a wait call from a handler that
has turned interrupts on also returns `E_CTX`.

Tests: the `wait-context` QEMU suite calls each wait call with interrupts
disabled. The `task-context-*` QEMU suites call them from a real timer
interrupt and before the first task starts, when there is no current task. In
the timer handler they also clear every enable bit in `mie`, turn `MIE` on, and
check that each wait call still returns `E_CTX` and changes nothing. The PC
unit tests cover the same contexts.

## Ending a task from an invalid context

`tk_ext_tsk` and `tk_exd_tsk` need the same context as the wait calls: a
current task, interrupts enabled, and the CPU outside the ISR state. They have
no error return, so a call from any other context stops the whole system. It
stops before it changes the task's state. `tk_exd_tsk` also stops before it
returns the task's control block to the free list.

The kernel records the call in `tkmc_last_context_failure` in
[`kernel/panic.h`](../../kernel/panic.h): the API that was called and
`E_CTX`. A value of zero means no failure. This record is separate from the
CPU exception record and the startup record.

The kernel then calls the weak hook `tkmc_context_panic(api)`. The default
loops on `wfi`. A board may replace it for reporting. The replacement is
entered with interrupts disabled and must keep them disabled, must not call
task APIs, and must not return.

Tests: the `task-context-*` QEMU suites, one program per case:

| Suite | What it checks |
| --- | --- |
| `task-context-isr` | Wait calls and other calls from a real timer interrupt, and that a task the handler wakes runs only after the handler returns |
| `task-context-no-current-wait` | Wait calls before the first task starts |
| `task-context-di-ext`, `task-context-di-exd` | `tk_ext_tsk` and `tk_exd_tsk` with interrupts disabled |
| `task-context-isr-ext`, `task-context-isr-exd` | `tk_ext_tsk` and `tk_exd_tsk` from a timer interrupt |
| `task-context-no-current-ext`, `task-context-no-current-exd` | `tk_ext_tsk` and `tk_exd_tsk` before the first task starts |

Nested external interrupts are tested in the `interrupt` suite.

## Waking a task with tk_wup_tsk

`tk_wup_tsk` uses the ISR state, not `MIE`, to tell a call from a task apart
from a call from an ISR.

| Caller and target | Result |
| --- | --- |
| A task wakes itself, with interrupts enabled or disabled | `E_OBJ`. Nothing changes. |
| An ISR wakes the task it interrupted | The call queues a wake-up request for that task. If 4095 requests are already queued, it returns `E_QOVR`. |
| Any context wakes another task | Allowed |

Tests: the `wakeup-context` QEMU suite calls `tk_wup_tsk` from the real timer
interrupt. [wakeup-context-mutation.md](../developer/notes/wakeup-context-mutation.md)
records which faults in this check the tests detect.

## Calls from interrupt handlers

The specification lists the calls that must work in the task-independent
portion and leaves the others to the implementation. Of the calls this kernel
implements, the required ones are `tk_sta_tsk`, `tk_rel_wai`, `tk_wup_tsk`,
`tk_sig_sem` and `tk_set_flg`. This kernel also allows `tk_cre_tsk`,
`tk_cre_sem`, `tk_cre_flg`, `tk_cre_mbx`, `tk_del_sem`, `tk_del_mbx`,
`tk_clr_flg` and `tk_snd_mbx` from a handler, with valid arguments and their
normal error returns. The wait calls return `E_CTX`, and `tk_ext_tsk` and
`tk_exd_tsk` stop the system.

This list is verified for a single-level handler only. Calls from a nested
handler (one that interrupted another handler) need separate validation.

[interrupts.md](interrupts.md) has the full table of calls and the
specification references. It also covers how to define a handler with
`tk_def_int`, `EnableInt` and `DisableInt` for the PLIC (the external interrupt
controller), the interrupt stack that handlers run on, how much a task stack
needs for a trap, and the rules for nested interrupts.

## Interrupt-disabled time of mailbox calls

Most mailbox calls keep interrupts disabled for a time that does not depend on
how many messages are queued. The kernel does not limit that number.

- `tk_del_mbx` does not visit the queued messages.
- In a `TA_MPRI` mailbox, which queues messages by their priority `msgpri` (a
  smaller value is a higher priority), a `tk_snd_mbx` whose message has the
  same or a lower priority than the last queued one appends it without a scan.
- A `TA_MPRI` message with a higher priority than every queued message goes to
  the head at the first step of the scan.

The exception is a `TA_MPRI` message that goes between two queued messages. The
kernel finds its place by a scan from the head, so the time grows with the
number of messages ahead of it. A constant time for this send would need one
queue per priority, and so a limit on `msgpri`, which the specification does
not give.

The budget for this send is 31250 instructions, a tenth of a 10 ms tick when
QEMU runs with `-icount shift=5` (QEMU then counts guest time as 2^5 ns per
instruction). The budget holds for 256 queued messages in the QEMU Debug and
MinSizeRel configurations. Latency on hardware needs a measurement on the
target. The `mailbox-cost` QEMU suite measures these calls, and
[mailbox-di-budget.md](../developer/notes/mailbox-di-budget.md) records the
budget and the counts.

After `tk_del_mbx`, the messages that were still queued belong to the
application again. Their links are undefined, and sending a message again
overwrites them.

## Stop records and hooks

When the kernel stops the system, it leaves one of three records in
[`kernel/panic.h`](../../kernel/panic.h) and calls a weak hook that a board can
replace:

| Cause | Record | Hook | Default hook |
| --- | --- | --- | --- |
| Fatal CPU exception (illegal instruction, access fault and others) | `tkmc_last_trap`: `mcause`, `mepc`, `mtval` | `tkmc_panic(mcause, mepc, mtval)`. A replacement must not fault: a second trap overwrites `tkmc_last_trap`. | `wfi` loop |
| Startup failure | `tkmc_last_startup_failure`: `stage`, `error` | `tkmc_startup_panic(stage, error)` | `wfi` loop |
| `tk_ext_tsk` or `tk_exd_tsk` from an invalid context | `tkmc_last_context_failure`: `api`, `error` (`E_CTX`) | `tkmc_context_panic(api)` | `wfi` loop |

None of the hooks may return.
