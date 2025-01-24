# SPDX-FileCopyrightText: 2025 Daisuke Nagao
#
# SPDX-License-Identifier: MIT

# ~~~
# DetermineTypes.cmake
# Script to check type sizes and assign appropriate types
# ~~~

include(CheckTypeSize)

# Check type sizes
check_type_size("char" CHAR_SIZE)
check_type_size("short" SHORT_SIZE)
check_type_size("int" INT_SIZE)
check_type_size("long" LONG_SIZE)
check_type_size("long long" LONG_LONG_SIZE)

# Debug output for confirmed type sizes
message(STATUS "char size: ${CHAR_SIZE}")
message(STATUS "short size: ${SHORT_SIZE}")
message(STATUS "int size: ${INT_SIZE}")
message(STATUS "long size: ${LONG_SIZE}")
message(STATUS "long long size: ${LONG_LONG_SIZE}")

# Function to assign types based on size
function(assign_type target_size var_signed var_unsigned)
  if(CHAR_SIZE EQUAL ${target_size})
    set(${var_signed}
        "signed char"
        PARENT_SCOPE)
    set(${var_unsigned}
        "unsigned char"
        PARENT_SCOPE)
  elseif(SHORT_SIZE EQUAL ${target_size})
    set(${var_signed}
        "signed short"
        PARENT_SCOPE)
    set(${var_unsigned}
        "unsigned short"
        PARENT_SCOPE)
  elseif(INT_SIZE EQUAL ${target_size})
    set(${var_signed}
        "signed int"
        PARENT_SCOPE)
    set(${var_unsigned}
        "unsigned int"
        PARENT_SCOPE)
  elseif(LONG_SIZE EQUAL ${target_size})
    set(${var_signed}
        "signed long"
        PARENT_SCOPE)
    set(${var_unsigned}
        "unsigned long"
        PARENT_SCOPE)
  elseif(LONG_LONG_SIZE EQUAL ${target_size})
    set(${var_signed}
        "signed long long"
        PARENT_SCOPE)
    set(${var_unsigned}
        "unsigned long long"
        PARENT_SCOPE)
  else()
    message(FATAL_ERROR "No ${target_size}-bit type available")
  endif()
endfunction()

# Assign types for each bit width
assign_type(1 TYPE_8BIT_SIGNED TYPE_8BIT_UNSIGNED)
assign_type(2 TYPE_16BIT_SIGNED TYPE_16BIT_UNSIGNED)
assign_type(4 TYPE_32BIT_SIGNED TYPE_32BIT_UNSIGNED)
assign_type(8 TYPE_64BIT_SIGNED TYPE_64BIT_UNSIGNED)
