# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Included `launch_task.s` to implement the `tkmc_launch_task` assembly routine for launching tasks.
- Added `task1.c` to define a basic task that outputs "Hello, world." to the UART0 interface.
- Enhanced `CMakeLists.txt` with:
  - Support for `typedef.h` generation using `typedef.h.in` and `DetermineTypes.cmake`.
  - New options: `TK_HAS_DOUBLEWORD` (for 64-bit types) and `TK_SUPPORT_USEC` (for microsecond-related types).
  - Updated include directories to include `${CMAKE_BINARY_DIR}/include/`.
  - Added conditional compile definitions based on options (`TK_HAS_DOUBLEWORD` and `TK_SUPPORT_USEC`).
- Added `DetermineTypes.cmake` to dynamically determine data type sizes and generate type definitions.
- Included `typedef.h.in` to define configurable data types and constants for the project.
- Updated `main.c`:
  - Included `typedef.h`.
  - Declared the `task1` function and its stack.
  - Integrated a call to `tkmc_launch_task` to initialize and launch `task1`.
- Added `toolchain.cmake` for managing the RISC-V bare-metal toolchain.
- Included `linker.ld` to define memory layout and section settings.

### Changed
- Updated all source files with consistent SPDX license header formatting.
- Updated `start.s` to include `.option norelax` and `.option relax` for proper global pointer (`gp`) handling.

### Fixed
- None.

### Removed
- None.
