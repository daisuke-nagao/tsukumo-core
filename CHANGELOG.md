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
- Added `putstring.h` to provide a helper function for UART-based string output.
- Added `kernel/list.h` to implement a doubly linked list utility:
  - Provides functions like `tkmc_init_list_head`, `tkmc_list_add_tail`, `tkmc_list_del`, and `tkmc_list_empty`.
  - Macros for container manipulation (`tkmc_list_entry`, `tkmc_list_first_entry`).
- Enhanced task management in `main.c`:
  - Implemented `tkmc_create_task` for creating tasks with their stacks and entry points.
  - Added `tkmc_start_task` to mark tasks as ready.
  - Implemented `tkmc_context_switch` for cooperative multitasking between tasks.
  - Updated task initialization to create and launch `task1` and `task2`.
- Introduced `task1.c` and `task2.c` for demonstration tasks:
  - `task1` outputs "Hello, world." via UART using `putstring`.
  - `task2` outputs "FizzBuzz" via UART using `putstring`.
- Enhanced `typedef.h.in` with additional application-specific types:
  - Attributes (`ATR`), error codes (`ER`), function codes (`FN`), and priorities (`PRI`).
- Updated `CMakeLists.txt`:
  - Integrated `context_switch.s`, `task1.c`, `task2.c`, `putstring.h`, and `kernel/list.h`.
  - Enabled the generation of `typedef.h` from `typedef.h.in` using `DetermineTypes.cmake`.
  - Added new configuration options: `TK_HAS_DOUBLEWORD` for 64-bit support and `TK_SUPPORT_USEC` for microsecond-related types.

### Changed
- Refactored `start.s`:
  - Improved alignment with `.balign 4`.
  - Updated handling of the global pointer (`gp`) with `.option norelax` and `.option relax`.
- Replaced the deprecated `tkmc_launch_task` in `launch_task.s` with `__launch_task` from `context_switch.s`.
- Applied consistent SPDX license header formatting across all source files.
- Refactored task management in `kernel/task.c` and `kernel/start.c`:
  - Replaced custom TCB linked list management with the new `tkmc_list_head` from `list.h`.
  - Added priority (`itskpri`) to `TCB` for task scheduling.
  - Simplified context switching logic with priority-based scheduling.

### Fixed
- None.

### Removed
- Deprecated `tkmc_launch_task` from `launch_task.s` as its functionality is now covered by `__launch_task` in `context_switch.s`.
- Removed custom linked list implementation in `kernel/task.c` in favor of the new `kernel/list.h`.
- Removed `tkmc_context_switch` in favor of the more flexible `tkmc_yield` function.
