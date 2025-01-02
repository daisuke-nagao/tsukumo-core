# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Included `launch_task.s` to implement the `tkmc_launch_task` assembly routine for launching tasks.
- Added `task1.c` to define a basic task that outputs "Hello, world." to the UART0 interface.
- Updated `main.c` to:
  - Declare the `task1` function and its stack.
  - Call `tkmc_launch_task` to launch `task1` during initialization.
- Added `launch_task.s` and `task1.c` as sources in `CMakeLists.txt`.
- Updated `linker.ld`:
  - Refined the license header formatting.
- Updated license headers across all source files for consistent style and clarity.
- Added `README.md` to provide project details, including:
  - Project description, current status, requirements, and next steps.
  - Instructions for running the RTOS on QEMU using a RISC-V processor.
- Included a `.clang-format` file to standardize code formatting, based on the LLVM style:
  - Added SPDX license headers to comply with CC0-1.0 license requirements.
- Added a new static function `clear_bss` in `main.c` to clear the `.bss` section during initialization.
- Integrated a call to the `clear_bss` function within `tkmc_start` to ensure uninitialized variables are properly set to zero.
- Introduced the `tkmc_start` function in `main.c` as a placeholder for transitioning to the C runtime.
- Added initialization for the global pointer (`gp`) and stack pointer (`sp`) in the assembly startup routine in `start.s`.
- Integrated a call to `tkmc_start` in the assembly startup routine to enable the transition from assembly to C code.
- Included SPDX license headers to ensure compliance with project licensing standards.
- Added an assembly file `start.s` for initialization, providing basic setup for the RISC-V target.
- Added `main.c` as the entry point of the project.
- Added `CMakeLists.txt` to support building for the RISC-V bare-metal environment:
  - Configured for Clang compiler usage.
  - Integrated custom linker script (`linker.ld`).
  - Added custom build step to generate a binary file using `objcopy`.
  - Included a custom target to display binary size in Berkeley format.
- Added `linker.ld` to define memory layout and section settings.
- Added Clang toolchain file `toolchain.cmake` for the RISC-V bare-metal environment.

### Changed
- Updated `CMakeLists.txt` to include additional spacing for SPDX license header clarity.
- Updated `start.s` to use `.option norelax` and `.option relax` directives for proper handling of the `gp` initialization.

### Fixed
- None.

### Removed
- None.
