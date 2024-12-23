# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
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
- None.

### Fixed
- None.

### Removed
- None.
