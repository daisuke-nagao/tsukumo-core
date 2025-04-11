# SPDX-FileCopyrightText: 2024 Daisuke Nagao
#
# SPDX-License-Identifier: MIT

# CMakeLists.txt for RISC-V Bare-metal Project
cmake_minimum_required(VERSION 3.22)

# Set project name and languages (explicitly include ASM)
project(tsukumo-core LANGUAGES C ASM)

# Set executable name
set(EXECUTABLE ${PROJECT_NAME}.elf)

# Set common flags for bare-metal
set(COMMON_FLAGS -ffreestanding -nostdlib -nostdinc)

# Create static library for kernel components
add_library(
  tsukumo-core-kernel STATIC
  kernel/asm/rv32/start.S
  kernel/asm/rv32/launch_task.S
  kernel/asm/rv32/idl_tsk.S
  kernel/start.c
  kernel/task.c
  kernel/ini_tsk.c
  kernel/usermain.c
  kernel/memcpy.c
  kernel/memset.c
  kernel/timer.c
  kernel/event_flag.c
  kernel/semaphore.c)

# Set include paths for the kernel library
target_include_directories(tsukumo-core-kernel
                           PUBLIC include ${CMAKE_BINARY_DIR}/include/)

# Set compile options for the kernel library
target_compile_options(
  tsukumo-core-kernel
  PRIVATE $<$<COMPILE_LANGUAGE:C>:${COMMON_FLAGS} -fstack-usage>
          $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>)

# Set required language standard for the kernel library
target_compile_features(tsukumo-core-kernel PRIVATE c_std_11)

# Define macro function to create an executable with linked kernel library
function(tkmc_add_executable exec_name)
  # Parse arguments, expecting SOURCES keyword
  cmake_parse_arguments(TKMC "" "" "SOURCES" ${ARGN})

  # Define the executable with the given sources
  add_executable(${exec_name} ${TKMC_SOURCES})

  # Link against the prebuilt kernel static library
  target_link_libraries(${exec_name} PRIVATE tsukumo-core-kernel)

  # Add compile-time definitions (e.g. for CONST checking)
  target_compile_definitions(${exec_name} PUBLIC TKERNEL_CHECK_CONST)
endfunction()

# Add main executable with user task sources
tkmc_add_executable(
  ${EXECUTABLE}
  SOURCES
  usermain.c
  task1.c
  task2.c
  task3.c
  task4.c)

# Set CMake compilation flags
target_compile_options(
  ${EXECUTABLE} PRIVATE $<$<COMPILE_LANGUAGE:C>:${COMMON_FLAGS} -fstack-usage>
                        $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>)

# Set required standards
target_compile_features(${EXECUTABLE} PRIVATE c_std_11)

# Convert to BIN file
add_custom_command(
  TARGET ${EXECUTABLE}
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary ${EXECUTABLE} ${PROJECT_NAME}.bin
  COMMENT "Generating binary file")

# Custom target for size display
find_program(SIZE_COMMAND NAMES riscv32-unknown-elf-size llvm-size)
if(SIZE_COMMAND)
  add_custom_target(
    size ALL
    COMMAND ${SIZE_COMMAND} --format=berkeley "${EXECUTABLE}"
    DEPENDS ${EXECUTABLE})
else()
  message(
    WARNING "Size command not found. Size information will not be available.")
endif()

# Define options
option(
  TKERNEL_CHECK_CONST
  "Enable CONST checks to prevent unintended modifications to pointer parameters in system calls. This helps ensure code safety by using 'const' qualifiers when supported. If disabled, CONST will be defined as an empty macro to maintain compatibility with legacy code."
  ON)

# Include external script for type determination
include(DetermineTypes.cmake)

# Generate typedef.h from template
configure_file(typedef.h.in ${CMAKE_BINARY_DIR}/include/tk/typedef.h @ONLY)

# Add compile definitions based on options
if(TKERNEL_CHECK_CONST)
  target_compile_definitions(${EXECUTABLE} PUBLIC TKERNEL_CHECK_CONST)
endif()

# Detect the QEMU executable. On Windows, QEMU might be installed in "C:/Program
# Files/qemu".
find_program(
  QEMU_EXECUTABLE
  NAMES qemu-system-riscv32.exe qemu-system-riscv32
  HINTS "C:/Program Files/qemu" ENV ProgramFiles)

if(NOT QEMU_EXECUTABLE)
  message(
    WARNING
      "qemu-system-riscv32 not found. Please check PATH, CMAKE_PREFIX_PATH, or install QEMU."
  )
else()

  # Set the kernel image path (kernel is tsukumo-core.elf in CMAKE_BINARY_DIR).
  set(KERNEL_IMAGE "${CMAKE_BINARY_DIR}/tsukumo-core.elf")

  # Generate Linux shell scripts.

  # Standard QEMU launch script for Linux (run_qemu.sh).
  file(
    WRITE "${CMAKE_BINARY_DIR}/run_qemu.sh"
    "#!/bin/sh\n"
    "\"${QEMU_EXECUTABLE}\" -machine virt -kernel \"${KERNEL_IMAGE}\" -nographic -bios none\n"
  )

  # Debug QEMU launch script for Linux (run_qemu_debug.sh).
  file(
    WRITE "${CMAKE_BINARY_DIR}/run_qemu_debug.sh"
    "#!/bin/sh\n"
    "\"${QEMU_EXECUTABLE}\" -machine virt -kernel \"${KERNEL_IMAGE}\" -nographic -bios none -d in_asm\n"
  )

  # Generate Windows batch files.

  # Standard QEMU launch batch file for Windows (run_qemu.bat).
  file(
    WRITE "${CMAKE_BINARY_DIR}/run_qemu.bat"
    "@echo off\r\n"
    "\"${QEMU_EXECUTABLE}\" -machine virt -kernel \"${KERNEL_IMAGE}\" -nographic -bios none\r\n"
  )

  # Debug QEMU launch batch file for Windows (run_qemu_debug.bat).
  file(
    WRITE "${CMAKE_BINARY_DIR}/run_qemu_debug.bat"
    "@echo off\r\n"
    "\"${QEMU_EXECUTABLE}\" -machine virt -kernel \"${KERNEL_IMAGE}\" -nographic -bios none -d in_asm\r\n"
  )

  # Set executable permissions for Linux scripts (not needed on Windows). if(NOT
  # WIN32) file(CHMOD "${CMAKE_BINARY_DIR}/run_qemu.sh" OWNER_READ OWNER_WRITE
  # OWNER_EXECUTE) file(CHMOD "${CMAKE_BINARY_DIR}/run_qemu_debug.sh" OWNER_READ
  # OWNER_WRITE OWNER_EXECUTE) endif()
endif()
