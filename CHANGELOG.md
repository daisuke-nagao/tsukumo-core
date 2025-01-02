# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Included `launch_task.s` to implement the `tkmc_launch_task` assembly routine for launching tasks.
- Added `task1.c` to define a basic task that outputs "Hello, world." to the UART0 interface.
- Updated `main.c` to declare the `task1` function and its stack, and call `tkmc_launch_task` to initialize and launch `task1`.
- Added `README.md` with project details, including:
  - Description, current status, and setup requirements.
  - Instructions for running the RTOS on QEMU using a RISC-V processor.
- Included a `.clang-format` file to standardize code formatting, based on the LLVM style.
- Enhanced runtime initialization in `main.c` with a static function `clear_bss` to clear the `.bss` section.
- Integrated `start.s` for RISC-V environment initialization, with a call to `tkmc_start` for transitioning from assembly to C runtime.
- Added `CMakeLists.txt` for RISC-V bare-metal development, including:
  - Clang compiler setup, linker script integration, and custom build steps.
  - A custom target to display binary size in Berkeley format.
- Added `toolchain.cmake` for managing the RISC-V bare-metal toolchain.
- Included `linker.ld` to define memory layout and section settings.

### Changed
- Updated all source files with consistent SPDX license header formatting.
- Updated `start.s` to include `.option norelax` and `.option relax` for proper global pointer (`gp`) handling.

### Fixed
- None.

### Removed
- None.
