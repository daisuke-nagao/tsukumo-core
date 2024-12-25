# SPDX-FileCopyrightText: 2024 Daisuke Nagao
# SPDX-License-Identifier: MIT
# RISC-V Bare-metal Toolchain File for Windows using Clang

# Set system name and processor
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)

# Set Clang path (For Windows, adjust the path as appropriate)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)

# Explicitly set cross-compilation
set(CMAKE_CROSSCOMPILING TRUE)

# Specify RISC-V target (e.g., RV32IMAC)
set(TARGET_ARCH "riscv32-unknown-none-elf")
set(RISCV_MARCH "rv32imac")
set(RISCV_MABI "ilp32")

# Initial setup of compilation flags
set(RISCV_COMPILE_OPTIONS
    "-target ${TARGET_ARCH} -march=${RISCV_MARCH} -mabi=${RISCV_MABI} -mcmodel=medany"
)
set(CMAKE_CXX_FLAGS_INIT "${RISCV_COMPILE_OPTIONS}")
set(CMAKE_C_FLAGS_INIT "${RISCV_COMPILE_OPTIONS}")
set(CMAKE_ASM_FLAGS_INIT "${RISCV_COMPILE_OPTIONS}")

# Set linker flags
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-nostdlib -nostartfiles -T ${CMAKE_SOURCE_DIR}/linker.ld")

# Set include and library paths (adjust as needed)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Additional definitions for bare-metal environment

# add_definitions(-DBARE_METAL)

# Set ASM file extensions
set(CMAKE_ASM_SOURCE_FILE_EXTENSIONS s;S;as)
