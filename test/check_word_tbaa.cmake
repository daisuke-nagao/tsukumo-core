# SPDX-FileCopyrightText: 2026 Daisuke Nagao
#
# SPDX-License-Identifier: MIT

# Checks that the word loops of kernel/memcpy.c and kernel/memset.c access
# memory as "omnipotent char" in clang's type-based alias analysis (TBAA), the
# class that may alias any object. The functions copy and fill objects of any
# type; an access typed as "int" would let the optimizer assume that it does not
# touch, say, a float, and reorder around it.
#
# Usage: cmake -DCOMPILER=<clang> -DFLAGS=<;-list> -DOUT_DIR=<dir>
# -DSOURCES=<;-list> [-DREADERS=<;-list>] -P check_word_tbaa.cmake
#
# Every source must have word stores; the sources in READERS (file names without
# extension) must have word loads too.
#
# Each source is compiled at -O2 with the LLVM passes off: TBAA is attached by
# the front end at -O1 and above, and no pass can then merge, vectorize or turn
# the accesses into calls. With the passes off every local variable lives in an
# alloca and is loaded and stored with its own type, so accesses through an
# alloca are left out; what is left are the accesses through the loop pointers.

cmake_minimum_required(VERSION 3.22) # if(IN_LIST) in script mode

foreach(var COMPILER FLAGS OUT_DIR SOURCES)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "check_word_tbaa: ${var} is not set")
  endif()
endforeach()

# Checks the word access on `line`, whose pointer is CMAKE_MATCH_1, unless the
# pointer is an alloca, and counts it in `counter`. A macro, so that it reads
# and sets the variables of check_source(). The load and store patterns are not
# joined with OR in one if(): CMake evaluates both, and a failed MATCHES clears
# CMAKE_MATCH_n.
macro(check_access counter)
  if(NOT CMAKE_MATCH_1 IN_LIST allocas)
    math(EXPR ${counter} "${${counter}} + 1")
    if(NOT line MATCHES "!tbaa !([0-9]+)")
      message(FATAL_ERROR "check_word_tbaa: ${stem}: no TBAA tag:\n${line}")
    endif()
    set(access "${tag_${CMAKE_MATCH_1}}")
    set(name "${type_${access}}")
    if(NOT name STREQUAL "omnipotent char")
      message(
        FATAL_ERROR
          "check_word_tbaa: ${stem}: a word access is typed \"${name}\", "
          "not \"omnipotent char\":\n${line}")
    endif()
  endif()
endmacro()

function(check_source source)
  get_filename_component(stem "${source}" NAME_WE)
  set(ir_file "${OUT_DIR}/${stem}.tbaa.ll")
  execute_process(
    COMMAND "${COMPILER}" ${FLAGS} -O2 -Xclang -disable-llvm-passes
            -fno-discard-value-names -S -emit-llvm "${source}" -o "${ir_file}"
    RESULT_VARIABLE status
    ERROR_VARIABLE errors)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "check_word_tbaa: cannot compile ${source}:\n${errors}")
  endif()
  file(STRINGS "${ir_file}" lines)

  # Metadata nodes: an access tag !N = !{!base, !access, i64 offset} and the
  # type it accesses, !access = !{!"name", ...}. They are variables of this
  # function, so nothing of another source is left over.
  foreach(line IN LISTS lines)
    if(line MATCHES "^!([0-9]+) = !{![0-9]+, !([0-9]+), i64 [0-9]+}$")
      set(tag_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    elseif(line MATCHES "^!([0-9]+) = !{!\"([^\"]*)\"")
      set(type_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    endif()
  endforeach()

  set(loads 0)
  set(stores 0)
  set(allocas "")
  foreach(line IN LISTS lines)
    # SSA names start again in every function, and so do the allocas.
    if(line MATCHES "^define ")
      set(allocas "")
    elseif(line MATCHES "^ *(%[-a-zA-Z$._0-9]+) = alloca ")
      list(APPEND allocas "${CMAKE_MATCH_1}")
    elseif(line MATCHES "load i32, ptr (%[-a-zA-Z$._0-9]+)")
      check_access(loads)
    elseif(line MATCHES "store i32 [^,]+, ptr (%[-a-zA-Z$._0-9]+)")
      check_access(stores)
    endif()
  endforeach()

  # A pattern that matches nothing would pass any code.
  if(stores EQUAL 0)
    message(FATAL_ERROR "check_word_tbaa: ${stem}: no word store found")
  endif()
  if(stem IN_LIST READERS AND loads EQUAL 0)
    message(FATAL_ERROR "check_word_tbaa: ${stem}: no word load found")
  endif()
  message(STATUS "${stem}: ${loads} word loads and ${stores} word stores "
                 "are omnipotent char")
endfunction()

foreach(source IN LISTS SOURCES)
  check_source("${source}")
endforeach()
