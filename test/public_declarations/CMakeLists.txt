# SPDX-FileCopyrightText: 2026 Daisuke Nagao
#
# SPDX-License-Identifier: MIT

# Compile and link the same two translation units as both public include forms.
function(add_public_declarations_check name)
  add_executable(${name} definitions.c consumer.c)
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/include
                                             ${PROJECT_BINARY_DIR}/include)
  target_compile_features(${name} PRIVATE c_std_11)
  target_compile_options(${name} PRIVATE -fno-common -Wno-macro-redefined)
  if(NOT CMAKE_CROSSCOMPILING)
    # The host includes integer-address I/O helpers whose casts are RV32-sized.
    target_compile_options(${name} PRIVATE -Wno-int-to-pointer-cast)
  endif()

  if(ARGC GREATER 1 AND ARGV1 STREQUAL "UMBRELLA")
    target_compile_definitions(${name} PRIVATE PUBLIC_DECLARATIONS_UMBRELLA)
  endif()
  if(ARGC GREATER 2 AND ARGV2 STREQUAL "CONST")
    target_compile_definitions(${name} PRIVATE TKERNEL_CHECK_CONST)
  endif()

  if(CMAKE_CROSSCOMPILING)
    # Match the repository's QEMU-gated RV32 test availability.
    if(QEMU_EXECUTABLE)
      add_test(NAME ${name}-link COMMAND ${CMAKE_COMMAND} --build
                                         ${CMAKE_BINARY_DIR} --target ${name})
    endif()
  else()
    add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}>)
  endif()
endfunction()

add_public_declarations_check(public-declarations-direct)
add_public_declarations_check(public-declarations-umbrella-off UMBRELLA)
add_public_declarations_check(public-declarations-umbrella-on UMBRELLA CONST)
