# The FreeRTOS kernel for a firmware that runs the port under KB_RTOS=FREERTOS (examples/stm32 with
# EXAMPLE_RTOS=FREERTOS): FreeRTOS-Kernel fetched unchanged at a pinned tag and commit (SOUP, docs/soup.md), built as
# the static library kb_port_stm32_freertos_kernel with the Cortex-M4F or Cortex-M7 (r0p1) GCC port, static
# allocation only. kb-core's FreeRTOS backend includes the kernel as "freertos/<header>.h" (the ESP-IDF layout): a
# folder of one-line headers in the build tree gives it that layout over the upstream one.
#   kb_port_stm32_freertos(<folder of FreeRTOSConfig.h>)   before add_subdirectory(<port>)
# Sets KB_RTOS=FREERTOS and KB_FREERTOS_INCLUDE_DIRS for kb-core. FETCHCONTENT_SOURCE_DIR_KB_FREERTOS may point at a
# local clone of the same commit; its commit is checked too.
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The port-stm32 authors
include_guard(GLOBAL)
include(FetchContent)

set(_kb_freertos_tag V11.1.0)
set(_kb_freertos_commit dbf70559b27d39c1fdb68dfb9a32140b6a6777a0)

function(kb_port_stm32_freertos config_dir)
  # SOURCE_SUBDIR names no folder: the kernel's own CMakeLists.txt is not run; the files the port needs are built
  # below.
  FetchContent_Declare(kb_freertos GIT_REPOSITORY "https://github.com/FreeRTOS/FreeRTOS-Kernel.git"
                       GIT_TAG "${_kb_freertos_commit}" GIT_PROGRESS OFF SOURCE_SUBDIR kb-port-stm32-none)
  FetchContent_MakeAvailable(kb_freertos)
  execute_process(COMMAND git -C "${kb_freertos_SOURCE_DIR}" rev-parse HEAD OUTPUT_VARIABLE _head
                  OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0 OR NOT _head STREQUAL _kb_freertos_commit)
    message(FATAL_ERROR "[kb-port-stm32] FreeRTOS-Kernel is at '${_head}', not the pinned ${_kb_freertos_tag}")
  endif()
  message(STATUS "[kb-port-stm32] FreeRTOS-Kernel ${_kb_freertos_tag} (${_kb_freertos_commit})")
  # The core from this repository's toolchain file (KB_ARM_CORE) or from any toolchain's flags (CI's).
  if((KB_ARM_CORE STREQUAL "cortex-m7f") OR (CMAKE_C_FLAGS MATCHES "cortex-m7"))
    set(_port "${kb_freertos_SOURCE_DIR}/portable/GCC/ARM_CM7/r0p1")
  else()
    set(_port "${kb_freertos_SOURCE_DIR}/portable/GCC/ARM_CM4F")
  endif()
  set(_shim "${CMAKE_BINARY_DIR}/kb_freertos_shim")
  foreach(_h IN ITEMS FreeRTOS task queue semphr event_groups timers)
    file(WRITE "${_shim}/freertos/${_h}.h.in" "/* Generated: the ESP-IDF include layout over FreeRTOS-Kernel (the kernel folder is searched, not this one). */\n#include <${_h}.h>\n")
    configure_file("${_shim}/freertos/${_h}.h.in" "${_shim}/freertos/${_h}.h" COPYONLY)
  endforeach()
  set(_includes "${_shim}" "${kb_freertos_SOURCE_DIR}/include" "${_port}" "${config_dir}")
  add_library(kb_port_stm32_freertos_kernel STATIC
    "${kb_freertos_SOURCE_DIR}/tasks.c" "${kb_freertos_SOURCE_DIR}/queue.c" "${kb_freertos_SOURCE_DIR}/list.c"
    "${kb_freertos_SOURCE_DIR}/timers.c" "${kb_freertos_SOURCE_DIR}/event_groups.c" "${_port}/port.c")
  target_include_directories(kb_port_stm32_freertos_kernel SYSTEM PUBLIC ${_includes})
  target_compile_options(kb_port_stm32_freertos_kernel PRIVATE -w) # SOUP: its own warnings, not the port's
  set(KB_RTOS FREERTOS CACHE STRING "kb-core RTOS backend" FORCE)
  # The port's interrupt-priority check takes the kernel's syscall limit from the same FreeRTOSConfig.h.
  include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/kb_port_stm32_rtos.cmake")
  kb_port_stm32_freertos_min_priority("${config_dir}/FreeRTOSConfig.h" _min)
  if(_min STREQUAL "")
    message(FATAL_ERROR "[kb-port-stm32] ${config_dir}/FreeRTOSConfig.h: no configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY")
  endif()
  set(KB_PORT_STM32_MIN_RTOS_IRQ_PRIORITY "${_min}" CACHE STRING "Lowest NVIC priority of an RTOS-calling interrupt" FORCE)
  set(KB_FREERTOS_INCLUDE_DIRS "${_includes}" CACHE STRING "FreeRTOS include folders for kb-core" FORCE)
endfunction()
