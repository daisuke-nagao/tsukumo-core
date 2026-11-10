# Memory and mailbox validation across optimization levels

This record is for developers who change the kernel's memory copy and fill
functions, the mailbox queue, the compiler flags or the toolchain. It shows
whether copying, filling and sending messages keep their expected results at
every optimization level, with and without link-time optimization (LTO).

Validated on 2026-10-31 (JST).

## Summary

The existing tests ran in six build configurations: the presets `debug`
(-O0), `relwithdebinfo` (-O2) and `minsizerel` (-Os), each without LTO and
with ThinLTO (`-flto=thin`). Every configuration passed all 7 focused tests and
all 43 CTest tests. The existing presets and tests were sufficient, and no
kernel, build-system or test correction was needed.

With the recorded toolchain and test inputs, copying, filling and sending
messages keep their expected results at -O0, -O2 and -Os, with and without LTO.
After a source or toolchain change, repeat the comparison instead of assuming
that this result still applies.

## What the optimizer could break

The word loops in [memcpy.c](../../../kernel/memcpy.c) and
[memset.c](../../../kernel/memset.c) access storage that belongs to objects of
different C types. They use the compiler's `may_alias` attribute, so the
optimizer cannot assume that those accesses leave, for example, a float
unchanged. The [mailbox implementation](../../../kernel/mailbox.c) queues
messages through the link type that [T_MSG](../../../include/tk/tkernel.h)
declares, so it never accesses a message through an unrelated structure type
that merely has the same layout.

LTO lets the compiler optimize across source files. It can therefore expose
aliasing mistakes that a debug build hides.

## Results

Each configuration used its own build directory. The table records the flags
observed in the build commands, not flags inferred from the preset names. The
checked commands were the compile commands for `kernel/memcpy.c`,
`kernel/memset.c` and `kernel/mailbox.c`, for the memcpy and mailbox suites and
for the test runner, and the link commands of the memcpy and mailbox ELFs. No
configuration used `-fno-strict-aliasing` as a workaround.

| Preset | Effective optimization | Compile/link LTO | Focused CTest | Full CTest |
| --- | --- | --- | --- | --- |
| debug | Clang default, no `-O` flag (-O0) | OFF | 7/7 | 43/43 |
| relwithdebinfo | `-O2` | OFF | 7/7 | 43/43 |
| minsizerel | `-Os` | OFF | 7/7 | 43/43 |
| debug-lto | Clang default, no `-O` flag (-O0) | `-flto=thin` | 7/7 | 43/43 |
| relwithdebinfo-lto | `-O2` | `-flto=thin` | 7/7 | 43/43 |
| minsizerel-lto | `-Os` | `-flto=thin` | 7/7 | 43/43 |

The seven focused tests were `qemu-memcpy`, `qemu-memset`, `qemu-mailbox`,
their three `-hostclock` runs on the host clock, and `word-access-tbaa`. The
full CTest run included these tests again and every other registered test,
including the runner self-tests, whose expected failures CTest evaluates. Each
full run included 15 host-clock tests. No test was skipped or disabled to
obtain a passing result. [Running the kernel and its test suites on
QEMU](run_qemu.md) describes each suite and the host-clock runs.

The existing tests covered:

- copy lengths 0 through 70 with source and destination offsets 0 through 3;
- typed copy and fill of structs and of `UW` (unsigned 32-bit word), float and
  double arrays;
- mailbox FIFO and priority queues, direct delivery to a waiting receiver,
  receiver ordering, deletion and timeout.

The results were judged by the tests' assertions and expected summaries, not
by a byte-for-byte comparison of UART output, which contains addresses and
timing.

## Environment

- Windows x64 host; Ninja generator, using the Visual Studio developer
  environment.
- Clang and LLD 21.1.8; CMake 4.3.1-msvc1; Ninja 1.13.2.
- Target: `riscv32-unknown-none-elf`, `-march=rv32imac`, `-mabi=ilp32`.
- QEMU 9.2.50, machine `virt`, no BIOS.
- Unity v2.6.1, the C unit-test framework the tests use. All build directories
  shared one existing checkout through `FETCHCONTENT_SOURCE_DIR_UNITY`.
- QEMU instruction-count clock: `-icount shift=5`. The `-hostclock` tests ran
  without `-icount`, and CTest ran them one at a time.

## Reproduction

Before you start:

- Use a fresh clone and PowerShell, with Clang, LLD, CMake (3.22 or newer),
  Ninja and `qemu-system-riscv32` on PATH. A Visual Studio developer shell can
  supply CMake and Ninja.
- The first configure downloads the pinned Unity v2.6.1, so it needs network
  access. No earlier build directory or local helper is needed.
- See [Testing](../testing.md) for the build requirements and presets, and
  [Testing: How CTest decides a result](../testing.md#how-ctest-decides-a-result)
  for how CTest judges the suites.

Run the following from the repository root:

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

To work offline, add `-DFETCHCONTENT_SOURCE_DIR_UNITY=<checkout>` to the
configure command, pointing to an existing Unity checkout at the version above.
Do not reuse one build directory for different presets.

Then check the results:

1. Read `compile_commands.json` and `build-commands.log` in each build
   directory for the effective compile and link flags. Non-LTO commands must
   have no `-flto` flag. With LTO, the flag must apply to both the C objects
   and the ELF links. Check for later flags that could override the intended
   optimization.
2. Record the output of `clang --version`, `ld.lld --version`,
   `cmake --version`, `ninja --version` and `qemu-system-riscv32 --version`
   with the results.
3. CTest writes the guest output to `Testing/Temporary/LastTest.log` in each
   build directory, and the next CTest run replaces it. If you need both logs,
   save the focused run's log before the full run.

Every configure, build and test command must succeed. Missing QEMU,
unregistered tests, crashes and timeouts do not count as passes. The generated
build directories and logs are not part of the clone.

## Limits

`word-access-tbaa` generates its own IR at `-O2` with the LLVM passes
disabled. It checks the alias metadata of the compiler front end, so running it
in six build directories does not make it a check of six final LTO outputs. The
QEMU tests and the observed compile and link flags are the separate evidence
for each configuration. `debug-lto` uses ThinLTO but keeps the limited
optimization of -O0.

This comparison covers the recorded toolchain and test inputs. It does not
prove the absence of undefined behavior for all inputs, validate other compiler
versions, or replace testing on physical hardware.
