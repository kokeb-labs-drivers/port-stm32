# kb-core for the host build: the installed package when CMAKE_PREFIX_PATH finds it, else the pinned copy in
# third_party/kb-core, whose every file is first checked against third_party/kb-core.lock (a hand edit or a
# missing file stops the configure). Defines KB_PORT_STM32_KB_CORE_DIR (the copy, for the test harness) and the
# strict flags of the port's own translation units.
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The port-stm32 authors
include_guard(GLOBAL)

set(KB_PORT_STM32_KB_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/kb-core")
get_filename_component(KB_PORT_STM32_KB_CORE_DIR "${KB_PORT_STM32_KB_CORE_DIR}" ABSOLUTE)
set(_kb_lock "${CMAKE_CURRENT_LIST_DIR}/../third_party/kb-core.lock")

function(kb_port_stm32_check_kb_core)
  file(STRINGS "${_kb_lock}" _lines)
  set(_count 0)
  foreach(_line IN LISTS _lines)
    if(_line MATCHES "^([0-9a-f]+)  (.+)$")
      set(_file "${KB_PORT_STM32_KB_CORE_DIR}/${CMAKE_MATCH_2}")
      if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "[kb-port-stm32] third_party/kb-core/${CMAKE_MATCH_2} is missing (tools/kb_core_snapshot.py)")
      endif()
      file(SHA256 "${_file}" _hash)
      if(NOT _hash STREQUAL CMAKE_MATCH_1)
        message(FATAL_ERROR "[kb-port-stm32] third_party/kb-core/${CMAKE_MATCH_2} differs from the lock; the copy is "
                            "never edited by hand (tools/kb_core_snapshot.py --update)")
      endif()
      math(EXPR _count "${_count} + 1")
    elseif(_line MATCHES "^commit ([0-9a-f]+)$")
      set(_commit "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  message(STATUS "[kb-port-stm32] kb-core copy: ${_count} files equal the lock at ${_commit}")
endfunction()

if(NOT TARGET kb::interfaces)
  find_package(kb CONFIG QUIET COMPONENTS interfaces dma rtos utils)
endif()
if(NOT TARGET kb::interfaces)
  kb_port_stm32_check_kb_core()
  set(KB_PARTS interfaces dma CACHE STRING "kb-core parts for the port (interfaces, dma and their dependencies)")
  set(KB_QUIET ON)
  add_subdirectory("${KB_PORT_STM32_KB_CORE_DIR}" "${CMAKE_BINARY_DIR}/kb-core" EXCLUDE_FROM_ALL SYSTEM)
else()
  message(STATUS "[kb-port-stm32] kb-core ${kb_VERSION} from the installed package")
endif()

# The flags of kb-core's own translation units (kb-core cmake/kb_testing.cmake), for the port's sources and tests.
set(KB_PORT_STM32_STRICT_FLAGS -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -pedantic-errors -Wshadow
    -Wconversion -Wsign-conversion -Wdeprecated -Wimplicit-fallthrough -Wswitch-enum -Wformat=2 -Wnull-dereference
    -Wundef -Wcast-qual -Wold-style-cast -Werror)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  list(APPEND KB_PORT_STM32_STRICT_FLAGS -Wmaybe-uninitialized -Wduplicated-cond -Wlogical-op)
endif()
