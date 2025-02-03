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
set(COMMON_FLAGS -ffreestanding -nostdlib)

# Specify the executable
add_executable(${EXECUTABLE})

target_sources(
  ${EXECUTABLE}
  PRIVATE kernel/asm/rv32/start.s
          kernel/asm/rv32/launch_task.s
          kernel/asm/rv32/context_switch.s
          kernel/start.c
          kernel/task.c
          task1.c
          task2.c)

# Set CMake compilation flags
target_compile_options(
  ${EXECUTABLE} PRIVATE $<$<COMPILE_LANGUAGE:C>:${COMMON_FLAGS}>
                        $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>)

# Set required standards
target_compile_features(${EXECUTABLE} PRIVATE c_std_11 cxx_std_20)

# Add include directories (if needed)
target_include_directories(${EXECUTABLE} PRIVATE include
                                                 ${CMAKE_BINARY_DIR}/include/)

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

# Include external script
include(DetermineTypes.cmake)

# Generate typedef.h
configure_file(typedef.h.in ${CMAKE_BINARY_DIR}/include/tk/typedef.h @ONLY)

# Add compile definitions based on options
if(TKERNEL_CHECK_CONST)
  target_compile_definitions(${EXECUTABLE} PUBLIC TKERNEL_CHECK_CONST)
endif()
