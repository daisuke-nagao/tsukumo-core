# Explicit interrupt completion validation

The [interrupt guide](../../kernel/interrupts.md#explicit-completion) defines
`EndOfInt(UINT)` and its coordination with C and ASM exits. This record
explains how tests establish one PLIC completion write per claim.

## Environment and results

The runtime checks were recorded on 2026-11-15. Their source hashes and
results were checked again for the final handoff on 2026-11-17. The environment
used Clang 21.1.8 and a QEMU 9.2.50 development build, single-hart RV32
`rv32imac`/`ilp32` on QEMU `virt`. Host tests used ASan and UBSan on Windows.

The baseline passed 168 host Unity cases, three public consumers and all
104 Debug CTests. C11 and C++17 consumers in both CONST modes reproduced the
missing `EndOfInt` declaration.

The implementation passed 174 host Unity cases and three public consumers.
Each of `debug`, `debug-lto`, `relwithdebinfo`, `relwithdebinfo-lto`,
`minsizerel` and `minsizerel-lto` passed all 106 CTests. Those runs include
both QEMU clocks, the existing timer/mailbox instruction budgets, interrupt
stack bounds, an MMIO trace check and its parser regression test. The normal
interrupt suite and trace check each cover 41 cases.

C11 and C++17 consumers in both `TKERNEL_CHECK_CONST` modes compiled and
linked against the actual RV32 kernel archive. A typed function pointer used
`EndOfInt` from `usermain`, and the linked ELF defined its C-linkage symbol.

Independent read-only reviews covered the implementation and tests. The test
review led to complete state-preservation observations, single-call ASM and
mixed-nesting combinations, and rejection of EOI writes outside a traced
case. The final review found no remaining blockers. Formatting, REUSE and
Markdown checks passed.

## What the tests observe

The six EndOfInt host tests in `test/host/test_intctl.c` check immediate EOI,
repeated calls, C exit after explicit completion, invalid/inactive/unmatched
calls, disabled sources, MIE states and operation without a current task.
They also cover completion of an enclosing claim, same-source record
shadowing and initialization of a reused record.

The tests preserve and compare other source enable bits, priorities,
threshold, pending notification, context fields, active link and depth.
The existing kernel snapshot checks queues and dispatch requests. A disabled
source must receive an accepted EOI, with no increase in the fake's ignored
completion count.

The QEMU fixtures in `test/suites/interrupt.c` exercise normal high-level
return, `tk_ret_int` and direct ASM return with zero, one and repeated explicit
calls. Mixed tests cover both ASM/high-level nesting orders and both ASM exit
methods, including completion of the outer claim by the inner handler.
Register patterns, stack/depth, interrupt masks and dispatch observations
remain part of those fixtures.

The reassertion fixture cycles UART IER after EOI to drive a new request
before the handler exits. This verifies that completion releases the gateway
for that request. Continuously held device-line behavior requires separate
platform observation.

## Actual EOI write counts

`test/check_interrupt_completion.py` runs the interrupt ELF with QEMU's
`memory_region_ops_write` event enabled. It decodes UART output at
`0x10000000` to identify the existing RUN and result markers. It counts writes
to the PLIC claim/complete address `0x0c200004` within each case, including
writes that the PLIC might ignore.

| Case family | Expected completion source sequence |
| --- | --- |
| Repeated explicit completion followed by high-level exit, disabled source | `[10]` |
| Two UART deliveries through `tk_ret_int` or direct ASM return | `[10, 10]` |
| Explicit completion of the outer UART by an inner RTC handler | `[10, 11]` |
| Existing nested handlers with automatic completion | `[11, 10]` |

The checker validates all 41 cases. It rejects missing or repeated case
markers, missing or failed results, out-of-case completion writes and a
source sequence that differs from the expected sequence. The parser's small
synthetic regression test exercises valid input, orphan/extra/missing EOI,
and missing case/result or failed result. It runs as
`interrupt-completion-trace-self-test`.

## Isolated faults and restoration

Each fault used a separate copied source and build directory. Host variants
ran the full host suite. The ASM variant built the interrupt ELF and ran
`qemu-interrupt-completion-trace`.

| Fault | Intended failure observed |
| --- | --- |
| Omit the helper's EOI write while recording completion | EndOfInt host cases expected one write and observed zero. |
| Clear the flag before C automatic completion | The explicit-completion host case expected one write and observed two. |
| Remove the direct ASM completed-flag branch | The direct-return case traced `[10, 10, 10, 10]`, expected `[10, 10]`. |
| Initialize a new claim as already completed | The reused-record test failed, and EndOfInt cases observed zero completion writes. |

Every faulty variant compiled and reached its intended assertion or trace
count mismatch. Each production file was then restored and the same tests
passed. Build failures, unrelated-case failures and runner timeouts were
excluded from the detection result. The working source matched the accepted
snapshot and contained none of the injected faults.

## Repeating the checks

Use the environment setup and preset commands in the
[testing guide](../testing.md). Build the host preset and run its CTest
registration. The following commands configure a Debug cross build with an available QEMU,
build it and run its registered tests.

```sh
cmake --preset debug -DQEMU_ICOUNT_SHIFT=5
cmake --build --preset debug
ctest --test-dir ../build/debug --output-on-failure
```

Repeat for the other five cross presets. The trace test needs QEMU's
`memory_region_ops_write` event, listed by `qemu-system-riscv32 -trace help`.
The registered tests invoke the parser and trace checks through the configured
host Python interpreter. To repeat a fault, use a disposable source copy,
apply one table entry, build its relevant target and run the named host or
trace test. Restore the production file before rebuilding and rerunning.

The evidence covers the configured source range and two-level QEMU nesting.
Physical devices and deeper nesting require platform-specific validation.
The [specification review](../../kernel/spec-conformance-review.md) tracks the
remaining `ClearInt` availability gap.
