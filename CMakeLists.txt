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
# This function simplifies the process of adding executables by automatically linking
# the kernel static library and applying necessary compile-time definitions.
# Arguments:
#   exec_name: Name of the executable to be created.
#   SOURCES: List of source files to be included in the executable.
function(tkmc_add_executable exec_name)
  # Parse arguments, expecting SOURCES keyword
  cmake_parse_arguments(TKMC "" "" "SOURCES" ${ARGN})

  # Define the executable with the given sources
  add_executable(${exec_name} ${TKMC_SOURCES})

  # Link against the prebuilt kernel static library
  # This ensures that the executable has access to the core kernel functionality.
  target_link_libraries(${exec_name} PRIVATE tsukumo-core-kernel)

  # Add compile-time definitions (e.g., for CONST checking)
  # TKERNEL_CHECK_CONST is used to enforce const correctness in system calls,
  # enhancing code safety by preventing unintended modifications to pointer parameters.
  target_compile_definitions(${exec_name} PUBLIC TKERNEL_CHECK_CONST)
endfunction()

# Add main executable with user task sources
# This creates the main application executable and links it with the kernel library.
# The user task sources (e.g., task1.c, task2.c) define the application-specific logic.
tkmc_add_executable(
  ${EXECUTABLE}
  SOURCES
  usermain.c
  userstack.c
  task1.c
  task2.c
  task3.c
  task4.c)

# download external libraries by using FetchContent
include(FetchContent)

# fetch the unity test framework
FetchContent_Declare(
  Unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG v2.6.1)

# Include directories for Unity to allow unity_config.h to be used in the test framework.
# This ensures the test framework functions correctly.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/test)

# Make Unity available
FetchContent_MakeAvailable(Unity)

# Link Unity to the unit test target
# Unity is a lightweight test framework for C. Linking it here allows the
# executable to include and run unit tests defined using Unity's API.
target_link_libraries(${EXECUTABLE} PRIVATE unity::framework)

# Set CMake compilation flags
# These flags are essential for bare-metal development, ensuring no standard
# libraries or headers are included and enabling stack usage analysis.
target_compile_options(
  ${EXECUTABLE} PRIVATE $<$<COMPILE_LANGUAGE:C>:${COMMON_FLAGS} -fstack-usage>
                        $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>)

# Set required standards
target_compile_features(${EXECUTABLE} PRIVATE c_std_11)

target_compile_definitions(unity PUBLIC -DUNITY_INCLUDE_CONFIG_H)

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
