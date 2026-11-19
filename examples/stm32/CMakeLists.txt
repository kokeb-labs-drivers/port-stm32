# STM32 example firmware of the port: one CMake project for the four families, the device chosen by EXAMPLE_BOARD
# (boards/<board>/: linker script, HAL configuration, pins). The HAL, CMSIS and the startup file come from
# STMicroelectronics' repositories at pinned commits (cmake/kb_port_stm32_hal.cmake of the port).
#   cmake -S examples/stm32 -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DEXAMPLE_BOARD=g4
#   (h7 also needs -DKB_ARM_CORE=cortex-m7f; -DEXAMPLE_RTOS=FREERTOS runs it as a task under FreeRTOS)
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The port-stm32 authors
cmake_minimum_required(VERSION 3.25)
project(kb_port_stm32_examples C CXX ASM)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(EXAMPLE_BOARD "g4" CACHE STRING "g4 (STM32G474), h7 (STM32H743), f4 (STM32F446) or l4 (STM32L476)")
set(_devices g4 STM32G474xx h7 STM32H743xx f4 STM32F446xx l4 STM32L476xx)
list(FIND _devices "${EXAMPLE_BOARD}" _i)
if(_i LESS 0)
  message(FATAL_ERROR "EXAMPLE_BOARD must be g4, h7, f4 or l4, not '${EXAMPLE_BOARD}'")
endif()
math(EXPR _i "${_i} + 1")
list(GET _devices ${_i} _device)
set(_board "${CMAKE_CURRENT_LIST_DIR}/boards/${EXAMPLE_BOARD}")
set(KB_PORT_STM32_DEVICE "${_device}" CACHE STRING "" FORCE)
set(KB_PORT_STM32_HAL_CONF_DIR "${_board}" CACHE PATH "" FORCE)
set(EXAMPLE_RTOS "NONE" CACHE STRING "NONE (one loop, kb::rtos's none backend) or FREERTOS (a task)")

get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(EXAMPLE_RTOS STREQUAL "FREERTOS")
  include("${_root}/cmake/kb_port_stm32_freertos.cmake")
  kb_port_stm32_freertos("${CMAKE_CURRENT_LIST_DIR}/freertos") # before the port: kb-core takes KB_RTOS from it
elseif(NOT EXAMPLE_RTOS STREQUAL "NONE")
  message(FATAL_ERROR "EXAMPLE_RTOS must be NONE or FREERTOS, not '${EXAMPLE_RTOS}'")
endif()
add_subdirectory("${_root}" kb_port_stm32)

add_executable(port_selftest src/port_selftest.cpp "${KB_PORT_STM32_STARTUP}")
target_include_directories(port_selftest PRIVATE src "${_board}")
target_link_libraries(port_selftest PRIVATE kb::port_stm32)
target_compile_options(port_selftest PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics
                       -Wall -Wextra -Werror>)
# The newlib-nano C library without system calls, unused sections dropped; the core flags come from the toolchain
# file (CMAKE_C_FLAGS), which the startup file is assembled with too (a toolchain file may set no ASM flags).
target_link_options(port_selftest PRIVATE "-T${_board}/linker.ld" "-Wl,-Map=$<TARGET_FILE_DIR:port_selftest>/port_selftest.map"
                    -specs=nano.specs -specs=nosys.specs -Wl,--gc-sections)
separate_arguments(_core_flags UNIX_COMMAND "${CMAKE_C_FLAGS}")
set_source_files_properties("${KB_PORT_STM32_STARTUP}" PROPERTIES COMPILE_OPTIONS "${_core_flags}")
set_target_properties(port_selftest PROPERTIES SUFFIX ".elf")
if(EXAMPLE_RTOS STREQUAL "FREERTOS")
  target_sources(port_selftest PRIVATE src/freertos_start.c)
  target_link_libraries(port_selftest PRIVATE kb_port_stm32_freertos_kernel)
  target_compile_definitions(port_selftest PRIVATE EXAMPLE_RTOS_FREERTOS=1)
endif()
# After the link: every vector and HAL callback the port defines is in the firmware as a strong symbol, not the
# startup file's Default_Handler (tools/check_vectors.cmake). A failure fails the build, in CI and locally.
add_custom_command(TARGET port_selftest POST_BUILD
  COMMAND "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}" "-DELF=$<TARGET_FILE:port_selftest>"
          "-DVECTORS=$<TARGET_OBJECTS:kb_port_stm32_vectors>" "-DLIBRARY=$<TARGET_FILE:kb_port_stm32>"
          -P "${_root}/tools/check_vectors.cmake"
  COMMAND_EXPAND_LISTS VERBATIM)
