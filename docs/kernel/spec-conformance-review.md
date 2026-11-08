# Implemented kernel contracts and specification review

Reviewed on 2026-11-08 (JST), against the official Japanese
[muT-Kernel 3.0 specification Ver.3.00.01](https://tron-forum.github.io/mtk3_spec_jp/index.html).
The implementation under review is RV32 `rv32imac`/`ilp32`, single hart,
with the service profiles in `include/sys/profile.h`.

## Coverage and evidence

The inventory contains 34 implemented entries from
[API Support Status](API_Support_Status.md), cross-checked against public
declarations and definitions. Each row below describes the requirements
checked for that entry. Shared declaration and profile findings apply across
the inventory.

The assessment labels are:

- **Checked** means the listed contracts agree with inspected source and the
  available checks in this configuration.
- **Difference** means a specific specification discrepancy was reproduced.
- **Defined choice** means the specification permits the implementation choice.
- **Evidence gap** means the listed requirement needs further verification.

The baseline host build passed all 128 cases with AddressSanitizer and UBSan.
The RV32 Debug build passed all 82 CTests, including 33 host-clock tests.
Supplemental host probes passed three task cases and four synchronization
cases. RV32 compile probes checked integer widths, packet member order and
the interrupt-handler interface. Actual RV32 execution reproduced the EI
restoration discrepancy on both clocks, also at `-O2` on the host clock.

The host fake machine observes state and queues. Target suites provide the
blocked-call return values, register restoration, trap entry and delayed
dispatch evidence. Detailed observation logs and experimental sources are
kept under `.cache/conformance-task`, `.cache/conformance-sync`,
`.cache/conformance-lowlevel` and `.cache/rtos-project-sync`.

## API inventory

### Tasks and task synchronization

Requirements were compared with the official
[task-management chapter](https://tron-forum.github.io/mtk3_spec_jp/utk_os_functions.html),
[task-synchronization chapter](https://tron-forum.github.io/mtk3_spec_jp/task_dependent_synchronization_functions.html)
and [scheduling rules](https://tron-forum.github.io/mtk3_spec_jp/task_states_and_scheduling_rules.html).

| Entry | Assessment | Source and checked evidence |
| --- | --- | --- |
| `tk_cre_tsk` | Checked / Defined choice / Evidence gap | `kernel/task.c`, `test/host/test_task.c`, `task-create`. Allocation, ID and priority ranges, stack bounds/alignment, attributes, exhaustion, refusal snapshots. Supplemental RNG and ASM observations below. |
| `tk_sta_tsk` | Checked | `kernel/task.c`, `task-restart`. DORMANT-only start, argument/frame reconstruction, wake reset, tail insertion and refusal of an active task. |
| `tk_ext_tsk` | Checked / Defined choice | `kernel/task.c`, `task-restart`, `task-context`. DORMANT transition, queue removal, dispatch and non-returning invalid-context handling. |
| `tk_exd_tsk` | Checked / Defined choice | `kernel/task.c`, `task-restart`, `task-context`. Task deletion, free-list reuse, dispatch and non-returning invalid-context handling. |
| `tk_dly_tsk` | Checked / Defined choice | `kernel/timer.c`, `test_timeout_ticks.c`, `task-timer`. Unsigned RELTIM range, release causes, elapsed ticks and context gate. Zero delay yields to ready peers. |
| `tk_slp_tsk` | Checked | `kernel/timer.c`, `test_wait.c`, `task-timer`, `wait-context`. Queued-request consumption, poll/finite/infinite waits, timeout/release causes and context refusal. |
| `tk_rel_wai` | Checked | `kernel/task.c`, `test_wait.c`, `semaphore-wait-release`. State errors, timer/object unlinking, E_RLWAI, preserved wake requests and ready-queue scheduling. |
| `tk_wup_tsk` | Checked | `kernel/task.c`, `test_task.c`, `task-timer`, `wakeup-context`. Task self-refusal, ISR notification, sleep-only release, queued-count limit, overflow and scheduling. |

Equal-priority start/wake ordering passed an additional host queue check.
Existing target tests cover restart frames and actual return from waits.
Suspended-state combinations depend on suspension APIs that are listed as
unimplemented in the API inventory.

### Synchronization and communication

The normative API contracts are in the
[synchronization chapter](https://tron-forum.github.io/mtk3_spec_jp/synchronzation_and_communication_functions.html).
The evidence below names the inspected implementation and local checks.

| Entry | Assessment | Source and checked evidence |
| --- | --- | --- |
| `tk_cre_sem` | Checked | `kernel/semaphore.c`, `test_semaphore.c`, `semaphore`. Attribute/count boundaries, profile maximum, initialization and exhaustion. |
| `tk_wai_sem` | Checked / Defined choice | `kernel/semaphore.c`, `test_semaphore.c`, `semaphore`. Four attribute combinations, poll/finite/infinite waits, priorities, count sufficiency and cleanup. |
| `tk_sig_sem` | Checked | `kernel/semaphore.c`, `test_semaphore.c`, `semaphore`. Overflow before effects, waiter allocation, count updates and dispatch. |
| `tk_del_sem` | Checked | `kernel/semaphore.c`, `test_wait.c`, `semaphore-wait-release`. E_DLT release, timer/queue cleanup, reuse and dispatch. |
| `tk_cre_flg` | Checked | `kernel/event_flag.c`, `test_event_flag.c`, `event-flag`. Attributes, initial pattern, allocation and optional-feature refusal. |
| `tk_wai_flg` | Checked | `kernel/event_flag.c`, `test_event_flag.c`, `event-flag`. AND/OR, pre-clear output, CLR/BITCLR, single/multiple waiters, order, timeout and resumed output. |
| `tk_set_flg` | Checked | `kernel/event_flag.c`, `test_event_flag.c`, `event-flag`. Pattern update, ordered release and clearing between waiters. |
| `tk_clr_flg` | Checked | `kernel/event_flag.c`, `test_event_flag.c`. Mask clearing and preserved waiters/scheduling. |
| `tk_cre_mbx` | Checked | `kernel/mailbox.c`, `test_mailbox.c`, `mailbox`. Attributes, packet initialization, allocation and optional-feature refusal. |
| `tk_del_mbx` | Checked | `kernel/mailbox.c`, `test_mailbox.c`, `mailbox`. Receiver release, queue ownership, reuse and dispatch. |
| `tk_snd_mbx` | Checked | `kernel/mailbox.c`, `test_mailbox.c`, `mailbox`. Direct delivery, FIFO/priority insertion, positive priority range and equal-priority stability. |
| `tk_rcv_mbx` | Checked | `kernel/mailbox.c`, `test_mailbox.c`, `mailbox`. First-message removal, poll/finite/infinite waits and timeout/deletion/success return paths. |

Supplemental probes passed for count 32767, mailbox priority `INT_MAX`, equal
message priorities, flag bit 31 and pre-clear output. RV32 packet size/order
assertions passed for `T_CSEM`, `T_CFLG`, `T_CMBX`, `T_MSG` and `T_MSG_PRI`.

### Interrupts and I/O

The references are the
[OS interrupt chapter](https://tron-forum.github.io/mtk3_spec_jp/interrupt_management_functions_os.html),
[SM interrupt chapter](https://tron-forum.github.io/mtk3_spec_jp/interrupt_management_functions_sm.html)
and [I/O access chapter](https://tron-forum.github.io/mtk3_spec_jp/io_port_management_functions.html).

| Entry | Assessment | Source and checked evidence |
| --- | --- | --- |
| `tk_def_int` | Checked / Defined choice | `kernel/intctl.c`, `test_intctl.c`, `interrupt`. Packet ABI, TA_HLNG callback, replacement/removal, ranges and context handling. |
| `EnableInt` | Difference / Defined choice | `kernel/intctl.c`, `test_intctl.c`, `interrupt`. Priority/source enabling and pending delivery checked. Profile-selected declaration fails the consumer probe below. |
| `DisableInt` | Checked / Defined choice | `kernel/intctl.c`, `test_intctl.c`, `interrupt`. Source masking, own-source disable and completion handling. Controller-profile declaration discrepancy is shared. |
| `DI` | Checked | `include/sys/interrupt.h`, `wait-context`, RV32 probe. Atomic MIE save/clear and caller-variable assignment. |
| `EI` | Difference | `include/sys/interrupt.h`, RV32 semantic probe below. Saved-disabled restoration fails after an intervening enable. |
| `isDI` | Checked | `include/sys/interrupt.h`, `wait-context`, RV32 probe. Tests the MIE bit of the saved token. |
| `in_b` | Checked | `include/sys/ioport.h`, generated RV32 assembly. Volatile byte load. |
| `in_h` | Checked | `include/sys/ioport.h`, generated RV32 assembly. Volatile halfword load. |
| `in_w` | Checked | `include/sys/ioport.h`, generated RV32 assembly. Volatile word load. |
| `in_d` | Checked / Defined choice | `include/sys/ioport.h`, generated RV32 assembly. Two 32-bit volatile reads, lower half first. |
| `out_b` | Checked | `include/sys/ioport.h`, generated RV32 assembly. Volatile byte store. |
| `out_h` | Checked | `include/sys/ioport.h`, generated RV32 assembly. Volatile halfword store. |
| `out_w` | Checked | `include/sys/ioport.h`, generated RV32 assembly. Volatile word store. |
| `out_d` | Checked / Defined choice | `include/sys/ioport.h`, generated RV32 assembly. Two 32-bit volatile writes, lower half first. |

## Confirmed differences

### EI cannot restore a saved disabled state

**Priority P1.** `include/sys/interrupt.h` restores a nonzero saved token with
`csrs`. That instruction sets selected bits and leaves a currently set MIE
unchanged when the saved MIE is zero. The EI contract requires restoration
of the saved state. The sequence below ends with MIE enabled on both QEMU
clocks, although the saved state is disabled. Other interrupt sources were
masked so the observation concerns the CSR state itself.
[EI contract](https://tron-forum.github.io/mtk3_spec_jp/interrupt_management_functions_sm.html)

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
`after_EI_saved_mie=1 expected=0`, at `-O0` on both clocks and `-O2` on the
host clock. This establishes the restoration error. Downstream task or queue
damage was outside the probe.

Future acceptance must cover saved-enabled and saved-disabled tokens after
both current MIE states, ordinary nested DI/EI and `EI(0)`, preserving the
other CSR bits. The regular nesting case already covered by existing tests
keeps MIE disabled until restoration, so it passes with this implementation.

### Interrupt profiles disagree with the exported interface

**Priority P2.** `include/sys/profile.h` declares `TK_SUPPORT_INTCTRL` and
`TK_HAS_ENAINTLEVEL` false. The public header exports two-argument `EnableInt`,
and its implementation selects the PLIC priority. When a consumer attempts
this exported API using the arity selected by `TK_HAS_ENAINTLEVEL`, compilation
fails with "expected 2, have 1". A consumer gated by `TK_SUPPORT_INTCTRL`
omits the capability entirely with the current declaration.
[EnableInt and service profiles](https://tron-forum.github.io/mtk3_spec_jp/interrupt_management_functions_sm.html)

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

Future acceptance must align the profile flags, exported declarations and
implemented controller capabilities. A profile-driven consumer must compile,
and hardware-dependent source/priority choices must be documented.

### Both time representations are disabled

**Priority P2.** `TK_SUPPORT_UTC` and `TK_SUPPORT_TRONTIME` are both false.
The service-profile definitions require at least one representation. This
consumer assertion fails with `0 || 0`.
[Service-profile definitions](https://tron-forum.github.io/mtk3_spec_jp/service_profile_definitions.html)

```c
#include <tk/tkernel.h>
_Static_assert(TK_SUPPORT_UTC || TK_SUPPORT_TRONTIME,
               "one time representation must be enabled");
```

Future work must select and provide a time representation, including its
corresponding interfaces and semantics, and update the profile declaration
together with that implementation.

### Shared public declarations are incomplete

**Priority P2.** `typedef.h.in` places the millisecond `SYSTIM` declaration
inside `#if 0`. The public headers also omit `LOCAL`, `EXPORT` and `IMPORT`.
Consumers using these common declarations fail compilation. Microsecond
support qualifies `SYSTIM_U`; the millisecond declaration has its own common
definition. `TA_NULL` is present and its zero-value compile check passes.
[Common data types and declarations](https://tron-forum.github.io/mtk3_spec_jp/common_utk.html)

Compile the following snippets separately to observe the missing type and
the missing linkage macros.

```c
#include <tk/tkernel.h>
SYSTIM now;
```

```c
#include <tk/tkernel.h>
LOCAL INT local_value;
EXPORT INT exported_value;
IMPORT INT imported_value;
```

Future acceptance must compile both consumers through the public umbrella
header, verify the intended linkage, and check RV32 `SYSTIM` size 8, signed
`W hi` at offset 0 and unsigned `UW lo` at offset 4, including configurations
with `TK_SUPPORT_USEC` false.

## Defined choices and evidence gaps

- Parameter packets omit fields disabled by service profiles. RV32 layout
  checks passed for the retained fields. The fixed-size mailbox link header
  follows the implementation-defined message-header rule.
  [Packet-format rules](https://tron-forum.github.io/mtk3_spec_jp/system_calls.html)
- `tk_wai_sem` rejects a count above the object's maximum with `E_PAR`.
  The official reference implementation contains that check, and the
  specification delegates detailed behavior to the reference code.
  [Reference-code role](https://tron-forum.github.io/mtk3_spec_jp/reference_code_of_utk.html),
  [official semaphore source](https://github.com/tron-forum/mtkernel_3/blob/master/kernel/tkernel/semaphore.c)
- Unsupported subfunction error choices and optional parameter/attribute
  checks follow adaptation rules. The observed `E_RSATR` for omission of
  `TA_USERBUF` with automatic allocation disabled was retained as an allowed
  choice.
  [Error-code adaptation rules](https://tron-forum.github.io/mtk3_spec_jp/list_of_error_codes.html)
- The system maps protection attributes to its single privileged execution
  environment. All four RNG attributes passed the host creation probe.
  Additional creation/definition context restrictions and zero-delay yielding
  were assessed against the common context and relative-time rules.
  [Task-management rules](https://tron-forum.github.io/mtk3_spec_jp/utk_os_functions.html),
  [common call rules](https://tron-forum.github.io/mtk3_spec_jp/system_calls.html)
- **ASM evidence gap.** `TK_SUPPORT_ASM` is false, yet a valid packet with
  `TA_ASM | TA_USERBUF` produces a positive task ID in the host probe.
  Attribute-check adaptation permits acceptance of some unsupported choices.
  Assembly entry/return compatibility still needs target evidence and an
  explicit advertised-support policy. Follow-up acceptance is a documented
  policy and a focused target test of the chosen entry/return contract.
- External interrupt nesting has target evidence for two levels. Three to
  seven levels and interrupt-stack margin remain tracked additional
  verification. Real-device bus behavior, multi-hart operation and the full
  optimization/LTO matrix are outside the evidence collected here.
- The full specification also includes always-available APIs absent from the
  implementation, including flag deletion and object-reference functions.
  Their implementation status remains visible in the API inventory. This
  review covers the implemented subset and its shared contracts.
  [API/profile availability](https://tron-forum.github.io/mtk3_spec_jp/list_of_api_and_service_profiles.html)

The ASM observation used the following packet in a host fixture initialized
with `host_reset()`. Creation returned ID 1 with `TK_SUPPORT_ASM == 0`.
The three supplemental task checks record this observation, protection
attributes and same-priority ordering.

```c
static _Alignas(16) UW stack[256];
static void entry(INT code, void *info) { (void)code; (void)info; }
T_CTSK packet = {
    .tskatr = TA_ASM | TA_USERBUF,
    .task = (FP)entry,
    .itskpri = 4,
    .stksz = sizeof stack,
    .bufptr = stack,
};
/* In the initialized fixture, tk_cre_tsk(&packet) returns a positive ID. */
```

## Reproduction

Use the tool setup in the [README](../../README.md) and a clean disposable
checkout. Configure/build `host` and `debug`, then run the host preset and
all Debug CTests. For focused target checks, select `task-create`,
`task-restart`, `task-timer`, `wakeup-context`, `semaphore`, `event-flag`,
`mailbox`, `interrupt`, `task-context` and `wait-context`. The default
instruction-count configuration registers host-clock counterparts.

For each compile-only finding, save its shown snippet as `consumer.c` and run
from the repository root after configuring Debug:

```shell
clang --target=riscv32-unknown-none-elf -march=rv32imac -mabi=ilp32 -ffreestanding -nostdinc -Iinclude -I../build/debug/include -fsyntax-only consumer.c
```

The current implementation reports the diagnostics described above. Successful
RV32 control probes checked integer widths, `T_DINT` offsets 0/4 and size 8,
the C interrupt callback and `TK_SPECVER == 0x0300`. Generated I/O assembly
used width-appropriate volatile loads/stores and the permitted split 64-bit
accesses.

For the EI observation, save the following standalone source as `ei-probe.c`
and the linker script as `ei-probe.ld` in the disposable checkout.

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

```shell
clang --target=riscv32-unknown-none-elf -march=rv32imac -mabi=ilp32 -mcmodel=medany -mno-relax -ffreestanding -nostdlib -nostdinc -O0 -Iinclude -I../build/debug/include -fuse-ld=lld -Wl,-T,ei-probe.ld ei-probe.c -o ei-probe.elf
qemu-system-riscv32 -machine virt -bios none -nographic -kernel ei-probe.elf
qemu-system-riscv32 -machine virt -bios none -nographic -kernel ei-probe.elf -icount shift=5,sleep=off
```

The expected restored MIE is zero. Both executions print `restored_mie=1`
with the reviewed implementation. The observation ELF exits through the QEMU
finisher with status zero, so the printed state supplies the discrepancy
evidence. Its entry point and stack are defined by the shown linker script.

The recorded environment was Windows x64, clang 21.1.8, CMake 4.3.1-msvc1,
Unity 2.6.1 and QEMU 9.2.50 development build. Recorded source reviews,
compile probes and target observations establish the specific findings above.
Additional evidence is required for the explicitly listed gaps.
