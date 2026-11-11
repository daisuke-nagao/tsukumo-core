# Specification conformance review: fixes and test runs

This document records the differences from the μT-Kernel 3.0 specification
that the [specification conformance review](spec-conformance-review.md) found
and that the kernel has since fixed, and the test runs made at each step. Read
it to learn why the kernel behaves as it does in these cases or to find the
regression test for a fix. The review describes the current state, and its
[terms section](spec-conformance-review.md#terms-used-in-this-review) defines
the vocabulary used here (ISR, MIE, service profile, PC unit test, QEMU suite,
both clocks).

## Fixed differences

| Fixed on | What was wrong | APIs | Regression test |
| --- | --- | --- | --- |
| 2026-11-09 | [`EI` did not restore a saved disabled state](#ei-did-not-restore-a-saved-disabled-state) | `EI` | `interrupt-state` suite, `test/host/test_interrupt_state.c` |
| 2026-11-10 | [An ended task could resume its old run](#an-ended-task-could-resume-its-old-run) | `tk_ext_tsk`, `tk_exd_tsk` | Two cases in the `interrupt` suite |
| 2026-11-10 | [An ISR could not release the task it interrupted](#an-isr-could-not-release-the-task-it-interrupted) | `tk_rel_wai` | PC unit test in `test/host/test_wait.c` |
| 2026-11-10 | [A zero delay yielded the processor](#a-zero-delay-yielded-the-processor) | `tk_dly_tsk` | PC unit test in `test/host/test_wait_context.c` |
| 2026-11-10, in part | [The interrupt profile flags disagreed with the exported calls](#the-interrupt-profile-flags-disagreed-with-the-exported-calls) | `EnableInt`, `DisableInt` | PC unit test in `test/host/test_intctl.c` |
| 2026-11-11 | [Common public declarations were unavailable](#common-public-declarations-were-unavailable) | `SYSTIM`, `LOCAL`, `EXPORT`, `IMPORT` | Public consumer compile and link checks |
| 2026-11-11 | [No time representation was enabled](#no-time-representation-was-enabled) | `tk_set_utc`, `tk_get_utc`, `TK_SUPPORT_UTC` | PC unit tests in `test/host/test_system_time.c`, `system-time` suite, public consumer checks |

The initial review on 2026-11-08 (JST) found the `EI` and interrupt-profile
problems. After the `EI` fix, a second review of the kernel found the task-exit
race, the `tk_rel_wai` refusal and the zero-delay yield, and reported the
profile problem again. All four were fixed on 2026-11-10.

### EI did not restore a saved disabled state

Before the fix, `EI()` in `include/sys/interrupt.h` restored a nonzero saved
value with `csrs`. That instruction only sets the selected bits, so when the
saved MIE was 0 and MIE was currently 1, MIE stayed 1. The
[EI contract](https://tron-forum.github.io/mtk3_spec_jp/interrupt_management_functions_sm.html)
requires `EI` to restore the saved state.

The initial review found the problem with an RV32 probe. The probe masked the
other interrupt sources, so it observed the CSR state alone, and ran this
sequence:

```c
UINT saved, after;
__asm__ volatile("csrci mstatus, 8" ::: "memory");
DI(saved);
EI(0);
EI(saved);
__asm__ volatile("csrr %0, mstatus" : "=r"(after));
/* isDI(saved) is true; after & MSTATUS_MIE is 8, expected 0. */
```

The recorded output was `saved_disabled=1` followed by
`after_EI_saved_mie=1 expected=0`, at `-O0` on both QEMU clocks and at `-O2` on
the host clock. MIE ended enabled although the saved state was disabled. The
probe established the restore error; it did not look for damage to tasks or
queues that might follow. The existing tests had missed the problem: their
nested `DI`/`EI` case kept MIE disabled until the restore, so it passed despite
the defect.

The fix had to restore saved-enabled and saved-disabled values after either
current MIE state, handle ordinary nested `DI`/`EI` and `EI(0)`, and keep the
other CSR bits. Since 2026-11-09, `EI` restores the saved MIE bit, or sets MIE
for `EI(0)`, and leaves the other CSR bits unchanged.

The RV32 regression suite `interrupt-state` (`test/suites/interrupt_state.c`)
covers every combination of saved and current MIE, `EI(0)`, ordinary nested
use, and a pending software interrupt while a disabled state is restored. The
PC regression test is `test/host/test_interrupt_state.c`. The timer, mailbox
and interrupt-stack measurements were updated after the fix; see the
[timer delay budget](../developer/notes/timer-delay-budget.md), the
[mailbox budget](../developer/notes/mailbox-di-budget.md) and
[interrupts](interrupts.md).

#### Reproduce the original observation

Save this standalone source as `ei-probe.c` and the linker script as
`ei-probe.ld` in a disposable checkout. The probe prints through the UART at
`0x10000000` and stops QEMU by writing to the `virt` machine's test-finisher
device at `0x100000`. Its entry point and stack come from the linker script.

```c
#include <tk/tkernel.h>
__asm__(".section .text.boot,\"ax\"\n.global _start\n_start:\n"
        "la sp, probe_stack_end\ncsrw mie, zero\ncall probe\n"
        "1: wfi\nj 1b\n.section .bss\n.balign 16\n"
        "probe_stack:\n.space 4096\nprobe_stack_end:\n");
static void print(const char *s) {
  while (*s) out_b(0x10000000, (UB)*s++);
}
void probe(void) {
  UINT saved, after;
  __asm__ volatile("csrci mstatus, 8" ::: "memory");
  DI(saved);
  EI(0);
  EI(saved);
  __asm__ volatile("csrr %0, mstatus" : "=r"(after));
  print(isDI(saved) ? "saved_disabled=1\n" : "saved_disabled=0\n");
  print((after & MSTATUS_MIE) ? "restored_mie=1\n" : "restored_mie=0\n");
  out_w(0x100000, 0x5555);
  for (;;) __asm__ volatile("wfi");
}
```

```ld
OUTPUT_ARCH("riscv")
ENTRY(_start)
SECTIONS {
  . = 0x80000000;
  .text : { *(.text.boot) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss (NOLOAD) : { *(.bss .bss.*) }
}
```

After configuring the `debug` preset, build the probe from the repository root
and run it on the host clock and on the instruction-count clock:

```shell
clang --target=riscv32-unknown-none-elf -march=rv32imac -mabi=ilp32 -mcmodel=medany -mno-relax -ffreestanding -nostdlib -nostdinc -O0 -Iinclude -I../build/debug/include -fuse-ld=lld -Wl,-T,ei-probe.ld ei-probe.c -o ei-probe.elf
qemu-system-riscv32 -machine virt -bios none -nographic -kernel ei-probe.elf
qemu-system-riscv32 -machine virt -bios none -nographic -kernel ei-probe.elf -icount shift=5,sleep=off
```

The expected restored MIE is zero. The original implementation printed
`restored_mie=1`; the regression suite now checks that MIE is restored to zero.
The probe always exits with status zero, so read the printed state, not the
exit status.

### An ended task could resume its old run

Before the fix, `tk_ext_tsk()` made the task DORMANT, requested a switch with
the software interrupt and enabled interrupts. A pending external interrupt
outranks the software interrupt, so its handler ran while the ended task was
still current. If the handler started the task again with `tk_sta_tsk()`, the
switch at the trap exit saved the stack pointer of the ended run over the new
start frame, and the task went on with its old run instead of starting at its
entry. `tk_exd_tsk()` had the same window for a task that the handler created
in the freed TCB. Skipping the context save for a dormant task would not have
helped, because after the restart the task is ready.

The second review found this race. The initial review had rated both calls
Checked.

Since 2026-11-10, both calls switch to the next task with interrupts still
disabled and discard the ending context. `tkmc_force_dispatch()` in
`kernel/task.c` makes the switch through `__launch_task` in
`kernel/asm/rv32/start.S`, which resumes a task with `mret` instead of a trap
and also starts the first task.

The `interrupt` suite reproduces the window without kernel hooks. The task
masks the software, timer and external interrupts in `mie` until the UART
interrupt is pending, then ends, and the handler restarts it or creates a task
in its TCB. The cases are
`a_task_restarted_by_an_isr_right_after_tk_ext_tsk_starts_anew` and
`a_task_created_by_an_isr_in_the_tcb_tk_exd_tsk_freed_starts_anew`. Before the
fix both failed on both QEMU clocks, because the ended run went on; they now
pass. On QEMU without AIA (the RISC-V Advanced Interrupt Architecture), the
hart may not take a pending external interrupt ahead of a software one, so the
window may not open there by itself. A hart that follows the specified priority
order opens it.

### An ISR could not release the task it interrupted

Before the fix, `tk_rel_wai()` returned `E_OBJ` for the current task, whoever
the caller was. A wait call makes its task waiting and requests the switch
before interrupts come back on, so an external interrupt taken in between finds
that task current and already waiting. The specification lets a handler call
`tk_rel_wai()` and, for a waiting task, specifies `E_OK` and the end of the
wait. The handler got `E_OBJ` instead, and a task in an endless wait stayed
blocked until something else released it.

The second review found this problem.

Since 2026-11-10, `tk_rel_wai()` follows the rule `tk_wup_tsk()` already used:
only a task that names itself is refused. An ISR that names the running task
gets `E_OBJ` only if that task is not waiting. The release chooses the next task
again, so the switch at the trap exit goes to the released task if it outranks
the task the wait call chose.

The PC unit test
`test_an_isr_releases_the_task_it_interrupted_right_after_it_began_to_wait`
in `test/host/test_wait.c` failed with `E_OBJ` before the fix. It calls from a
handler with interrupts off and from one that turned them on, because a handler
is an ISR whatever MIE is.

### A zero delay yielded the processor

Before the fix, `tk_dly_tsk(0)` gave the processor to the ready tasks of the
same priority. The initial review had rated this yield a defined choice.

The second review reported it as a difference. The reference implementation
waits only for a positive delay and returns at once for zero; here, the
reference implementation means the TRON Forum's reference implementation of
μT-Kernel 3.0, the `mtkernel_3` source released with the specification. The
specification delegates detailed behavior to the reference implementation (see
the `tk_wai_sem` item in the review's
[defined choices](spec-conformance-review.md#defined-choices)). This statement
about the reference implementation was not checked against its source for the
fix, because no external access was granted. It rests on the second review and
on prior knowledge of `mtkernel_3`.

Since 2026-11-10, a zero delay returns `E_OK` after the same context check as
before, without changing any task, queue or switch request. The kernel offers
no public yield in its place (`tk_rot_rdq()` is not implemented). The
`idle-halt` suite now yields with the kernel's internal `tkmc_yield()`.

The PC unit test
`test_a_delay_of_zero_with_interrupts_on_changes_nothing_and_leaves_them_on`
in `test/host/test_wait_context.c` compares the kernel before and after the
call, with a ready task of the same priority. It failed before the fix.

### The interrupt profile flags disagreed with the exported calls

Before the fix, `include/sys/profile.h` declared `TK_SUPPORT_INTCTRL` and
`TK_HAS_ENAINTLEVEL` false. The public header exported a two-argument
`EnableInt`, and its implementation sets the PLIC priority. A consumer that
chose the arity by `TK_HAS_ENAINTLEVEL` failed to compile with "expected 2,
have 1", and a consumer that checked `TK_SUPPORT_INTCTRL` left the capability
out. See the specification's
[EnableInt and service-profile section](https://tron-forum.github.io/mtk3_spec_jp/interrupt_management_functions_sm.html).

```c
#include <tk/tkernel.h>
void driver_enable(void) {
#if TK_HAS_ENAINTLEVEL
  EnableInt(10, 1);
#else
  EnableInt(10);
#endif
}
```

The initial review found the problem and set the conditions for a fix: the
profile flags, the exported declarations and the implemented controller
capabilities agree, a consumer written to the profile compiles, and the
hardware-dependent choices of source and priority are documented. The second
review reported the problem again.

Since 2026-11-10, `TK_SUPPORT_INTCTRL` and `TK_HAS_ENAINTLEVEL` are true. They
match the exported `EnableInt(intno, level)`, whose level is the PLIC priority
(1 to 7, see [interrupts](interrupts.md)), and `DisableInt(intno)`. The snippet
above now compiles and takes the two-argument branch. The fix is partial:
`ClearInt`, `EndOfInt` and `CheckInt` are still missing, an
[open difference](spec-conformance-review.md#tk_support_intctrl-also-covers-calls-the-kernel-does-not-provide)
in the review.

The PC unit test `test_a_driver_written_to_the_profile_enables_a_source_at_its_level`
in `test/host/test_intctl.c` follows the same pattern and checks the priority it
sets. With the previous flags it did not compile.

### Common public declarations were unavailable

The original public-header consumers failed to compile because the generated
`tk/typedef.h` hid `SYSTIM` inside `#if 0` and a microsecond-support condition,
and omitted `LOCAL`, `EXPORT` and `IMPORT`. Separate RV32 snippets reproduced
the unknown type and all three unknown declaration macros before the fix.

On 2026-11-11, the millisecond `SYSTIM` structure moved into the unconditional
common declarations. The header also gained the specified linkage macros.
The field types and the availability conditions follow the
[common specification](https://tron-forum.github.io/mtk3_spec_jp/common_utk.html).

The checks in `test/public_declarations/` use the generated header through
both public include forms. They assert the type and tag identity, field
widths, signedness, offsets and total size, and link two translation units.
The host runs the linked programs. RV32 performs final executable links.
The umbrella variants check `CONST` both enabled and disabled.
The RV32 link checks register with the other RV32 tests after QEMU discovery.
Their executables still build when QEMU is unavailable. A configuration with
QEMU disabled built all three executables, and the documented CTest command
with `--no-tests=error` reported no tests instead of passing only link checks.

The new fixtures failed against the original header. Isolated wrong headers
also failed for swapped fields, either field's wrong signedness, an external
`LOCAL`, an internal `EXPORT`, and a defining `IMPORT`. The corrected header
passed on the host and RV32. The fixtures use `-fno-common` so an unintended
external variable definition causes a duplicate-symbol link failure.

Existing RV32 packet layouts matched the baseline in both `CONST` modes.
Clang C++17 consumers compiled and linked against the actual C kernel in both
modes. The correction uses the existing type-generation path. The time
interfaces and their profile were then corrected as described in
[the next section](#no-time-representation-was-enabled).

### No time representation was enabled

Before the fix, `include/sys/profile.h` declared `TK_SUPPORT_UTC` and
`TK_SUPPORT_TRONTIME` both false, and the kernel provided no call that sets or
reads the system time. The
[service-profile definitions](https://tron-forum.github.io/mtk3_spec_jp/service_profile_definitions.html)
require at least one of the two. This consumer assertion failed with `0 || 0`:

```c
#include <tk/tkernel.h>
_Static_assert(TK_SUPPORT_UTC || TK_SUPPORT_TRONTIME,
               "one time representation must be enabled");
```

Since 2026-11-11, the kernel provides `tk_set_utc` and `tk_get_utc` in
milliseconds, as the
[time-management chapter](https://tron-forum.github.io/mtk3_spec_jp/time_management_functions.html)
defines them, and `TK_SUPPORT_UTC` is true. `TK_SUPPORT_TRONTIME` and
`TK_SUPPORT_USEC` stay false. The clock starts at 0 with the kernel timer,
advances 10 ms per tick, keeps the millisecond remainder of a set value, and
saturates at 2^63 − 1 ms. It is kept apart from the tick count that timed waits
use, so a set moves no wait and no timer compare value. The
[kernel behavior](behavior.md#system-time-utc) page states the rules.

Public consumer probes with the original headers failed because the UTC calls
were undeclared and the time-profile assertion was false. The tests in
`test/host/test_system_time.c` and the `system-time` suite cover the provided
APIs. Fourteen isolated faults in the clock each made at least one PC unit
test fail: a set that moves the timer compare value,
a set or a read that counts pending ticks twice or consumes them, the tick unit,
the word assembly and sign extension of `lo`, the saturation check, the tick
count added after the wait loop, the order of `E_PAR` and `E_CTX`, an output
written before the context check, interrupts not restored, a refused
interrupts-disabled task, and a missing reset at timer start. Clang C++17
consumers compiled and linked both calls against the C kernel in both `CONST`
modes.

## Test runs by date

Each row is the recorded result of one run. PC unit tests ran with
AddressSanitizer and UBSan. The six presets are `debug`, `relwithdebinfo`,
`minsizerel` and their `-lto` twins.

| Date | Kernel state | PC unit tests | RV32 CTests | Other checks |
| --- | --- | --- | --- | --- |
| 2026-11-08 | Before any fix | 128 passed | `debug`: all 82 passed, 33 of them on the host clock | Supplemental PC probes passed: three task cases and four synchronization cases. RV32 compile probes checked integer widths, packet member order and the interrupt-handler interface. The RV32 probe reproduced the `EI` difference on both clocks, and at `-O2` on the host clock. |
| 2026-11-09 | After the `EI` fix | 135 passed | `debug`: all 84 passed | `interrupt-state` passed on both clocks in Debug, `-O2` and MinSizeRel. `interrupt` passed on both clocks in MinSizeRel. `timer-cost` and `mailbox-cost` passed on the instruction-count clock in MinSizeRel; their measurements are in the [timer delay budget](../developer/notes/timer-delay-budget.md) and the [mailbox budget](../developer/notes/mailbox-di-budget.md). |
| 2026-11-10 | After the task-exit fix | 135 passed | All 84 passed in each of the six presets | The two new `interrupt` cases failed on the previous kernel with `debug`, `minsizerel` and `relwithdebinfo-lto`, on both clocks. |
| 2026-11-10 | After the `tk_rel_wai` fix | 136 passed | Not recorded | |
| 2026-11-10 | After the zero-delay fix | 136 passed | All 84 passed in each of the six presets | |
| 2026-11-10 | After the profile fix | 137 passed | `debug`: all 84 passed | |
| 2026-11-11 | After the common-declaration fix | 137 passed, plus three host consumer checks | All 88 passed in Debug, including three compile/link checks | Original-header and six wrong-header cases detected on both targets. Existing RV32 packet layouts matched the baseline. Clang C++17 consumers linked against the C kernel in both `CONST` modes. |
| 2026-11-11 | After the time-representation fix | 162 passed, plus three host consumer checks | All 90 passed in each of the six presets | `timer-cost`, `timer-cost-max` and `mailbox-cost` passed in all six presets. Fourteen isolated clock faults each failed a PC unit test. Clang C++17 consumers linked both time calls against the C kernel in both `CONST` modes. |

The `timer-cost-max` suite raised the Debug count to 85 after the earlier
2026-11-10 runs. A full validation then passed all 137 PC unit tests and all
85 RV32 CTests in each of the six presets. The common-declaration checks add
three compile/link CTests to Debug, giving the 88 tests in the new row.
The new declaration checks were validated with the host and Debug presets.
