# RV32 assembly entry validation

The assembly entry contract is described in the
[assembly entry guide](../../kernel/assembly-entry.md). This record explains
which faults the tests detect and how to repeat the checks from a clone.

## Environment and normal checks

Validation was performed on 2026-11-14 with Clang 21.1.8 and a QEMU 9.2.50
development build, targeting single-hart QEMU `virt`, RV32 `rv32imac`/`ilp32`.
Host kernel tests used AddressSanitizer and UBSan on Windows.

The host run passed 167 Unity cases and three public declaration consumers.
Each of `debug`, `debug-lto`, `relwithdebinfo`, `relwithdebinfo-lto`,
`minsizerel` and `minsizerel-lto` passed all 104 CTests. Those runs include
instruction-count and host-clock QEMU tests and the existing timer and mailbox
instruction budgets. The `interrupt` suite covers assembly tasks, both raw
interrupt exits and both orders of mixed ASM/high-level nesting. The
`task-context` variants cover invalid interrupt-return contexts, including an
assembly task that disables interrupts before its normal task return.

A standalone preprocessed assembler consumer included only the public include
path and expanded `TKMC_RET_INT_ASM`. A C++17 consumer, including
`<stdnoreturn.h>` first, compiled and linked against a C definition of
`tk_ret_int` with `TKERNEL_CHECK_CONST` off and on. The target interrupt ELF
also defines the actual public `tk_ret_int` symbol. The baseline target ELF
had no such symbol.

Independent read-only reviews covered the core and the test fixtures. They
led to fixture cleanup, nested `tk_ret_int` coverage, saved-frame content
checks and an isolated absent-current guard test. A host fixture that retained
ISR depth 2 while finishing its outer context caused a fatal abort. Restoring
depth 1 corrected it. Host diagnostics now identify an unexpected fatal API
and the active test.

## Isolated fault experiments

Copy the current source and tests into a disposable directory. Give each fault
its own source and build directories. Apply one change at a time. Build the
`tsukumo-core-test-interrupt.elf` target and run `qemu-interrupt` with the
instruction-count clock. For these experiments the disposable suite's case
list selected the relevant existing cases listed below. Its test bodies and
fixtures were retained. Restore the changed production file, rebuild and rerun
the same cases after each experiment.

The task cases are `asm_task_entry_and_termination_paths`,
`asm_task_restart_rebuilds_entry_and_arguments` and
`asm_task_tcb_reuse_starts_with_fresh_arguments`. The raw exit cases are
`asm_tk_ret_int_restores_frame_and_dispatches_in_external_trap` and
`asm_direct_return_restores_frame_and_leaves_dispatch_pending`. The outer
nested-dispatch case is `asm_outer_and_hlng_inner_dispatch_at_the_outer_exit`.

| Fault | Reached case and observed mismatch |
| --- | --- |
| Reject `TA_ASM` again in `tk_def_int` | Both raw exit cases expected `E_OK`, received `E_RSATR`. |
| Pass entry `sp` as the raw handler's `a1` | Both raw exit cases expected saved `s0 = 0x7e57000b`, received context depth 1. |
| Set ordinary task-entry `a0` to zero | Entry, restart and TCB-reuse cases reported incorrect start codes, including a negative code. Fixture cleanup commands were preserved so cleanup could finish before argument assertions. |
| Seed task `ra` with `tk_exd_tsk` | The normal-return case expected `TTS_DMT` (16), observed `TTS_NOEXS` (0). |
| Restore direct-return `t6` as zero | The direct-return register observer expected mask 0, received mask 16. |
| Omit the direct-return PLIC completion store | After two source-trigger attempts, the direct-return case expected two entries and observed one. |
| Skip outer `tk_ret_int` dispatch handling | The raw-return case observed no external-trap schedule, and the mixed nesting case expected one and observed zero. |

All seven faulty variants compiled. Each produced the relevant case's
assertion mismatch. After restoration, the same selected cases passed.
Build failures, unrelated case failures and runner timeouts were excluded
from the detection result. Device-event waits were bounded, and the
completion experiment checked the observed entry count after both trigger
attempts.

Task cleanup uses the reserved start code `0x41534d`, and an older fixture uses
`-1`. The argument experiment kept these two harness commands while replacing
other start codes with zero. The ordinary entry arguments remained subject to
the fault.

## Repeating the normal checks

Follow the tool setup in the [testing guide](../testing.md). For each cross
preset, configure, build and run CTest, for example with an installed QEMU
executable available to CMake.

```sh
cmake --preset debug -DQEMU_ICOUNT_SHIFT=5
cmake --build --preset debug
ctest --test-dir ../build/debug --output-on-failure
```

Repeat for the five other cross presets. The normal CTest registration
includes both clock modes. Run the host preset through the documented host
toolchain environment, then run its CTest registration. Formatting, REUSE and
Markdown hooks are defined in `.pre-commit-config.yaml`.

The stack observations in [Interrupts](../../kernel/interrupts.md#measured-stack-use)
cover the exercised one- and two-level paths. Hardware-device behavior,
three-to-seven-level nesting and interrupt-stack overflow protection remain
outside this evidence. The
[deep nesting record](deep-interrupt-nesting-validation.md) covers three to
seven levels with assembly handlers.
