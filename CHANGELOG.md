# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Added `CMakePresets.json` to define build and configure presets:
  - Presets include `debug`, `release`, `relwithdebinfo`, and `minsizerel` for different build configurations.
  - Common settings are inherited from the `base` preset, using the Ninja generator and exporting compile commands.
  - Supports `CMAKE_TOOLCHAIN_FILE` for RISC-V bare-metal toolchain configuration.
- Included `context_switch.s` to implement context switching with the `__context_switch` and `__launch_task` routines.
- Enhanced task management in `main.c`:
  - Implemented `tkmc_create_task` for creating tasks with their stacks and entry points.
  - Added `tkmc_start_task` to mark tasks as ready.
  - Implemented `tkmc_context_switch` for cooperative multitasking between tasks.
  - Updated task initialization to create and launch `task1` and `task2`.
- Introduced `task1.c` and `task2.c` for demonstration tasks:
  - `task1` outputs "Hello, world." via UART using `putstring`.
  - `task2` outputs "FizzBuzz" via UART using `putstring`.
- Added `putstring.h` to provide a helper function for UART-based string output.
- Enhanced `typedef.h.in` with additional application-specific types:
  - Attributes (`ATR`), error codes (`ER`), function codes (`FN`), and priorities (`PRI`).
- Updated `CMakeLists.txt`:
  - Integrated `context_switch.s`, `task1.c`, `task2.c`, and `putstring.h`.
  - Enabled the generation of `typedef.h` from `typedef.h.in` using `DetermineTypes.cmake`.
  - Added new configuration options: `TK_HAS_DOUBLEWORD` for 64-bit support and `TK_SUPPORT_USEC` for microsecond-related types.

### Changed
- Refactored `start.s`:
  - Improved alignment with `.balign 4`.
  - Updated handling of the global pointer (`gp`) with `.option norelax` and `.option relax`.
- Replaced the deprecated `tkmc_launch_task` in `launch_task.s` with `__launch_task` from `context_switch.s`.
- Applied consistent SPDX license header formatting across all source files.

### Fixed
- None.

### Removed
- Deprecated `tkmc_launch_task` from `launch_task.s` as its functionality is now covered by `__launch_task` in `context_switch.s`.
