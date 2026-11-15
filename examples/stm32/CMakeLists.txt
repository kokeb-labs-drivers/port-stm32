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
  # configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY of freertos/FreeRTOSConfig.h: the port checks its interrupts.
  set(KB_PORT_STM32_MIN_RTOS_IRQ_PRIORITY "5" CACHE STRING "" FORCE)
elseif(NOT EXAMPLE_RTOS STREQUAL "NONE")
  message(FATAL_ERROR "EXAMPLE_RTOS must be NONE or FREERTOS, not '${EXAMPLE_RTOS}'")
endif()
add_subdirectory("${_root}" kb_port_stm32)
foreach(_driver IN ITEMS pcal9555 tmc5160 bno08x)
  if(NOT EXISTS "${_root}/drivers/driver-${_driver}/CMakeLists.txt")
    message(FATAL_ERROR "drivers/driver-${_driver} is not checked out: git submodule update --init drivers")
  endif()
  add_subdirectory("${_root}/drivers/driver-${_driver}" "drivers/${_driver}" EXCLUDE_FROM_ALL SYSTEM)
endforeach()

add_executable(all_drivers src/all_drivers.cpp "${KB_PORT_STM32_STARTUP}")
target_include_directories(all_drivers PRIVATE src "${_board}")
target_link_libraries(all_drivers PRIVATE kb::port_stm32 pcal9555::pcal9555 tmc5160::tmc5160 bno08x::bno08x)
target_compile_options(all_drivers PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics
                       -Wall -Wextra -Werror>)
# The newlib-nano C library without system calls, unused sections dropped; the core flags come from the toolchain
# file (CMAKE_C_FLAGS), which the startup file is assembled with too (a toolchain file may set no ASM flags).
target_link_options(all_drivers PRIVATE "-T${_board}/linker.ld" "-Wl,-Map=$<TARGET_FILE_DIR:all_drivers>/all_drivers.map"
                    -specs=nano.specs -specs=nosys.specs -Wl,--gc-sections)
separate_arguments(_core_flags UNIX_COMMAND "${CMAKE_C_FLAGS}")
set_source_files_properties("${KB_PORT_STM32_STARTUP}" PROPERTIES COMPILE_OPTIONS "${_core_flags}")
set_target_properties(all_drivers PROPERTIES SUFFIX ".elf")
if(EXAMPLE_RTOS STREQUAL "FREERTOS")
  target_sources(all_drivers PRIVATE src/freertos_start.c)
  target_link_libraries(all_drivers PRIVATE kb_port_stm32_freertos_kernel)
  target_compile_definitions(all_drivers PRIVATE EXAMPLE_RTOS_FREERTOS=1)
endif()
# After the link: every vector and HAL callback the port defines is in the firmware as a strong symbol, not the
# startup file's Default_Handler (tools/check_vectors.cmake). A failure fails the build, in CI and locally.
add_custom_command(TARGET all_drivers POST_BUILD
  COMMAND "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}" "-DELF=$<TARGET_FILE:all_drivers>"
          "-DVECTORS=$<TARGET_OBJECTS:kb_port_stm32_vectors>" "-DLIBRARY=$<TARGET_FILE:kb_port_stm32>"
          -P "${_root}/tools/check_vectors.cmake"
  COMMAND_EXPAND_LISTS VERBATIM)
