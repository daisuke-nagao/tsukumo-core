# tsukumo-core

This project aims to create a real-time operating system (RTOS) based on the uT-Kernel 3.0, targeting RISC-V architecture. The development is currently in the early stages, with the focus on porting basic C language code for processor handling.

## Current Status

- The project is in its early stages, and the code so far includes basic functionality for transferring processing to the RISC-V architecture.
- Target processor: **RISC-V**
- Emulator: **QEMU**
- The build environment is set up on **Windows**, using **Clang** as the compiler.

## Requirements

- **Windows** (Build environment)
- **Clang** (Compiler)
- **CMake** (Build system)
- **QEMU** (For running the RISC-V processor)

## Build Instructions

1. Clone the repository:
   ```shell
   git clone https://github.com/daisuke-nagao/tsukumo-core.git
   cd tsukumo-core
   ```

2. Configure the build with a preset (it selects the RISC-V toolchain; a plain
   `cmake ..` has no toolchain and builds only the PC unit tests, see below):
   ```shell
   cmake --preset debug
   ```

3. Build the project (the build directory is `../build/debug`):
   ```shell
   cmake --build --preset debug
   ```

## Running on QEMU

1. After building the project, you can run the generated ELF file on QEMU with the following command:
   ```shell
   qemu-system-riscv32 -machine virt -bios none -kernel tsukumo-core.elf -nographic
   ```

   This will boot the kernel in QEMU without a graphical interface. The kernel
   image has no tests, so it prints nothing and idles; stop it with Ctrl+A, then
   X. The tests are separate ELFs, described below.

2. If debugging is needed, run with the following option:
   ```shell
   qemu-system-riscv32 -machine virt -bios none -kernel tsukumo-core.elf -nographic -d in_asm
   ```

   This will display the executed RISC-V assembly instructions.

## Running the tests

The kernel tests are separate ELFs that run on QEMU and end the run by themselves.
With QEMU found at configure time, run them with CTest after building:

```shell
ctest --test-dir <build dir> --output-on-failure --no-tests=error
```

The exit status of each suite tells whether every case passed, except for the
`panic-*` suites: each ends at a deliberate fatal trap, and its test passes only
if the output has a `PANIC OK` line (the kernel stopped and reported the right
cause). See
[docs/developer/notes/run_qemu.md](docs/developer/notes/run_qemu.md) for details.

## Unit tests on the PC

The logic of the kernel (lists, wait queues, the timer handler, `tkmc_memset`) also has unit
tests that run on the PC with no QEMU and no RISC-V toolchain. They need only
clang. The kernel sources are compiled unchanged, with stand-ins for the two
headers that touch the machine (`include/sys/interrupt.h` and
`include/sys/ioport.h`) and AddressSanitizer and UBSan switched on:

```shell
cmake --preset host
cmake --build --preset host
ctest --preset host
```

The tests run on a fake machine that cannot switch stacks, so they check the
kernel's queues and task states when a task blocks and when it is woken, not
the values a woken task returns; the QEMU tests check those. The PC has 64 bit
pointers, so what the kernel stores as 32 bit addresses (for example in a new
task's initial frame) is not checked either. See `test/host/fakes.h`.

What is compiled and what is not:

- `kernel/task.c`, `timer.c`, `semaphore.c`, `event_flag.c` and `mailbox.c` are
  compiled. `memset.c` is compiled through `test/host/memset_under_test.c`, which
  neutralises its weak `memset` alias so that it does not clash with the C
  library. `start.c` (boot, linker symbols, the first task launch) and
  `ini_tsk.c` are not, and neither are the assembly files; the QEMU tests cover
  them.
- The tests are built for the PC's 64 bit pointers. A 32 bit build (`-m32`) was
  not possible where this was written: clang there ships the sanitizer runtimes
  for x86_64 only.
- The first configure downloads Unity with FetchContent. Without network access,
  point `-DFETCHCONTENT_SOURCE_DIR_UNITY=<path to a Unity source tree>` at an
  existing copy.

## Next Steps

- Implement more features based on the uT-Kernel 3.0 design.
- Enhance the kernel functionality and ensure compatibility with RISC-V instructions.
- Expand the project to include inter-process communication, scheduling, and other essential RTOS components.

## Contributing

Feel free to fork the repository, make contributions, and submit pull requests. For any issues or suggestions, please create an issue in the issue tracker.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
