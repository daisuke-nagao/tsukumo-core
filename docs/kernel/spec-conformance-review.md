# Specification conformance review of the implemented API

This review compares the 40 APIs that tsukumo-core implements with the
official Japanese
[μT-Kernel 3.0 specification Ver.3.00.01](https://tron-forum.github.io/mtk3_spec_jp/index.html).
Read it to learn what the kernel conforms to today, where it still differs,
which choices it makes where the specification allows one, and how to repeat
the checks. [API support status](API_Support_Status.md) lists which APIs exist
at all, and the [review history](spec-conformance-history.md) tells how the
differences found so far were fixed.

## Scope

The review covers this configuration:

- RV32 `rv32imac`/`ilp32` on a single hart (one RISC-V hardware thread).
- The service profile in `include/sys/profile.h`.
- The 40 entries checked in [API support status](API_Support_Status.md),
  cross-checked against their public declarations and their definitions.
  Findings about the shared declarations and the service profile apply to all
  of them.

The specification also defines APIs that are available in every profile, such
as `tk_del_flg` and the object-reference calls (`tk_ref_*`), that the kernel
does not implement. [API support status](API_Support_Status.md) shows them as
missing; this review covers only the implemented subset and the contracts it
shares
([API and profile availability](https://tron-forum.github.io/mtk3_spec_jp/list_of_api_and_service_profiles.html)).

The initial review was made on 2026-11-08 (JST). Follow-ups on 2026-11-09
covered `EI`, and follow-ups on 2026-11-10 covered task exit, wait release,
the zero delay and the interrupt profile. Follow-ups on 2026-11-11 covered
the common public declarations and the time representation. A 2026-11-15
follow-up covers `CheckInt` and `EndOfInt`. A 2026-11-17 follow-up covers
`ClearInt`. The findings rest
on source inspection, compile probes and target observations recorded here; the
[evidence gaps](#evidence-gaps) list what still needs evidence.

## Assessment labels

Each entry in the [API inventory](#api-inventory) carries one or more of these
labels:

| Label | Meaning |
| --- | --- |
| Checked | The listed contracts agree with the inspected source and with the checks available in this configuration. |
| Difference | A specific discrepancy with the specification was reproduced. |
| Defined choice | The specification permits the choice the implementation makes. |
| Evidence gap | The listed requirement needs further verification. |

"Checked (Difference corrected *date*)" means that the review reproduced a
difference, the kernel was fixed on that date, and the entry now passes. The
[review history](spec-conformance-history.md) describes each fix.

## Current state

- All 40 implemented entries are Checked. Some also carry a Defined choice or
  an Evidence gap.
- Seven differences that the review found are corrected. `EI` did not restore a
  saved disabled state (fixed 2026-11-09). On 2026-11-10 the kernel was fixed
  so that an ended task can no longer resume its old run, an ISR can release
  the task it interrupted, `tk_dly_tsk(0)` no longer yields, and the interrupt
  profile flags announce the implemented `EnableInt` and `DisableInt` forms.
  On 2026-11-11 the generated
  public header gained `SYSTIM`, `LOCAL`, `EXPORT` and `IMPORT`, and the kernel
  gained `tk_set_utc` and `tk_get_utc` with `TK_SUPPORT_UTC` true. On 2026-11-15
  the kernel added `CheckInt` for PLIC pending snapshots and `EndOfInt` for
  explicit completion coordinated with automatic exits. On 2026-11-17 the
  kernel added `ClearInt` to discard one selected pending request.
- The implemented subset has no remaining reproduced differences. The
  [interrupt profile](#interrupt-controller-profile) now provides all five
  calls associated with `TK_SUPPORT_INTCTRL`.
- Assembly task and external-interrupt entries use the documented
  [RV32 assembly contract](assembly-entry.md). The remaining evidence gaps
  concern three or more distinct active sources, handler stack use outside
  the measured fixture, real hardware and more than one hart. The
  [deep nesting record](../developer/notes/deep-interrupt-nesting-validation.md)
  covers three-to-seven-level alternating-source chains.
- The assembly-entry update passed 167 PC unit tests, three public consumers
  and all 104 RV32 CTests in each of six presets. Its
  [validation record](../developer/notes/assembly-entry-validation.md) includes
  the isolated fault experiments and restoration checks.
- The time-representation update passed 162 PC unit tests, three host
  consumer checks and all 90 RV32 CTests in each of the six presets. The
  [test history](spec-conformance-history.md#test-runs-by-date) records this
  and the earlier results.

## Terms used in this review

| Term | Meaning |
| --- | --- |
| Specification | The official μT-Kernel 3.0 specification Ver.3.00.01, in Japanese. The section links in this review point into it. |
| Reference implementation | The TRON Forum's reference implementation of μT-Kernel 3.0: the [`mtkernel_3`](https://github.com/tron-forum/mtkernel_3) source released with the specification. The specification delegates detailed behavior to it ([reference-code role](https://tron-forum.github.io/mtk3_spec_jp/reference_code_of_utk.html)). |
| Task-independent portion, ISR | The specification calls code that runs outside any task, such as an interrupt handler, the task-independent portion. This review calls it an ISR (interrupt service routine). The kernel treats code as an ISR while it runs inside a trap, whatever the state of MIE; `tkmc_in_isr()` reports this (see [interrupts](interrupts.md)). |
| MIE | The machine interrupt-enable bit of the RISC-V `mstatus` register (`MSTATUS_MIE`, bit 3). While it is clear, the processor takes no interrupt. The `mie` register is a different thing: it enables individual interrupt sources. |
| `DI`, `EI`, `isDI` | Macros in `include/sys/interrupt.h`. `DI(intsts)` clears MIE and saves its previous state in the caller's variable `intsts`. `EI(intsts)` restores that state, and `EI(0)` sets MIE. `isDI(intsts)` tells whether the saved state was disabled. |
| Service profile | The macros in `include/sys/profile.h` that tell application code which optional features the kernel provides. `TK_SUPPORT_*` flags announce a feature (for example `TK_SUPPORT_INTCTRL`, interrupt-controller management) and `TK_HAS_*` flags a variant (for example `TK_HAS_ENAINTLEVEL`, an `EnableInt` that takes a level). |
| PLIC | The platform-level interrupt controller of the QEMU `virt` machine, which delivers external interrupts. The level of `EnableInt` is a PLIC priority, 1 to 7. |
| TCB | Task control block, the kernel's record of one task. `tk_exd_tsk` returns the TCB to a free list, and `tk_cre_tsk` reuses it. |
| DORMANT | The specification's task state between creation (or `tk_ext_tsk`) and `tk_sta_tsk` (`TTS_DMT` in the source). |
| PC unit test | A test case in `test/host/`, written with the Unity C test framework. The `host` preset builds it for the PC with AddressSanitizer and UBSan. It runs the kernel sources on a fake machine (`test/host/fakes.h`) that cannot switch stacks, so it checks kernel state and queues but not the values a woken task returns. The [testing guide](../developer/testing.md) explains how to run these tests. |
| QEMU suite | A test program from `test/suites/` that runs the kernel on QEMU's RV32 `virt` machine. Suite `task-create` comes from `test/suites/task_create.c` and runs as the CTest `qemu-task-create`; the other names follow the same pattern. `task-context` stands for the eight `task-context-<case>` suites built from `test/suites/task_context.c`. |
| Instruction-count clock, host clock | By default (`QEMU_ICOUNT_SHIFT` 5) QEMU runs with `-icount shift=5,sleep=off`: guest time follows the number of instructions run, so every run is the same. Most suites also run once on the host clock, where guest time follows real time, as the CTest `qemu-<name>-hostclock`; the suites that read the instruction counter do not. "Both clocks" means both runs. |
| Preset | A configuration in `CMakePresets.json`. `debug` (`-O0`), `relwithdebinfo` (`-O2`) and `minsizerel` (`-Os`) build for RV32, and each has an `-lto` twin with link-time optimization. `host` builds the PC unit tests. Preset `<p>` builds in `../build/<p>`. |
| Probe | A short program written for the review, in addition to the committed tests, to observe one behavior. |

## API inventory

Each row names the implementation file, the PC unit test files, and the QEMU
suites that provide the evidence, then lists the requirements checked for the
entry. The PC unit tests show state and queues. The QEMU suites supply the rest:
the values a blocked call returns, register restoration, trap entry, and
dispatch that waits for the trap exit.

### Tasks and task synchronization

The requirements come from the
[task-management chapter](https://tron-forum.github.io/mtk3_spec_jp/utk_os_functions.html),
the [task-synchronization chapter](https://tron-forum.github.io/mtk3_spec_jp/task_dependent_synchronization_functions.html)
and the [scheduling rules](https://tron-forum.github.io/mtk3_spec_jp/task_states_and_scheduling_rules.html).

| Entry | Assessment | Source and checked evidence |
| --- | --- | --- |
| `tk_cre_tsk` | Checked / Defined choice | `kernel/task.c`, `test/host/test_task.c`, `task-create`. Allocation, ID and priority ranges, stack bounds and alignment, attributes, exhaustion, and that a refused call leaves the kernel unchanged. See the protection-level [defined choice](#defined-choices) and [assembly entry contract](assembly-entry.md). |
| `tk_sta_tsk` | Checked | `kernel/task.c`, `task-restart`. Starts only a DORMANT task, rebuilds the argument and the initial frame, clears queued wakeup requests, inserts at the tail of the ready queue, and refuses an active task. |
| `tk_ext_tsk` | Checked (Difference corrected 2026-11-10) / Defined choice | `kernel/task.c`, `task-restart`, `task-context`, `interrupt`. Moves the task to DORMANT, removes it from its queue, switches away and discards the ending context, and does not return from an invalid context. The race in which an ended task could resume its old run is fixed ([history](spec-conformance-history.md#an-ended-task-could-resume-its-old-run)). |
| `tk_exd_tsk` | Checked (Difference corrected 2026-11-10) / Defined choice | `kernel/task.c`, `task-restart`, `task-context`, `interrupt`. Deletes the task, returns its TCB to the free list for reuse, switches away and discards the ending context, and does not return from an invalid context. The same race with a task created in the freed TCB is fixed ([history](spec-conformance-history.md#an-ended-task-could-resume-its-old-run)). |
| `tk_dly_tsk` | Checked (Difference corrected 2026-11-10) | `kernel/timer.c`, `test/host/test_timeout_ticks.c`, `test/host/test_wait_context.c`, `task-timer`, `wait-context`. The full unsigned `RELTIM` range, release causes, elapsed ticks, and `E_CTX` where the call cannot wait. A zero delay returns `E_OK` and changes nothing ([history](spec-conformance-history.md#a-zero-delay-yielded-the-processor)). |
| `tk_slp_tsk` | Checked | `kernel/timer.c`, `test/host/test_wait.c`, `task-timer`, `wait-context`. A queued wakeup request is used up instead of sleeping; polling, finite and endless waits; timeout and release causes; refusal where the call cannot wait. |
| `tk_rel_wai` | Checked (Difference corrected 2026-11-10) | `kernel/task.c`, `test/host/test_wait.c`, `semaphore-wait-release`. State errors, unlinking from the timer and the object queue, `E_RLWAI`, queued wakeup requests kept, and ready-queue scheduling. An ISR can release the task it interrupted once that task is waiting ([history](spec-conformance-history.md#an-isr-could-not-release-the-task-it-interrupted)). |
| `tk_wup_tsk` | Checked | `kernel/task.c`, `test/host/test_task.c`, `task-timer`, `wakeup-context`. Refuses the calling task itself, accepts a request from an ISR for the task it interrupted, releases only a `tk_slp_tsk` wait, and checks the queued-request limit (`TK_WAKEUP_MAXCNT`), overflow and scheduling. |

An additional PC queue check confirmed the order of equal-priority tasks
after a start or a wakeup. The QEMU suites cover the restart frames and the
values a task returns from a wait. Combinations with the SUSPENDED state
depend on the suspension APIs, which are unimplemented (see
[API support status](API_Support_Status.md)).

### Synchronization and communication

The normative contracts are in the
[synchronization and communication chapter](https://tron-forum.github.io/mtk3_spec_jp/synchronzation_and_communication_functions.html).

| Entry | Assessment | Source and checked evidence |
| --- | --- | --- |
| `tk_cre_sem` | Checked | `kernel/semaphore.c`, `test/host/test_semaphore.c`, `semaphore`. Attribute and count boundaries, the profile maximum (`TK_SEMAPHORE_MAXCNT`), initialization and exhaustion. |
| `tk_wai_sem` | Checked / Defined choice | `kernel/semaphore.c`, `test/host/test_semaphore.c`, `semaphore`. The four attribute combinations (`TA_TFIFO` or `TA_TPRI`, with `TA_FIRST` or `TA_CNT`), polling, finite and endless waits, priorities, whether the count suffices, and cleanup. |
| `tk_sig_sem` | Checked | `kernel/semaphore.c`, `test/host/test_semaphore.c`, `semaphore`. Overflow detected before any effect, allocation to waiters, count updates and dispatch. |
| `tk_del_sem` | Checked | `kernel/semaphore.c`, `test/host/test_wait.c`, `semaphore-wait-release`. Waiters released with `E_DLT`, timer and queue cleanup, reuse and dispatch. |
| `tk_cre_flg` | Checked | `kernel/event_flag.c`, `test/host/test_event_flag.c`, `event-flag`. Attributes, initial pattern, allocation, and refusal of optional features. |
| `tk_wai_flg` | Checked | `kernel/event_flag.c`, `test/host/test_event_flag.c`, `event-flag`. AND and OR waits, the pattern returned before clearing, `TWF_CLR` and `TWF_BITCLR`, single and multiple waiters, order, timeout, and the pattern a resumed task receives. |
| `tk_set_flg` | Checked | `kernel/event_flag.c`, `test/host/test_event_flag.c`, `event-flag`. Pattern update, release in order, and clearing between waiters. |
| `tk_clr_flg` | Checked | `kernel/event_flag.c`, `test/host/test_event_flag.c`. Mask clearing, with waiters and scheduling left unchanged. |
| `tk_cre_mbx` | Checked | `kernel/mailbox.c`, `test/host/test_mailbox.c`, `mailbox`. Attributes, packet initialization, allocation, and refusal of optional features. |
| `tk_del_mbx` | Checked | `kernel/mailbox.c`, `test/host/test_mailbox.c`, `mailbox`. Release of waiting receivers, queued messages returned to the application, reuse and dispatch. |
| `tk_snd_mbx` | Checked | `kernel/mailbox.c`, `test/host/test_mailbox.c`, `mailbox`. Direct delivery to a waiting receiver, FIFO and priority insertion, the positive priority range, and stable order for equal priorities. |
| `tk_rcv_mbx` | Checked | `kernel/mailbox.c`, `test/host/test_mailbox.c`, `mailbox`. Removal of the first message, polling, finite and endless waits, and the return paths for timeout, deletion and success. |

Supplemental probes passed for a semaphore count of 32767, mailbox priority
`INT_MAX`, equal message priorities, event-flag bit 31 and the pattern
returned before clearing. RV32 size and member-order assertions passed for the
packets `T_CSEM`, `T_CFLG`, `T_CMBX`, `T_MSG` and `T_MSG_PRI`.

### Time management

The references are the
[time-management chapter](https://tron-forum.github.io/mtk3_spec_jp/time_management_functions.html)
and the
[service-profile definitions](https://tron-forum.github.io/mtk3_spec_jp/service_profile_definitions.html).

| Entry | Assessment | Source and checked evidence |
| --- | --- | --- |
| `tk_set_utc` | Checked (Difference corrected 2026-11-11) / Defined choice | `kernel/timer.c`, `test/host/test_system_time.c`, `system-time`. Millisecond UTC values kept with their remainder, both words and the range 0 to 2^63 − 1, `E_PAR` for a NULL pointer or a negative `hi` before `E_CTX`, refusal in an ISR or without a current task with nothing changed, and timed waits unaffected by a set ([history](spec-conformance-history.md#no-time-representation-was-enabled)). |
| `tk_get_utc` | Checked (Difference corrected 2026-11-11) / Defined choice | `kernel/timer.c`, `test/host/test_system_time.c`, `system-time`. The clock starts at 0 with the timer and advances 10 ms per tick, counts ticks whose interrupt is still pending exactly once, saturates at 2^63 − 1, and refuses an ISR or a missing current task without writing the output ([history](spec-conformance-history.md#no-time-representation-was-enabled)). |

The refusal in an ISR, the refusal of negative times and the saturation at
2^63 − 1 ms are listed under
[defined choices](#defined-choices).

### Interrupts and I/O

The references are the
[OS interrupt chapter](https://tron-forum.github.io/mtk3_spec_jp/interrupt_management_functions_os.html),
the [SM interrupt chapter](https://tron-forum.github.io/mtk3_spec_jp/interrupt_management_functions_sm.html)
and the [I/O access chapter](https://tron-forum.github.io/mtk3_spec_jp/io_port_management_functions.html).

| Entry | Assessment | Source and checked evidence |
| --- | --- | --- |
| `tk_def_int` | Checked / Defined choice | `kernel/intctl.c`, `test/host/test_intctl.c`, `interrupt`. The `T_DINT` packet layout, a `TA_HLNG` C callback or a `TA_ASM` raw entry, replacement and removal, number ranges, and context handling. |
| `tk_ret_int` | Checked / Defined choice | `kernel/intctl.c`, `kernel/asm/rv32/start.S`, `interrupt`. Matching ASM entry/depth/stack and disabled MIE required, fatal invalid context, exactly-once completion and outermost delayed dispatch. The [assembly contract](assembly-entry.md) also defines the direct-return alternative. |
| `EnableInt` | Checked (Difference corrected 2026-11-10) / Defined choice | `kernel/intctl.c`, `test/host/test_intctl.c`, `interrupt`. Setting the priority, enabling the source, and delivery of an interrupt that was already pending. A consumer written to the service profile compiles and sets the level ([history](spec-conformance-history.md#the-interrupt-profile-flags-disagreed-with-the-exported-calls)). |
| `DisableInt` | Checked / Defined choice | `kernel/intctl.c`, `test/host/test_intctl.c`, `interrupt`. Masking a source, a handler disabling its own source, and completion of the source. `TK_SUPPORT_INTCTRL` now announces it. |
| `CheckInt` | Checked / Defined choice | `kernel/intctl.c`, `test/host/test_pending_int.c`, `interrupt`. Pending snapshots with disabled sources, repeated reads, and a claim observed from C and assembly handlers. |
| `ClearInt` | Checked / Defined choice | `kernel/intctl.c`, `test/host/test_pending_int.c`, `interrupt`. One target-only claim and completion, full enable-word isolation, priority-zero handling, state restoration and active-claim preservation. |
| `EndOfInt` | Checked / Defined choice | `kernel/intctl.c`, `test/host/test_asm_interrupt.c`, `interrupt`. Newest matching active context, synchronous EOI once, repeated/inactive calls, source and CPU state preservation, and coordination with C and ASM exits. |
| `DI` | Checked | `include/sys/interrupt.h`, `wait-context`, an RV32 probe. Saves and clears MIE in one instruction and assigns the caller's variable. |
| `EI` | Checked (Difference corrected 2026-11-09) | `include/sys/interrupt.h`, `interrupt-state` (`test/suites/interrupt_state.c`), `test/host/test_interrupt_state.c`. Restores the saved MIE state whatever MIE is when `EI` runs ([history](spec-conformance-history.md#ei-did-not-restore-a-saved-disabled-state)). |
| `isDI` | Checked | `include/sys/interrupt.h`, `wait-context`, an RV32 probe. Tests the MIE bit of the saved value. |
| `in_b` | Checked | `include/sys/ioport.h`, generated RV32 assembly. Volatile byte load. |
| `in_h` | Checked | `include/sys/ioport.h`, generated RV32 assembly. Volatile halfword load. |
| `in_w` | Checked | `include/sys/ioport.h`, generated RV32 assembly. Volatile word load. |
| `in_d` | Checked / Defined choice | `include/sys/ioport.h`, generated RV32 assembly. Two 32-bit volatile reads, lower half first. |
| `out_b` | Checked | `include/sys/ioport.h`, generated RV32 assembly. Volatile byte store. |
| `out_h` | Checked | `include/sys/ioport.h`, generated RV32 assembly. Volatile halfword store. |
| `out_w` | Checked | `include/sys/ioport.h`, generated RV32 assembly. Volatile word store. |
| `out_d` | Checked / Defined choice | `include/sys/ioport.h`, generated RV32 assembly. Two 32-bit volatile writes, lower half first. |

RV32 compile probes passed for integer widths, the `T_DINT` member offsets 0
and 4 and its size 8, the C interrupt callback, and `TK_SPECVER == 0x0300`.
The generated I/O assembly uses volatile loads and stores of the right width,
and the split 64-bit accesses the specification permits.

## Interrupt controller profile

`TK_SUPPORT_INTCTRL` now has declarations and definitions for `EnableInt`,
`DisableInt`, `CheckInt`, `ClearInt` and `EndOfInt`. The pending query observes
one bit. ClearInt isolates one source for a single claim and completes only
its temporary claim, preserving other pending requests and existing handler
contexts. EndOfInt completes the newest matching active handler claim once.
The [interrupt guide](interrupts.md) defines these contracts and the
[ClearInt record](../developer/notes/clearint-validation.md) gives the checks.

The earlier profile-availability difference is recorded in the
[history](spec-conformance-history.md#the-interrupt-profile-flags-disagreed-with-the-exported-calls).
The specification defines these APIs in its
[SM interrupt chapter](https://tron-forum.github.io/mtk3_spec_jp/interrupt_management_functions_sm.html).

## Common public declarations

The generated `<tk/typedef.h>` publishes `SYSTIM` as a millisecond system-time
structure with signed 32-bit `W hi` at offset 0 and unsigned 32-bit `UW lo` at
offset 4. Its RV32 size is 8 bytes. It is available when `TK_SUPPORT_USEC` is
false and through the public umbrella header `<tk/tkernel.h>`.

The same header defines `LOCAL` as `static`, `EXPORT` as an empty replacement,
and `IMPORT` as `extern`. C consumers use these macros for internal definitions,
external definitions and external references. The declarations follow the
[common specification](https://tron-forum.github.io/mtk3_spec_jp/common_utk.html).

The checks in `test/public_declarations/` compile the field layout and
signedness and link two translation units to check local and external symbols.
They cover direct and umbrella includes, with `CONST` enabled and disabled.
The host executables run the value checks. RV32 CTests build the executables
to verify target compilation and final linking. Clang C++17 consumers also
compiled and linked with the C kernel in both `CONST` modes. The
[history](spec-conformance-history.md#common-public-declarations-were-unavailable)
records the original failures, the correction and its verification.

The [time-management entries](#time-management) cover the interfaces that use
`SYSTIM`.

## Defined choices

In these cases the specification leaves a choice to the implementation, and
the review found the kernel's choice permitted.

| Entry or area | Choice | Basis in the specification |
| --- | --- | --- |
| Parameter packets | Packets omit the fields that disabled service-profile features would use. RV32 layout checks passed for the fields kept. The fixed-size link header of a mailbox message follows the rule for implementation-defined message headers. | [Packet-format rules](https://tron-forum.github.io/mtk3_spec_jp/system_calls.html) |
| `tk_wai_sem` | Rejects a count above the semaphore's maximum with `E_PAR`. The reference implementation contains the same check ([its semaphore source](https://github.com/tron-forum/mtkernel_3/blob/master/kernel/tkernel/semaphore.c)). | [Reference-code role](https://tron-forum.github.io/mtk3_spec_jp/reference_code_of_utk.html) |
| Unsupported features | Error codes for unsupported subfunctions, and the optional parameter and attribute checks, follow the adaptation rules. `tk_cre_tsk` returns `E_RSATR` when `TA_USERBUF` is omitted while automatic buffer allocation is disabled (`TK_SUPPORT_AUTOBUF` false); the review kept this as an allowed choice. | [Error-code adaptation rules](https://tron-forum.github.io/mtk3_spec_jp/list_of_error_codes.html) |
| Protection levels (`tk_cre_tsk`) | The kernel maps the protection-level attributes `TA_RNG0` to `TA_RNG3` to its single privileged execution environment. All four passed the creation probe on the PC. The other context restrictions on creation and definition calls were assessed against the common context rules. | [Task-management rules](https://tron-forum.github.io/mtk3_spec_jp/utk_os_functions.html), [common call rules](https://tron-forum.github.io/mtk3_spec_jp/system_calls.html) |
| `tk_ext_tsk`, `tk_exd_tsk` | A call from an invalid context does not return. | [Task-management chapter](https://tron-forum.github.io/mtk3_spec_jp/utk_os_functions.html) |
| `tk_def_int`, `EnableInt`, `DisableInt` | [Interrupts](interrupts.md) describes how these calls behave where the specification leaves the behavior open, for example for a bad interrupt number or level. | [SM interrupt chapter](https://tron-forum.github.io/mtk3_spec_jp/interrupt_management_functions_sm.html) |
| `in_d`, `out_d` | A 64-bit access is split into two 32-bit volatile accesses, lower half first. | [I/O access chapter](https://tron-forum.github.io/mtk3_spec_jp/io_port_management_functions.html) |
| `tk_set_utc`, `tk_get_utc` | A call from an ISR, or before the first task starts, returns `E_CTX` after the argument checks and changes nothing. `tk_set_utc` returns `E_PAR` for a negative time, and the clock stops at 2^63 − 1 ms (see [kernel behavior](behavior.md#system-time-utc)). | The [time-management chapter](https://tron-forum.github.io/mtk3_spec_jp/time_management_functions.html) marks both calls as not available in the task-independent portion, and the specification's notes on the API description format make detecting `E_CTX` implementation-dependent. `SYSTIM` is a signed 64-bit millisecond count; the `E_PAR` of `tk_set_utc` covers an invalid time without defining which times are invalid, and the specification does not say what follows the largest value. |

## Evidence gaps

### Interrupt nesting beyond two levels

Target evidence now covers external interrupts nested three to seven levels
deep on QEMU `virt`, with the two software-raised sources alternating (see the
[deep nesting record](../developer/notes/deep-interrupt-nesting-validation.md)).
Chains with more than two distinct active sources, and the stack use of
handlers other than the tested ones, remain outside the evidence.

### Outside the collected evidence

Bus behavior on a real device and operation with more than one hart are outside
the evidence collected here.

## Reproducing the checks

Install the tools listed in the [README](../../README.md) and work in a clean,
disposable checkout. The [testing guide](../developer/testing.md) explains the
presets and test commands in more detail.

### Run the tests

1. Build and run the PC unit tests:

   ```shell
   cmake --preset host
   cmake --build --preset host
   ctest --preset host
   ```

2. Build the `debug` preset and run all of its CTests:

   ```shell
   cmake --preset debug
   cmake --build --preset debug
   ctest --test-dir ../build/debug --output-on-failure --no-tests=error
   ```

3. For focused target checks, run only the suites the inventory names:
   `task-create`, `task-restart`, `task-timer`, `wakeup-context`, `semaphore`,
   `event-flag`, `mailbox`, `interrupt`, `task-context`, `wait-context` and
   `system-time`.
   Pass `ctest -R` a regular expression that matches their CTest names
   (`qemu-<name>`). With the default instruction-count clock, each suite also
   has a `qemu-<name>-hostclock` test, which the same expression matches.

### Compile the consumer snippets

To repeat a compile probe from the [review history](spec-conformance-history.md),
configure the `debug` preset, save the snippet as `consumer.c`, and run from
the repository root.

```shell
clang --target=riscv32-unknown-none-elf -march=rv32imac -mabi=ilp32 -ffreestanding -nostdinc -Iinclude -I../build/debug/include -fsyntax-only consumer.c
```

`-I../build/debug/include` finds the generated `tk/typedef.h`. Each snippet
reports the diagnostic its section describes. The interrupt-profile consumer
in the [history](spec-conformance-history.md#the-interrupt-profile-flags-disagreed-with-the-exported-calls)
now compiles. The history also has the standalone probe that reproduced the
original `EI` difference on QEMU.

### Recorded environment

The recorded runs used Windows x64, clang 21.1.8, CMake 4.3.1-msvc1, Unity
2.6.1 and a QEMU 9.2.50 development build.
