# SPDX-FileCopyrightText: 2026 Daisuke Nagao
#
# SPDX-License-Identifier: MIT

# Unit tests of the kernel logic that run on the PC, with no QEMU. The kernel
# sources are compiled unchanged. Everything that touches the machine goes
# through include/sys/interrupt.h and include/sys/ioport.h, and shadow/ holds
# stand-ins for both; it comes first on the include path. See fakes.h for what
# the fake machine can and cannot do.

FetchContent_MakeAvailable(Unity)

# The same generated types as the RISC-V build, in this build directory (the
# size checks are cached per build directory).
include(${PROJECT_SOURCE_DIR}/DetermineTypes.cmake)
configure_file(${PROJECT_SOURCE_DIR}/typedef.h.in
               ${CMAKE_BINARY_DIR}/include/tk/typedef.h @ONLY)

# Sanitizers. -fno-sanitize=null: tkmc_offsetof() in kernel/list.h takes the
# address of a member of a null pointer, which the null check reports on every
# use. No recovery, so a finding stops the test and the exit status shows it.
set(HOST_SANITIZE -fsanitize=address,undefined -fno-sanitize-recover=all
                  -fno-sanitize=null)

# The kernel sources the tests use. memcpy.c and memset.c are left out: their
# weak memcpy/memset aliases would fight with the C library's. memset.c is
# tested on its own through memset_under_test.c (see there).
add_library(
  kernel-host STATIC
  ${PROJECT_SOURCE_DIR}/kernel/task.c
  ${PROJECT_SOURCE_DIR}/kernel/timer.c
  ${PROJECT_SOURCE_DIR}/kernel/semaphore.c
  ${PROJECT_SOURCE_DIR}/kernel/event_flag.c
  ${PROJECT_SOURCE_DIR}/kernel/mailbox.c)
target_include_directories(
  kernel-host
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shadow ${PROJECT_SOURCE_DIR}/include
         ${CMAKE_BINARY_DIR}/include ${PROJECT_SOURCE_DIR}/kernel)
target_compile_definitions(kernel-host PUBLIC TKERNEL_CHECK_CONST)
target_compile_features(kernel-host PUBLIC c_std_11)
target_compile_options(
  kernel-host
  PUBLIC ${HOST_SANITIZE} -g
  PRIVATE # The kernel keeps 32 bit addresses in plain integers and the PC has
          # wider pointers, and typedef.h defines NULL itself; none of that
          # matters here.
          -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast
          -Wno-macro-redefined)
target_link_options(kernel-host PUBLIC ${HOST_SANITIZE})

add_executable(
  host-tests
  main.c
  fakes.c
  memset_under_test.c
  test_fixture.c
  test_list.c
  test_memset.c
  test_object_id.c
  test_task.c
  test_timeout_ticks.c
  test_timer.c
  test_wait.c
  test_wait_context.c
  ${PROJECT_SOURCE_DIR}/test/memset_matrix.c)
target_link_libraries(host-tests PRIVATE kernel-host unity::framework)
target_compile_options(host-tests PRIVATE -Wno-macro-redefined
                                          -Wno-pointer-to-int-cast)
# -fno-builtin: an optimizing build could turn the byte loops of memset.c into
# calls to the C library's memset, and the test would then check the library.
set_source_files_properties(memset_under_test.c PROPERTIES COMPILE_OPTIONS
                                                           -fno-builtin)

add_test(NAME host-tests COMMAND host-tests)
# A kernel bug that corrupts a queue can make a test loop forever; ctest has no
# time limit of its own.
set_tests_properties(host-tests PROPERTIES TIMEOUT 60)

# On Windows the AddressSanitizer runtime is a DLL next to the compiler's
# libraries, which is not on PATH.
if(WIN32)
  execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-resource-dir
    OUTPUT_VARIABLE clang_resource_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  # PATH holds semicolons, which separate the items of the ENVIRONMENT list, so
  # they are escaped.
  string(REPLACE ";" "\;" host_path "$ENV{PATH}")
  set_tests_properties(
    host-tests
    PROPERTIES ENVIRONMENT
               "PATH=${clang_resource_dir}/lib/windows\;${host_path}")
endif()
