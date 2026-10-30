# Memory and mailbox validation across optimization levels

Validated on 2026-10-31 (JST).

## Purpose

The word loops in [memcpy.c](../../../kernel/memcpy.c) and
[memset.c](../../../kernel/memset.c) access storage belonging to objects of
different C types. They use the compiler's `may_alias` attribute so the
optimizer cannot assume those accesses leave, for example, a float unchanged.
The [mailbox implementation](../../../kernel/mailbox.c) queues messages through
the same link type declared in [T_MSG](../../../include/tk/tkernel.h), avoiding
access through an unrelated structure type with merely the same layout.

This comparison checks that copying, filling and sending messages preserve
their expected results at three optimization levels, with and without
link-time optimization (LTO). LTO can optimize across source files, exposing
aliasing mistakes that a debug build can hide. The existing presets and tests
are sufficient; no kernel or test correction was needed during this validation.

## Environment

- Windows x64 host; Ninja generator, using the Visual Studio developer environment.
- Clang and LLD 21.1.8; CMake 4.3.1-msvc1; Ninja 1.13.2.
- Target: `riscv32-unknown-none-elf`, `-march=rv32imac`, `-mabi=ilp32`.
- QEMU 9.2.50, machine `virt`, no BIOS.
- Unity v2.6.1;
  the same existing checkout was shared through `FETCHCONTENT_SOURCE_DIR_UNITY`.
- QEMU instruction-count clock: `-icount shift=5`; the companion `-hostclock`
  tests ran without `-icount`. CTest ran them serially.

## Results

Each configuration used its own build directory. The table
records observed flags, rather than inferring them from preset names. The
compile commands for `kernel/{memcpy,memset,mailbox}.c`, the memcpy/mailbox
suites and the runner were checked, as were the memcpy/mailbox ELF link commands.
No configuration used `-fno-strict-aliasing` as a workaround.

| Preset | Effective optimization | Compile/link LTO | Focused CTest | Full CTest |
| --- | --- | --- | --- | --- |
| debug | Clang default, no `-O` flag (-O0) | OFF | 7/7 | 43/43 |
| relwithdebinfo | `-O2` | OFF | 7/7 | 43/43 |
| minsizerel | `-Os` | OFF | 7/7 | 43/43 |
| debug-lto | Clang default, no `-O` flag (-O0) | `-flto=thin` | 7/7 | 43/43 |
| relwithdebinfo-lto | `-O2` | `-flto=thin` | 7/7 | 43/43 |
| minsizerel-lto | `-Os` | `-flto=thin` | 7/7 | 43/43 |

The seven focused tests were `qemu-memcpy`, `qemu-memset`, `qemu-mailbox`, their
three `-hostclock` companions, and `word-access-tbaa`. Full CTest included these
tests again and all other registered checks, including the runner self-tests
whose expected failures CTest evaluates. Each full run included 15 host-clock
tests. No test was skipped or disabled to obtain a passing result.

The existing tests checked copy lengths 0 through 70 with source/destination
offsets 0 through 3, typed copy/fill of structs and UW/float/double arrays,
and mailbox FIFO/priority queues, direct delivery, receiver ordering, deletion
and timeout. Results were compared through the tests' assertions and expected
summaries, rather than byte-for-byte UART output containing addresses or timing.

## Reproduction

From a fresh clone, use PowerShell with Clang, LLD, CMake (3.22 or newer),
Ninja and `qemu-system-riscv32` available on PATH. A Visual Studio developer
shell can supply CMake and Ninja. See the [README](../../../README.md) for the
basic build requirements and [QEMU test documentation](run_qemu.md) for how
CTest judges the suite results. Run the following from the repository root.

Configure obtains the pinned Unity v2.6.1 dependency, so its first download
requires network access. No previous build directory or local helper is
required. Repeat the comparison after source or toolchain changes rather than
assuming the recorded results still apply.

```powershell
$presets = @('debug', 'relwithdebinfo', 'minsizerel',
             'debug-lto', 'relwithdebinfo-lto', 'minsizerel-lto')
foreach ($preset in $presets) {
    $buildDir = ".cache/optimization-validation/$preset"
    $ipo = if ($preset.EndsWith('-lto')) { 'ON' } else { 'OFF' }
    cmake --preset $preset -B $buildDir `
        "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=$ipo"
    if ($LASTEXITCODE) { throw "$preset configure failed" }
    cmake --build $buildDir
    if ($LASTEXITCODE) { throw "$preset build failed" }
    cmake --build $buildDir -- -t commands |
        Set-Content "$buildDir/build-commands.log"
    if ($LASTEXITCODE) { throw "$preset command listing failed" }
    ctest --test-dir $buildDir --no-tests=error --output-on-failure `
        -R '^(qemu-(memcpy|memset|mailbox)(-hostclock)?|word-access-tbaa)$'
    if ($LASTEXITCODE) { throw "$preset focused tests failed" }
    ctest --test-dir $buildDir --no-tests=error --output-on-failure
    if ($LASTEXITCODE) { throw "$preset full tests failed" }
}
```

For offline reuse, add `-DFETCHCONTENT_SOURCE_DIR_UNITY=<checkout>` to configure,
pointing to an existing Unity checkout at the version above. Do not reuse one
build directory for different presets.

Check `compile_commands.json` and `build-commands.log` in each build directory
for the effective compile/link flags. Non-LTO commands must have no `-flto`
flag; LTO must apply to both the C objects and ELF links. Check for later flags
that could override the intended optimization. Record `clang --version`,
`ld.lld --version`, `cmake --version`, `ninja --version` and
`qemu-system-riscv32 --version` alongside the results.

CTest writes guest output to each build's `Testing/Temporary/LastTest.log`.
Save the focused run's log before the full run if both are needed, since the
next CTest invocation replaces it. Each configure/build/test command must
succeed; missing QEMU, unregistered tests, crashes and timeouts do not count
as passes. Generated build directories and logs are not part of the clone.

## Limits

`word-access-tbaa` independently generates IR at `-O2` with LLVM passes disabled.
It checks the front end's alias metadata; running it in six build directories
does not make it a check of six final LTO outputs. The executable QEMU tests
and observed compile/link flags provide the separate configuration evidence.
Debug LTO uses ThinLTO but retains the limited optimization of -O0.

This comparison covers the recorded toolchain and test inputs. It does not
prove the absence of undefined behavior for all inputs, validate other compiler
versions, or substitute for physical hardware testing. No kernel, build-system
or test correction was needed for the configurations reported here.
