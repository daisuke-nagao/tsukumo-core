# Selected interrupt pending-clear validation

`ClearInt(UINT)` discards one selected PLIC pending request. Its public
contract is in the [interrupt guide](../../kernel/interrupts.md#clearing-a-pending-request).
This record explains how the tests check that another source retains its
request and that an existing handler keeps its claim ownership.

## Environment

The checks use Windows x64, Clang 21.1.8, Unity 2.6.1 and a QEMU 9.2.50
development build. The target is single-hart RV32 `rv32imac`/`ilp32` on
QEMU `virt`, using machine-mode PLIC context 0. Host tests use ASan and UBSan.

The baseline passed 174 host Unity cases, three public consumers and all
106 Debug CTests. C11 and C++17 consumers in both `TKERNEL_CHECK_CONST`
modes reproduced the missing `ClearInt` declaration. The added declaration
and definition then compiled and linked into an RV32 ELF in all four modes.

## Results

The implementation passed 180 host Unity cases, three public consumers and
all 106 CTests in each of `debug`, `debug-lto`, `relwithdebinfo`,
`relwithdebinfo-lto`, `minsizerel` and `minsizerel-lto`. The interrupt suite
and exact completion trace each cover 50 cases. Both QEMU clocks, existing
timer/mailbox instruction budgets and public RV32 links passed.

Independent adversarial review found two gaps in the new verification.
The host matrix now samples ignored completions before a later claim resets
the fake counter. The new nested ClearInt path now measures its painted
stack usage against the suite's existing bounds. Both corrections passed
re-review. The final checks protect accepted completion and stack use on the
added path.

## Controller contract

The PLIC pending registers are read-only. The implementation temporarily
isolates the target across all three context-0 enable words. A priority of
zero is temporarily raised to one. After isolation, threshold is temporarily
set to zero for the tested QEMU claim implementation. One claim read consumes the selected
pending request, if any, and a nonzero matching claim receives one completion
write while the source is still enabled. The original priority and every
enable word are restored before MIE is restored. The final saved-threshold
write refreshes QEMU notification state with those restored settings.
The PLIC specification defines claim selection independently of threshold.
The controller ownership rules follow the
[PLIC v1.0.0 claim](https://docs.riscv.org/reference/plic/v1.0.0/plic-claims.html)
and [completion](https://docs.riscv.org/reference/plic/v1.0.0/plic-completion.html)
contracts.

The driver clears the device cause. A later request can become pending after
the completion. The call makes one attempt and never waits for the device
condition to disappear. It leaves active interrupt records, their completion
flags and their later C or ASM cleanup intact.

## Host observations

The fake models all 96 source positions with separate pending and claimed
state. Its selection uses source enable, nonzero priority, highest priority
and smallest source ID to break a tie. The claim ignores threshold. The
focused fake checks establish these rules before ClearInt assertions rely
on them. Existing direct-entry fixtures supply controller prerequisites.

The ClearInt tests exercise invalid and absent requests, enabled and disabled
sources, priority zero, thresholds, current-task independence and both MIE
states. Multiple pending sources include competitors in other enable words,
higher and equal priority, and smaller IDs. They compare controller settings,
active contexts and kernel state and then claim the preserved requests.
They also exercise an outstanding handler claim and a new request while an
earlier completed record remains active.

## Target observations

The interrupt suite uses UART source 10 and goldfish RTC source 11.
The [completion trace checker](../../../test/check_interrupt_completion.py)
counts actual writes to `0x0c200004` within each named case, including writes
the controller may ignore. It distinguishes the temporary ClearInt claim's
completion from later handler completions through the expected source sequence.
Its parser self-test rejects unscoped writes and missing or failed results.

## Restored pending-source notification

The first simultaneous UART/RTC run preserved the RTC pending bit but its
handler did not execute after source enables were restored. A diagnostic
same-value threshold write triggered delivery. A further UART test at priority
one with threshold seven reproduced a claim returning zero while the target
remained pending, under both clocks. The final implementation temporarily
uses threshold zero after target isolation, then restores the saved threshold
after every enable word and before EI. That final write also refreshes
notification state. The fixtures call ClearInt directly, check below-threshold
clearing and observe the retained source's later handler execution.

## Isolated faults

Each fault is applied separately in an isolated source and host build tree.

| Fault | Observed failure |
| --- | --- |
| Leave other sources enabled for the claim | The target-pending matrix expected one EOI and observed zero. |
| Isolate only the first enable word | The target-pending matrix expected one EOI and observed zero. |
| Leave the selected priority at zero | The target-pending matrix expected one EOI and observed zero. |
| Omit the temporary claim's completion | The target-pending and nested matrices expected one EOI and observed zero. |
| Disable the target just for the completion write | The matrices expected zero ignored completions and observed one. |

All five faults built and reached assertions in
`test_clear_int_claims_only_its_pending_source_and_preserves_all_words`.
The last two also failed
`test_clear_int_in_nested_handler_preserves_outer_claim_and_record`.
Every restored variant rebuilt and passed its full host registration.
The disabled-completion fault confirms the corrected sampling catches an
EOI the controller ignores, even though the MMIO log contains a write.

Build errors and runner timeouts were excluded from detection. Each faulty
variant reached its intended assertion mismatch. Restored tests passed, and
the final production and test sources matched the accepted snapshot.

## Repeating the checks

Use the environment and dependencies in the [testing guide](../testing.md).
Configure and build the host preset, then run its registered tests.

```sh
cmake --preset host
cmake --build --preset host
ctest --preset host --output-on-failure
cmake --preset debug -DQEMU_ICOUNT_SHIFT=5
cmake --build --preset debug
ctest --test-dir ../build/debug --output-on-failure
```

Repeat the cross-build commands for `debug-lto`, `relwithdebinfo`,
`relwithdebinfo-lto`, `minsizerel` and `minsizerel-lto`. Registered tests cover
both QEMU clocks, the interrupt MMIO trace and existing timer/mailbox budgets.
To repeat a fault, copy the source into a disposable directory, alter only
ClearInt in `kernel/intctl.c`, configure a separate host tree and run
`host-tests` through CTest. Restore the function, rebuild and rerun the same
registration.

Physical devices, three-level nesting and multi-hart or additional PLIC
contexts require separate validation. Later, on 2026-11-18, the
[deep nesting record](deep-interrupt-nesting-validation.md) validated chains
of three to seven levels. The device cause remains under driver control, so
the tests do not establish a permanent absence of pending requests while a
device continues to assert its line.
