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

target_sources(${EXECUTABLE} PRIVATE start.s main.c launch_task.s context_switch.s task1.c task2.c)

# Set CMake compilation flags
target_compile_options(
  ${EXECUTABLE} PRIVATE $<$<COMPILE_LANGUAGE:C>:${COMMON_FLAGS}>
  $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>)

# Set required standards
target_compile_features(${EXECUTABLE} PRIVATE c_std_11 cxx_std_20)

# Add include directories (if needed)
target_include_directories(${EXECUTABLE} PRIVATE include ${CMAKE_BINARY_DIR}/include/)

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
option(TK_HAS_DOUBLEWORD "Enable 64-bit types" ON)
option(TK_SUPPORT_USEC "Enable microsecond-related types" ON)

# Include external script
include(DetermineTypes.cmake)

# Generate typedef.h
configure_file(
  typedef.h.in
  ${CMAKE_BINARY_DIR}/include/tk/typedef.h
  @ONLY
)

# Add compile definitions based on options
if(TK_HAS_DOUBLEWORD)
  target_compile_definitions(${EXECUTABLE} PUBLIC TK_HAS_DOUBLEWORD)
endif()

if(TK_SUPPORT_USEC)
  target_compile_definitions(${EXECUTABLE} PUBLIC TK_SUPPORT_USEC)
endif()
