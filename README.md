# tsukumo-core

tsukumo-core is a real-time operating system (RTOS) kernel for 32-bit RISC-V,
based on the
[μT-Kernel 3.0 specification](https://tron-forum.github.io/mtk3_spec_jp/index.html)
(also written uT-Kernel 3.0). It runs on the QEMU `virt` machine and is built
on Windows with Clang.

## Status

The project is in an early stage. The kernel implements a subset of the
μT-Kernel 3.0 API: creating and starting tasks, a task ending itself, sleep,
delay, wake-up and wait release, semaphores, event flags, mailboxes, external
interrupt handlers, interrupt control and I/O port access. The
[API support status](docs/kernel/API_Support_Status.md) lists which calls
exist. The goal is to implement more of the μT-Kernel 3.0 API.

- Target: RV32 (`rv32imac`, `ilp32` ABI), a single hart (hardware thread)
- Emulator: QEMU `virt` machine
- Build host: Windows, with Clang as the compiler

## Requirements

- Windows
- Clang, with the LLD linker (`toolchain.cmake` links with `-fuse-ld=lld`)
- CMake 3.22 or later
- Ninja (the presets use the Ninja generator)
- QEMU (`qemu-system-riscv32`), to run the kernel and the tests

The first configure of each build directory downloads the Unity test framework,
so it needs network access.
[Building and running the tests](docs/developer/testing.md#configure-without-network-access)
explains how to configure without it.

## Build

1. Clone the repository:
   ```shell
   git clone https://github.com/daisuke-nagao/tsukumo-core.git
   cd tsukumo-core
   ```

2. Configure the build with a preset. The preset selects the RISC-V toolchain.
   A plain `cmake ..` has no toolchain and builds only the PC unit tests.
   ```shell
   cmake --preset debug
   ```

3. Build the project. The build directory is `../build/debug`, beside the
   repository.
   ```shell
   cmake --build --preset debug
   ```

## Run on QEMU

From the build directory, boot the kernel image without a graphical interface:

```shell
qemu-system-riscv32 -machine virt -bios none -kernel tsukumo-core.elf -nographic
```

The kernel image has no tests, so it prints nothing and idles. Stop QEMU with
Ctrl+A, then X. The tests are separate programs; see the next section.

To display the executed RISC-V assembly instructions for debugging, add
`-d in_asm`:

```shell
qemu-system-riscv32 -machine virt -bios none -kernel tsukumo-core.elf -nographic -d in_asm
```

When CMake finds QEMU, it also writes scripts into the build directory that
start QEMU in these two ways; see
[Running the kernel and its test suites on QEMU](docs/developer/notes/run_qemu.md).

## Run the tests

After the build above, run the QEMU tests:

```shell
ctest --test-dir ../build/debug --output-on-failure --no-tests=error
```

To run the PC unit tests, which need no QEMU and no RISC-V toolchain:

```shell
cmake --preset host
cmake --build --preset host
ctest --preset host
```

[Building and running the tests](docs/developer/testing.md) explains how CTest
judges each suite, how to run the tests at every optimization level, and what
the PC tests cover.

## Documentation

The [documentation index](docs/README.md) lists every document by the question
it answers. The main ones are:

- [API support status](docs/kernel/API_Support_Status.md): which μT-Kernel 3.0
  calls exist.
- [Kernel behavior](docs/kernel/behavior.md): startup failure, which calls work
  in which context, and how long mailbox calls keep interrupts disabled.
- [Interrupts](docs/kernel/interrupts.md): `DI()` and `EI()`, interrupt
  handlers, the interrupt stack and nested interrupts.
- [Specification review](docs/kernel/spec-conformance-review.md): how each
  implemented call compares with the specification.
- [Building and running the tests](docs/developer/testing.md).

## Contributing

Fork the repository and open a pull request, or open an issue in the issue
tracker for a bug or a suggestion.

By submitting a contribution, you agree that it is provided under the same MIT
License as the project (see [LICENSE](LICENSE)).

See [CONTRIBUTING.md](CONTRIBUTING.md) for how to set up, test and submit a
change.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file
for details.

The tests download [Unity](https://github.com/ThrowTheSwitch/Unity) v2.6.1
(MIT License) when you configure the build. It is a test-only dependency and is
not part of the kernel image.
