# Assembly task and external-interrupt entries

This RV32 `rv32imac`/`ilp32`, single-hart port provides `TA_ASM` task and
PLIC external-interrupt entries. `TK_SUPPORT_ASM` advertises these entries.
The entry conventions below are the implementation-defined part of the
[task](https://tron-forum.github.io/mtk3_spec_jp/utk_os_functions.html) and
[interrupt](https://tron-forum.github.io/mtk3_spec_jp/interrupt_management_functions_os.html)
contracts in μT-Kernel 3.0 Ver.3.00.01.

## Task entry and termination

Pass the address of an assembly routine in `T_CTSK.task` and select
`TA_ASM | TA_USERBUF`. The task starts with the following state.

| Register | Value |
| --- | --- |
| `a0` | Signed `stacd` passed to `tk_sta_tsk` |
| `a1` | `exinf` from task creation |
| `sp` | User-stack upper end rounded down to a 16-byte boundary |
| `ra` | `tk_ext_tsk` |

Follow the RV32 ILP32 calling convention when calling C or kernel APIs.
Keep `gp` and `tp` unchanged. The other initial general-register values are
unspecified. Reserve stack space for the 128-byte trap frame as well as the
task's own call frames.

The routine may finish with `ret`, `tail tk_ext_tsk`, or `tail tk_exd_tsk`.
Both `ret` and `tk_ext_tsk` leave a dormant task that can be restarted.
`tk_exd_tsk` deletes it. Each requires task context, a current task and
enabled interrupts. An invalid termination records a fatal context error and
stops with interrupts disabled. In particular, turn interrupts back on before
returning from a task that disabled them.

Restart reconstructs the entry frame and supplies the new `stacd` with the
original creation-time `exinf`. Deleted task-control blocks can be reused
without restoring a previous task's frame.

## External-interrupt entry

Register an assembly symbol through `T_DINT.inthdr` with `intatr = TA_ASM`.
Source numbers, priorities, registration errors and device control follow
[Interrupts](interrupts.md). The kernel claims the PLIC source, saves the
interrupted registers and jumps to the assembly entry after its preparation
calls have returned.

| Entry state | Value |
| --- | --- |
| `a0` | Claimed PLIC source number |
| `a1` | Read-only pointer to this trap's 128-byte saved frame |
| `sp` | 16-byte-aligned interrupt-stack entry pointer |
| `mstatus.MIE` | Disabled |
| `mie.MSIE`, `mie.MTIE` | Masked until the outermost external trap ends |
| `ra` | Fatal-return guard |

The handler owns its downward-growing stack below entry `sp`. A 32-byte
kernel context record begins at entry `sp`. Leave that record and the saved
frame unchanged. Restore the exact entry `sp` before either interrupt exit.
Save it in a normal stack frame or a callee-saved register if the handler
calls C. Follow the ILP32 calling convention for each call.

`gp` and `tp` must remain unchanged. Other entry register values are
unspecified. The saved frame provides the interrupted values. The handler may
change `mstatus.MIE` to allow higher-priority external nesting, then must
disable it before exiting. Preserve `mstatus.MPIE` and all other CSR state.
Keep `mie.MSIE` and `mie.MTIE` masked throughout the handler, including during
nesting. The timer and software trap paths rely on that exclusion when using
the shared interrupt stack.

Silence the device before exiting. Use `EnableInt` and `DisableInt` for source
control. `EndOfInt(intno)` issues an optional explicit EOI through the C ABI.
It returns to the handler with the caller's interrupt state and context intact.
The exit paths coordinate that completion and own the PLIC threshold, context
chain and ISR depth. A normal `ret` from an interrupt handler stops
the kernel through the fatal-context path.

### Saved frame

Offsets are RV32 word indices, each four bytes. The interrupted `sp` is the
frame address plus 128 bytes.

| Word | Saved value |
| --- | --- |
| 0 | `ra` |
| 1–3 | `t0`–`t2` |
| 4–5 | `s0`–`s1` |
| 6–13 | `a0`–`a7` |
| 14–23 | `s2`–`s11` |
| 24–27 | `t3`–`t6` |
| 28 | `mepc` |
| 29 | Outer trap's saved `mie.MSIE`/`mie.MTIE` bits |
| 30–31 | Reserved |

### Returning with delayed dispatch

After restoring entry `sp` and disabling MIE, use `tail tk_ret_int`.
`tk_ret_int` completes the current source when needed, pops its context and
restores the interrupted frame. At the outermost exit it services a pending dispatch
request before restoring the selected task. Nested exits resume the enclosing
handler and defer dispatch to the outermost exit.

`tk_ret_int` requires a current task and the active, matching-depth `TA_ASM`
external entry. Calls from tasks, high-level handlers, timer/software traps,
or an incorrect stack or interrupt state record `TKMC_CONTEXT_RET_INT` and
stop with interrupts disabled. It never returns to its caller.

### Direct return

Include `<tk/asm/rv32/interrupt.h>` in a preprocessed `.S` file. After restoring
entry `sp` and disabling MIE, expand `TKMC_RET_INT_ASM`. On a valid entry the
macro performs the return inline, without a C exit call or a scheduler call.
It restores the PLIC threshold, completes the source if EOI is still needed,
preserves a source disabled by the handler, pops the context, restores the saved registers and
outer interrupt masks, and executes `mret`. Invalid use takes the fatal-context
path.

A pending software-dispatch request remains pending. The CPU can take that
software interrupt immediately after `mret`, before the interrupted task
executes another instruction. Dispatch then occurs in the software trap.

Both exits read the per-record completion flag, so `EndOfInt` followed by
either exit issues one EOI per claim. The 32-byte context layout is retained.
Call `EndOfInt` through the ordinary C ABI from an aligned call frame, then
restore entry `sp` before the interrupt exit.

Both exits restore machine privilege before `mret`. The public macro and
kernel context layout are checked together by target-side static assertions.
Use the matching header and kernel build together.

## Nesting and stack limits

`TA_ASM` and `TA_HLNG` handlers can nest in either order. Only a higher PLIC
priority can interrupt an active handler. Each external entry keeps a separate
context record on the shared 2048-byte interrupt stack. A nested entry also
needs its 128-byte saved frame and handler call frames. Interrupt-stack
overflow is unchecked.

The software-generated UART and RTC interrupts permit two-level nesting
tests on QEMU `virt`. The seven PLIC priorities bound possible nesting, while
actual safe depth also depends on handler stack use. Size application
handlers for that stack budget. Hardware-device behavior and deeper nesting
require platform-specific validation.

## Verification

The `interrupt` QEMU suite uses assembly task and interrupt fixtures through
the real RV32 trap paths. Host tests exercise registration and controller
bookkeeping. Dedicated invalid-context QEMU tests observe fatal records.
See the [testing guide](../developer/testing.md) for configure, build and CTest
commands, and [Interrupts](interrupts.md#how-the-interrupt-behavior-is-tested)
for the existing PLIC and stack checks. The
[validation record](../developer/notes/assembly-entry-validation.md) records
the build matrix, isolated faults, observed failures and restoration checks.
