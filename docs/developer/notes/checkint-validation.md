# CheckInt pending-query validation

`CheckInt(UINT)` returns a PLIC source's pending-bit snapshot. The
[interrupt guide](../../kernel/interrupts.md) specifies its source range,
invalid-input policy and calling contexts. This record describes the evidence
for the non-destructive query.

## Environment and normal results

Validation on 2026-11-15 used Clang 21.1.8 and a QEMU 9.2.50 development build
for single-hart RV32 `rv32imac`/`ilp32`, QEMU `virt`. Host tests ran on Windows
with AddressSanitizer and UBSan.

The baseline passed 167 host Unity cases, three public consumers and all
104 Debug CTests. C11 and C++17 consumers in both `TKERNEL_CHECK_CONST` modes
failed to compile their `CheckInt` reference because the declaration was
missing. This was evidence of the missing API.

The implemented version passed 168 host Unity cases, three public consumers
and all 104 CTests in each of `debug`, `debug-lto`, `relwithdebinfo`,
`relwithdebinfo-lto`, `minsizerel` and `minsizerel-lto`. These runs include
both QEMU clock modes and the existing timer and mailbox instruction budgets.
The `interrupt` suite passed 32 cases, including the new pending-query case
and the existing ASM return and nesting regressions.

Independent read-only review led to enabled-pending observations, the full
context/MIE matrix and corrected API inventory/history counts. The final
review found no blocking issues. Formatting, REUSE and Markdown hooks passed.

## Host observations

`test_check_int_reads_one_pending_word_without_claiming` in
`test/host/test_intctl.c` checks the first source and source 31, including the
unsigned high bit. With the current `CFN_MAX_INTNO=32`, source 33 is outside
the supported range.

- Invalid 0, `CFN_MAX_INTNO`, 33 and `UINT_MAX` return false with no MMIO.
- A valid query makes exactly one four-byte read at its pending word.
- A source can report pending while disabled, at priority zero, unregistered,
  or masked by the PLIC threshold. An enabled pending source also reports true.
- No-current, task and ISR contexts are checked with MIE enabled and disabled.
  Return values, MMIO, MIE, ISR depth and the existing kernel snapshot are
  checked. Fixture state is restored before assertions.
- Repeated queries retain the pending notification for a subsequent handler
  claim. After that claim, the pending query returns false.

The fake derives its pending bitmap from its existing single pending-source
slot. It retains that model's one-source limit. Port-log checks and later
handler delivery supplement the kernel snapshot, which omits PLIC state.

## QEMU and public consumers

`check_int_observes_disabled_pending_source_without_consuming_it` in
`test/suites/interrupt.c` raises the UART transmitter-empty interrupt while its
source is disabled. It repeats the query, disables MIE, enables the source and
queries again before allowing delivery. The source stays pending during the
queries and the handler runs once after delivery is allowed. The test also
checks that MIE remains disabled during that final query.

High-level handlers and both existing ASM interrupt-return fixtures call
`CheckInt` after the kernel has claimed their source. They observe the cleared
pending bit. Device, source and registration cleanup precedes the QEMU case's
result assertions.

C11 and C++17 consumers in both CONST modes compiled and linked against the
actual Debug kernel archive. Each consumer selected `CheckInt` through
`TK_SUPPORT_INTCTRL`, took a typed function pointer and called it from its
`usermain` entry. The linked RV32 ELF contained the public `CheckInt` symbol.

## Fault detection and restoration

Each experiment copied the source to an isolated directory and built the
`host-tests` target with the host preset. One `CheckInt` function fault was
applied at a time.

| Fault | Observed result |
| --- | --- |
| Read the claim register in place of the pending word | The pending-query test expected true and observed false. |
| Test bit `(intno + 1) % 32` | The same case expected true and observed false. |
| Require `is_enabled(intno)` before returning a pending bit | The same case expected true for a disabled source and observed false. |

All three variants built and reached that case's assertion. Each changed
production file was then restored, rebuilt and rerun. All 168 host cases
passed after each restoration. Build failures and runner timeouts were
excluded from the detection result. Source hashes confirmed that the working
implementation retained the accepted code.

## Repeating the checks

Use the environment setup in the [testing guide](../testing.md), then run
the existing presets.

```sh
cmake --preset host
cmake --build --preset host
ctest --preset host --output-on-failure
cmake --preset debug -DQEMU_ICOUNT_SHIFT=5
cmake --build --preset debug
ctest --test-dir ../build/debug --output-on-failure
```

Repeat the cross build and CTest commands for the other five presets. To
repeat a fault experiment, use a disposable copy, apply one table entry to
`CheckInt` in `kernel/intctl.c`, build `host-tests`, and run
`ctest --test-dir <host-build> -R '^host-tests$' -V`. Restore the production
file before rebuilding and rerunning the same tests.

The evidence covers the configured source range and QEMU platform. Physical
devices, a larger source configuration and deeper interrupt nesting require
additional platform validation. Later additions complete the
[interrupt-controller profile](../../kernel/spec-conformance-review.md#interrupt-controller-profile).
The [ClearInt record](clearint-validation.md) and
[EndOfInt record](interrupt-completion-validation.md) describe their checks.
