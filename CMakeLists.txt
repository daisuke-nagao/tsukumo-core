# SPDX-FileCopyrightText: 2024 Daisuke Nagao
#
# SPDX-License-Identifier: MIT

# CMakeLists.txt for RISC-V Bare-metal Project
cmake_minimum_required(VERSION 3.22)

# Set project name and language. ASM is enabled below, for the RISC-V build
# only.
project(tsukumo-core LANGUAGES C)

# download external libraries by using FetchContent
include(FetchContent)

# fetch the unity test framework
FetchContent_Declare(
  Unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG v2.6.1)

# Only a toolchain file makes this the RISC-V build (it sets
# CMAKE_CROSSCOMPILING). Without one this is the PC build, which holds the unit
# tests of the kernel logic and nothing else (see the host preset).
if(NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_subdirectory(test/host)
  return()
endif()

enable_language(ASM)

# Set executable name
set(EXECUTABLE ${PROJECT_NAME}.elf)

# Set common flags for bare-metal
set(COMMON_FLAGS -ffreestanding -nostdlib -nostdinc)

# Create static library for kernel components
add_library(
  tsukumo-core-kernel STATIC
  kernel/asm/rv32/start.S
  kernel/asm/rv32/idl_tsk.S
  kernel/start.c
  kernel/context.c
  kernel/task.c
  kernel/ini_tsk.c
  kernel/memcpy.c
  kernel/memset.c
  kernel/timer.c
  kernel/event_flag.c
  kernel/semaphore.c
  kernel/mailbox.c
  kernel/intctl.c
  kernel/panic.c)

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

# CONST in the system call prototypes is const when TKERNEL_CHECK_CONST is
# defined (include/tk/tkernel.h). PUBLIC on the kernel library, so that the
# kernel and everything that links it read the same prototypes. Not defined per
# executable: the kernel was then compiled without it and the option did
# nothing.
option(TKERNEL_CHECK_CONST
       "Define CONST as const in the system call prototypes" ON)
if(TKERNEL_CHECK_CONST)
  target_compile_definitions(tsukumo-core-kernel PUBLIC TKERNEL_CHECK_CONST)
endif()

# tkmc_add_executable(<name> SOURCES <files>...) adds a bare-metal executable
# that links the kernel library.
function(tkmc_add_executable exec_name)
  cmake_parse_arguments(TKMC "" "" "SOURCES" ${ARGN})
  add_executable(${exec_name} ${TKMC_SOURCES})
  target_link_libraries(${exec_name} PRIVATE tsukumo-core-kernel)
  target_compile_options(
    ${exec_name} PRIVATE $<$<COMPILE_LANGUAGE:C>:${COMMON_FLAGS} -fstack-usage>)
  target_compile_features(${exec_name} PRIVATE c_std_11)
endfunction()

# The kernel image. It carries no tests: usermain is empty (kernel/usermain.c),
# and every test suite is an ELF of its own (see add_test_suite).
tkmc_add_executable(${EXECUTABLE} SOURCES kernel/usermain.c)

# Include directories for Unity to allow unity_config.h to be used in the test
# framework. This ensures the test framework functions correctly.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/test)

# Make Unity available
FetchContent_MakeAvailable(Unity)

target_compile_definitions(unity PUBLIC UNITY_INCLUDE_CONFIG_H)

# unity.c calls test_abort_hook (see test/unity_config.h), and unity.c is
# compiled once for every ELF, so the definition travels with the library.
target_sources(unity PRIVATE test/abort_hook.c)

# The test runner, shared by every suite ELF. kernel/ is on its include path
# because the runner reads task states from the kernel's task table.
add_library(test-runner OBJECT test/runner.c)
target_include_directories(test-runner PRIVATE kernel)
target_link_libraries(test-runner PUBLIC tsukumo-core-kernel unity::framework)
target_compile_options(
  test-runner PRIVATE $<$<COMPILE_LANGUAGE:C>:${COMMON_FLAGS} -fstack-usage>)
target_compile_features(test-runner PRIVATE c_std_11)

# add_test_suite(<name> SOURCES <files>... [WRAP <symbols>...] [ICOUNT_ONLY]
# [EXPECT_FAIL OUTPUT_REGEX <regex> [FAIL_REGEX <regex>]])
#
# WRAP links the ELF with --wrap for each symbol, for a suite that replaces a
# kernel function with a test-only wrapper (__wrap_<symbol>).
#
# ICOUNT_ONLY is for a suite that reads minstret. QEMU counts instructions in
# minstret only under -icount; on the host clock it returns host timer ticks, a
# value that is not comparable. Such a suite gets no -hostclock ctest. A suite
# also reads instructions per guest time, which is 2^-shift of it, so it runs
# only for a number of 8 or less in QEMU_ICOUNT_SHIFT: with "auto" the ratio
# follows the host, and with a larger shift a spinning idle task would run too
# few instructions for idle-halt to tell it from a halted one.
#
# Builds the suite ELF and registers its ctests in one place, so a suite can
# never be built without being run. A plain suite is one ctest, which fails on
# ":FAIL:" in the output. A plain suite may also give OUTPUT_REGEX: the output
# must then match it, which a suite that ends by itself (the panic suites)
# needs, because an exit status of 0 alone cannot tell it from a run that never
# did what the suite is about; ctest then ignores the exit status. With
# EXPECT_FAIL the suite must fail and gets a pair of ctests:
#
# * <name>-expectfail: WILL_FAIL, no regex. It also turns green when QEMU does
#   not start or the ELF is missing, so it is no proof on its own.
# * <name>-output: OUTPUT_REGEX has to match, which shows that the guest ran to
#   the summary. It is one regex because PASS_REGULAR_EXPRESSION entries are
#   alternatives, not a conjunction; chain the expected lines with ".*".
function(add_test_suite name)
  cmake_parse_arguments(SUITE "EXPECT_FAIL;ICOUNT_ONLY"
                        "OUTPUT_REGEX;FAIL_REGEX" "SOURCES;WRAP" ${ARGN})
  # An empty or misspelled OUTPUT_REGEX would register a -output test that
  # passes on any output, which is worse than no test.
  if(SUITE_UNPARSED_ARGUMENTS OR SUITE_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR "add_test_suite(${name}): bad arguments")
  endif()
  if(SUITE_EXPECT_FAIL AND NOT SUITE_OUTPUT_REGEX)
    message(
      FATAL_ERROR "add_test_suite(${name}): EXPECT_FAIL needs OUTPUT_REGEX")
  endif()
  set(elf ${PROJECT_NAME}-test-${name}.elf)
  tkmc_add_executable(${elf} SOURCES ${SUITE_SOURCES})
  target_link_libraries(${elf} PRIVATE test-runner unity::framework)
  # panic.h, for the suites that replace tkmc_panic().
  target_include_directories(${elf} PRIVATE kernel)
  foreach(symbol IN LISTS SUITE_WRAP)
    target_link_options(${elf} PRIVATE LINKER:--wrap=${symbol})
  endforeach()

  if(NOT QEMU_EXECUTABLE)
    return()
  endif()
  if(SUITE_ICOUNT_ONLY AND NOT (QEMU_ICOUNT_SHIFT MATCHES "^[0-9]+$"
                                AND QEMU_ICOUNT_SHIFT LESS_EQUAL 8))
    message(
      WARNING
        "${name}: not run, it needs QEMU_ICOUNT_SHIFT to be a number of 8 or less"
    )
    return()
  endif()
  set(qemu_command ${QEMU_EXECUTABLE} ${QEMU_RUN_ARGS} $<TARGET_FILE:${elf}>)
  if(SUITE_EXPECT_FAIL)
    add_test(NAME qemu-${name}-expectfail COMMAND ${qemu_command})
    set_tests_properties(qemu-${name}-expectfail
                         PROPERTIES ${QEMU_TEST_PROPERTIES} WILL_FAIL TRUE)
    add_test(NAME qemu-${name}-output COMMAND ${qemu_command})
    set_tests_properties(
      qemu-${name}-output
      PROPERTIES ${QEMU_TEST_PROPERTIES} PASS_REGULAR_EXPRESSION
                 "${SUITE_OUTPUT_REGEX}")
    if(SUITE_FAIL_REGEX)
      set_property(TEST qemu-${name}-output PROPERTY FAIL_REGULAR_EXPRESSION
                                                     "${SUITE_FAIL_REGEX}")
    endif()
  else()
    add_test(NAME qemu-${name} COMMAND ${qemu_command})
    set_tests_properties(
      qemu-${name} PROPERTIES ${QEMU_TEST_PROPERTIES} FAIL_REGULAR_EXPRESSION
                              ":FAIL:")
    if(SUITE_OUTPUT_REGEX)
      set_property(TEST qemu-${name} PROPERTY PASS_REGULAR_EXPRESSION
                                              "${SUITE_OUTPUT_REGEX}")
    endif()
    if(QEMU_ICOUNT_SHIFT MATCHES "^[0-9]+$|^auto$" AND NOT SUITE_ICOUNT_ONLY)
      add_test(NAME qemu-${name}-hostclock
               COMMAND ${QEMU_EXECUTABLE} ${QEMU_HOST_CLOCK_ARGS}
                       $<TARGET_FILE:${elf}>)
      set_tests_properties(
        qemu-${name}-hostclock
        PROPERTIES RUN_SERIAL
                   TRUE
                   TIMEOUT
                   30
                   FAIL_REGULAR_EXPRESSION
                   ":FAIL:"
                   LABELS
                   host-clock)
      if(SUITE_OUTPUT_REGEX)
        set_property(TEST qemu-${name}-hostclock
                     PROPERTY PASS_REGULAR_EXPRESSION "${SUITE_OUTPUT_REGEX}")
      endif()
    endif()
  endif()
endfunction()

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

# Include external script for type determination
include(DetermineTypes.cmake)

# Generate typedef.h from template
configure_file(typedef.h.in ${CMAKE_BINARY_DIR}/include/tk/typedef.h @ONLY)

enable_testing()

# Detect the QEMU executable. On Windows, QEMU might be installed in "C:/Program
# Files/qemu".
find_program(
  QEMU_EXECUTABLE
  NAMES qemu-system-riscv32.exe qemu-system-riscv32
  HINTS "C:/Program Files/qemu" ENV ProgramFiles)
add_subdirectory(test/public_declarations)

# How ctest runs an ELF on QEMU. test_exit() in test/finish.h makes QEMU exit,
# so the exit status carries the verdict and TIMEOUT catches a hang.
#
# By default the guest clock follows the number of instructions the guest has
# run (-icount, one instruction takes 2^shift ns), not the host clock. The tests
# wait on timers, so on the host clock a stalled host stretched the guest's
# waits and made runs differ; with -icount the UART log is identical from run to
# run and a suite takes a fraction of the time. Larger shifts run faster but
# leave fewer instructions per 10 ms tick; 5 leaves about 300000. The kernel
# idle task halts in wfi, so sleep=off lets the guest clock jump to the next
# timer deadline while every task waits, instead of waiting for it in real time.
# Without sleep=off a wait of 100 ms would also last 100 ms of wall time.
#
# One fixed schedule can hide a race that only the host clock shows, so every
# passing suite also runs once on the host clock as <name>-hostclock, labeled
# host-clock (ctest -L host-clock runs just those, ctest -LE host-clock skips
# them). Those run one at a time (RUN_SERIAL), because a stalled host would
# otherwise cause spurious timeouts. -DQEMU_ICOUNT_SHIFT= (empty) puts every
# test on the host clock and registers no extra ones; "auto" lets QEMU follow
# the host clock, so it is not repeatable either and also runs serially.
set(QEMU_ICOUNT_SHIFT
    5
    CACHE STRING "-icount shift for the QEMU tests: a number, auto, or empty")
if(NOT QEMU_ICOUNT_SHIFT MATCHES "^([0-9]+|auto)?$")
  message(
    FATAL_ERROR
      "QEMU_ICOUNT_SHIFT must be a number, auto, or empty (host clock)")
endif()
set(QEMU_HOST_CLOCK_ARGS -machine virt -bios none -nographic -kernel)
set(QEMU_RUN_ARGS -machine virt -bios none -nographic)
if(QEMU_ICOUNT_SHIFT MATCHES "^[0-9]+$")
  list(APPEND QEMU_RUN_ARGS -icount shift=${QEMU_ICOUNT_SHIFT},sleep=off)
  set(QEMU_TEST_PROPERTIES TIMEOUT 30)
else()
  if(QEMU_ICOUNT_SHIFT STREQUAL "auto")
    list(APPEND QEMU_RUN_ARGS -icount shift=auto)
  endif()
  set(QEMU_TEST_PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)
endif()
list(APPEND QEMU_RUN_ARGS -kernel)

# add_completion_trace(<test name> <suite>) runs the suite's ELF on QEMU under
# test/check_interrupt_completion.py, which checks the PLIC completions of each
# case against the set the script keeps for the suite.
if(QEMU_EXECUTABLE)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()
function(add_completion_trace test_name suite)
  if(NOT QEMU_EXECUTABLE)
    return()
  endif()
  add_test(
    NAME ${test_name}
    COMMAND
      ${Python3_EXECUTABLE}
      ${PROJECT_SOURCE_DIR}/test/check_interrupt_completion.py
      --expect=${suite} ${QEMU_EXECUTABLE} ${QEMU_RUN_ARGS}
      $<TARGET_FILE:${PROJECT_NAME}-test-${suite}.elf>)
  set_tests_properties(${test_name} PROPERTIES ${QEMU_TEST_PROPERTIES})
endfunction()

# Self-tests of the test runner. Each ELF must fail, and the regex pins which
# cases failed and how many ran (Unity prints "<n> Tests <f> Failures").
add_test_suite(
  selftest-fail SOURCES test/suites/selftest_fail.c EXPECT_FAIL OUTPUT_REGEX
  ":fail_case:FAIL.*:abort_effective:PASS.*:pass_case:PASS.*3 Tests 1 Failures")
add_test_suite(
  selftest-child-fail
  SOURCES
  test/suites/selftest_child_fail.c
  EXPECT_FAIL
  OUTPUT_REGEX
  ":child_fail_case:FAIL.*:child_abort_effective:PASS.*:pass_case:PASS.*3 Tests 1 Failures"
)
# marker must not run: the timeout aborts the suite, so ":marker:" must not
# appear. hang_case must not also be concluded as a pass (a second conclude
# would add a ":hang_case:PASS" line).
add_test_suite(
  selftest-hang
  SOURCES
  test/suites/selftest_hang.c
  EXPECT_FAIL
  OUTPUT_REGEX
  "RUN hang_case.*:hang_case:FAIL: timeout.*2 Tests 1 Failures"
  FAIL_REGEX
  ":marker:|:hang_case:PASS")
add_test_suite(
  selftest-orphan SOURCES test/suites/selftest_orphan.c EXPECT_FAIL
  OUTPUT_REGEX ":orphan_case1:FAIL.*:orphan_case2:PASS.*2 Tests 1 Failures")
# A child that spins forever after its case has returned: the runner must still
# time the case out. Like selftest-hang, marker must not run.
add_test_suite(
  selftest-hang-child
  SOURCES
  test/suites/selftest_hang_child.c
  EXPECT_FAIL
  OUTPUT_REGEX
  "RUN hang_child_case.*:hang_child_case:FAIL: timeout.*2 Tests 1 Failures"
  FAIL_REGEX
  ":marker:|:hang_child_case:PASS")
# The only passing selftest: it shows that an all-green run exits with status 0
# and that test_join() really waits.
add_test_suite(selftest-pass SOURCES test/suites/selftest_pass.c)
# A child that ends without its checkpoint must fail test_join_expect().
add_test_suite(
  selftest-nocheck SOURCES test/suites/selftest_nocheck.c EXPECT_FAIL
  OUTPUT_REGEX ":no_checkpoint:FAIL.*:pass_case:PASS.*2 Tests 1 Failures")

# A fatal trap must stop the kernel and report its cause. These suites end in
# the replaced tkmc_panic() (test/panic_hook.c), which prints "PANIC OK" only if
# mcause, mepc and mtval are what the case caused. The suite is a ctest of its
# own because the machine stops at the trap, so one ELF can test one trap.
add_test_suite(panic-illegal SOURCES test/suites/panic_illegal.c
               test/panic_hook.c OUTPUT_REGEX "PANIC OK")
add_test_suite(panic-load-fault SOURCES test/suites/panic_load_fault.c
               test/panic_hook.c OUTPUT_REGEX "PANIC OK")
add_test_suite(panic-bad-stack SOURCES test/suites/panic_bad_stack.c
               test/panic_hook.c OUTPUT_REGEX "PANIC OK")

# Each startup failure stops before the runner exists. Wrapping is confined to
# these ELFs; successful earlier calls still use the real task APIs.
foreach(
  entry
  "success|TKMC_STARTUP_NONE|0"
  "initial-create|TKMC_STARTUP_INITIAL_CREATE|0"
  "initial-start|TKMC_STARTUP_INITIAL_START|0"
  "idle-create|TKMC_STARTUP_IDLE_CREATE|0"
  "idle-start|TKMC_STARTUP_IDLE_START|0"
  "idle-start-mie|TKMC_STARTUP_IDLE_START|1")
  # <case>|<stage that fails>|<MIE on at the failure>
  string(REPLACE "|" ";" entry "${entry}")
  list(GET entry 0 startup_case)
  list(GET entry 1 startup_stage)
  list(GET entry 2 startup_mie)
  if(startup_stage STREQUAL "TKMC_STARTUP_NONE")
    set(startup_marker "STARTUP SUCCESS OK")
  else()
    set(startup_marker "STARTUP FAILURE OK stage=")
  endif()
  add_test_suite(
    startup-${startup_case}
    SOURCES
    test/suites/startup.c
    test/panic_fail.c
    WRAP
    tk_cre_tsk
    tk_sta_tsk
    tkmc_get_highest_priority_task
    tkmc_start_timer
    __launch_task
    OUTPUT_REGEX
    "${startup_marker}")
  target_compile_definitions(
    ${PROJECT_NAME}-test-startup-${startup_case}.elf
    PRIVATE STARTUP_FAIL_STAGE=${startup_stage}
            STARTUP_ENABLE_MIE=${startup_mie})
endforeach()

# Actual timer/dispatch entry and prelaunch/termination failures, not simulated
# ISR calls. Each fatal case needs its own boot; no production injection flags.
foreach(
  entry
  "isr|LIVE_ISR"
  "no-current-wait|NO_CURRENT_WAIT"
  "di-ext|DI_EXT"
  "di-exd|DI_EXD"
  "isr-ext|ISR_EXT"
  "isr-exd|ISR_EXD"
  "no-current-ext|NO_CURRENT_EXT"
  "no-current-exd|NO_CURRENT_EXD"
  "ret-task|RET_TASK"
  "ret-task-di|RET_TASK_DI"
  "ret-hlng|RET_HLNG"
  "ret-timer|RET_TIMER"
  "ret-software|RET_SOFTWARE"
  "ret-asm-task-di|RET_ASM_TASK_DI"
  "ret-no-current|RET_NO_CURRENT")
  # <case>|<mode in test/suites/task_context.c>
  string(REPLACE "|" ";" entry "${entry}")
  list(GET entry 0 context_case)
  list(GET entry 1 context_mode)
  if(context_mode STREQUAL "LIVE_ISR")
    set(context_marker "1 Tests 0 Failures")
  elseif(context_mode STREQUAL "NO_CURRENT_WAIT")
    set(context_marker "NO CURRENT WAIT OK")
  else()
    set(context_marker "CONTEXT STOP OK")
  endif()
  set(context_sources test/suites/task_context.c)
  if(context_mode STREQUAL "RET_ASM_TASK_DI")
    list(APPEND context_sources test/asm_task_di_ret.S)
  endif()
  add_test_suite(
    task-context-${context_case}
    SOURCES
    ${context_sources}
    WRAP
    tk_cre_tsk
    tkmc_timer_handler
    schedule
    OUTPUT_REGEX
    "${context_marker}")
  target_compile_definitions(
    ${PROJECT_NAME}-test-task-context-${context_case}.elf
    PRIVATE CONTEXT_TEST_MODE=${context_mode})
endforeach()

# tkmc_memset() is tested with the shift check on, trapping: an undefined shift
# in it would leave the bytes right, and only the check could tell. The options
# go on the file that includes kernel/memset.c, not on kernel/memset.c itself,
# because the options of a source file reach every target that uses it, the
# kernel library too. panic_hook.c turns the trap into a line that says what
# happened.
set_source_files_properties(
  test/suites/memset_under_test.c
  PROPERTIES COMPILE_OPTIONS "-fsanitize=shift;-fsanitize-trap=shift")
add_test_suite(
  memset SOURCES test/suites/memset.c test/suites/memset_under_test.c
  test/memset_matrix.c test/panic_hook.c)
# tkmc_memcpy() and tkmc_memset() of the kernel library itself, as callers get
# them, on bytes and on objects of other types.
add_test_suite(memcpy SOURCES test/suites/memcpy.c)
# The port access functions of sys/ioport.h, on RAM with guard bytes.
add_test_suite(ioport SOURCES test/suites/ioport.c)

# The word loops of tkmc_memcpy() and tkmc_memset() must access memory as a type
# that may alias any object (see kernel/memcpy.c). No test that runs can see a
# wrong alias assumption reliably, as it shows only where the optimizer inlines
# the function, so the IR clang makes is checked instead, with the flags of the
# RISC-V build. It is registered only where the QEMU suites are, so that a build
# without QEMU still has no tests and `ctest --no-tests=error` still fails.
separate_arguments(word_tbaa_flags NATIVE_COMMAND "${CMAKE_C_FLAGS}")
list(APPEND word_tbaa_flags ${COMMON_FLAGS} -I${PROJECT_SOURCE_DIR}/include
     -I${CMAKE_BINARY_DIR}/include)
if(TKERNEL_CHECK_CONST)
  list(APPEND word_tbaa_flags -DTKERNEL_CHECK_CONST)
endif()
if(QEMU_EXECUTABLE)
  add_test(
    NAME word-access-tbaa
    COMMAND
      ${CMAKE_COMMAND} -DCOMPILER=${CMAKE_C_COMPILER}
      "-DFLAGS=${word_tbaa_flags}" -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
      "-DSOURCES=${PROJECT_SOURCE_DIR}/kernel/memcpy.c;${PROJECT_SOURCE_DIR}/kernel/memset.c"
      -DREADERS=memcpy -P ${PROJECT_SOURCE_DIR}/test/check_word_tbaa.cmake)
endif()

# The kernel must clear .bss. bss_dirty_entry.S runs first and fills it with
# 0xa5, and the suite checks that no stretch of 0xa5 is left; QEMU starts with
# zeroed RAM, which would hide a kernel that does not clear it. The entry also
# leaves a value in a0, as boot code does, which must reach usermain.
add_test_suite(bss-clear SOURCES test/bss_dirty_entry.S test/suites/bss_clear.c
               OUTPUT_REGEX "BOOT ARG OK.*BSS OK")

# Kernel tests, one ELF and one ctest per feature.
add_test_suite(task-timer SOURCES test/suites/task_timer.c)
add_test_suite(task-restart SOURCES test/suites/task_restart.c)
add_test_suite(task-create SOURCES test/suites/task_create.c)
add_test_suite(timer-wrap SOURCES test/suites/timer_wrap.c)
# tk_set_utc()/tk_get_utc(); the wrap of tkmc_timer_handler() runs the calls in
# a real interrupt handler.
add_test_suite(system-time SOURCES test/suites/system_time.c WRAP
               tkmc_timer_handler)
add_test_suite(event-flag SOURCES test/suites/event_flag.c)
add_test_suite(semaphore SOURCES test/suites/semaphore.c)
add_test_suite(semaphore-wait-release SOURCES
               test/suites/semaphore_wait_release.c)
add_test_suite(mailbox SOURCES test/suites/mailbox.c)
add_test_suite(wait-context SOURCES test/suites/wait_context.c)
add_test_suite(interrupt-state SOURCES test/suites/interrupt_state.c)
# External interrupts through the PLIC, and the stack the traps run on. The
# test-only link wraps of schedule() and tkmc_timer_handler() keep the mcause of
# the trap that switches to a task the handler woke, and where the C part of
# each trap ran; the wrap of tkmc_finish_external_interrupt() records whether
# mstatus.MIE is on when the completion of an interrupt starts and ends.
add_test_suite(
  interrupt
  SOURCES
  test/suites/interrupt.c
  test/panic_fail.c
  test/spin_with_registers.S
  test/asm_interrupt_entries.S
  WRAP
  schedule
  tkmc_timer_handler
  tkmc_finish_external_interrupt)
if(QEMU_EXECUTABLE)
  add_test(
    NAME interrupt-completion-trace-self-test
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/test/check_interrupt_completion.py
      --self-test)
endif()
add_completion_trace(qemu-interrupt-completion-trace interrupt)
# TA_ASM task entry and termination. Its completion trace checks that no
# interrupt completes while the tasks run.
add_test_suite(asm-task SOURCES test/suites/asm_task.c test/asm_task_entries.S)
add_completion_trace(qemu-asm-task-trace asm-task)
# External interrupts nested 3 to 7 levels deep and the interrupt stack they
# take, one ELF per depth so that a depth that overflows the stack fails alone.
# Each depth also gets the PLIC completion trace check.
foreach(depth RANGE 3 7)
  set(suite interrupt-depth-${depth})
  add_test_suite(${suite} SOURCES test/suites/interrupt_depth.c
                 test/interrupt_depth_entries.S WRAP schedule)
  target_compile_definitions(${PROJECT_NAME}-test-${suite}.elf
                             PRIVATE NEST_DEPTH=${depth})
  add_completion_trace(qemu-${suite}-trace ${suite})
endforeach()
add_test_suite(idle-halt ICOUNT_ONLY SOURCES test/suites/idle_halt.c)

# How long a mailbox call keeps interrupts disabled, from the guest's
# retired-instruction counter.
add_test_suite(mailbox-cost ICOUNT_ONLY SOURCES test/suites/mailbox_cost.c)

# How long the timer interrupt handler runs when many timed waits end in one
# tick, from the guest's retired-instruction counter. The test-only link wrap
# reads the counter around the handler on the trap path.
add_test_suite(timer-cost ICOUNT_ONLY SOURCES test/suites/timer_cost.c WRAP
               tkmc_timer_handler)

# The same at the kernel's limit, every task but idle waiting, with no runner:
# the suite prints the marker itself when every check held.
add_test_suite(
  timer-cost-max
  ICOUNT_ONLY
  SOURCES
  test/suites/timer_cost_max.c
  OUTPUT_REGEX
  "TIMER COST MAX OK"
  WRAP
  tkmc_timer_handler)

# tk_wup_tsk() from the actual timer trap: the test-only link wrap runs the call
# on the trap path, in the task the trap interrupted.
add_test_suite(wakeup-context SOURCES test/suites/wakeup_context.c
               test/panic_fail.c WRAP tkmc_timer_handler)

# Which trap makes a task switch: the test-only link wrap of schedule() keeps
# the mcause of the trap that ran it.
add_test_suite(timer-switch SOURCES test/suites/timer_switch.c WRAP schedule)

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
endif()
