# kb_port_stm32: kb-core's contracts over the STM32Cube HAL for the STM32 G4, H7, F4 and L4 families (kb-core
# ADR-0007, ADR-0008).
#   On target: cmake -S <app> ... with add_subdirectory(<this>) and
#              -DKB_PORT_STM32_DEVICE=STM32G474xx -DKB_PORT_STM32_HAL_CONF_DIR=<folder of stm32g4xx_hal_conf.h>
#              (an Arm GNU toolchain file); the HAL and CMSIS are fetched at pinned commits (cmake/kb_port_stm32_hal.cmake).
#   Off target: cmake -S . -B build -DKB_PORT_STM32_BUILD_TESTS=ON   host tests of the logic and the boundary files
#              against fakes of every HAL call they make (tests/fakes/stm32), in two family variants.
# kb-core comes from find_package(kb) when it is installed, else from the pinned copy in third_party/kb-core.
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The port-stm32 authors
cmake_minimum_required(VERSION 3.25)

project(kb_port_stm32 VERSION 0.1.0 DESCRIPTION "kb-core contracts over the STM32Cube HAL" LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KB_PORT_STM32_BUILD_TESTS "Build the host tests against the HAL fakes" OFF)
option(KB_PORT_STM32_VECTORS "Define the USART, EXTI and TIM2/TIM5 interrupt vectors the port serves" ON)
set(KB_PORT_STM32_DEVICE "" CACHE STRING "CMSIS device macro, e.g. STM32G474xx (empty: off target)")
set(KB_PORT_STM32_HAL_CONF_DIR "" CACHE PATH "Folder of the application's stm32<family>xx_hal_conf.h")

set(KB_PORT_STM32_LOGIC_SOURCES
    src/adc_pwm.cpp src/can_time_log.cpp src/gpio.cpp src/i2c_controller.cpp src/port.cpp src/spi_device.cpp src/uart.cpp)
# The only files that include HAL or CMSIS headers (deviations/boundary-files.yml).
set(KB_PORT_STM32_BOUNDARY_SOURCES src/stm32_bus_boundary.cpp src/stm32_can_boundary.cpp src/stm32_io_boundary.cpp)
set(KB_PORT_STM32_VECTOR_SOURCE src/stm32_vectors_boundary.cpp)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/kb_port_stm32_kb_core.cmake")

# The public headers: platform-free, no HAL type in a signature.
add_library(kb_port_stm32_api INTERFACE)
target_compile_features(kb_port_stm32_api INTERFACE cxx_std_20)
target_include_directories(kb_port_stm32_api INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>")
target_link_libraries(kb_port_stm32_api INTERFACE kb::interfaces kb::rtos kb::utils)

list(TRANSFORM KB_PORT_STM32_LOGIC_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/" OUTPUT_VARIABLE _kb_port_logic)
list(TRANSFORM KB_PORT_STM32_BOUNDARY_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/" OUTPUT_VARIABLE _kb_port_boundary)

# The platform-free logic alone (no HAL header anywhere): the poison build and the Cortex-M cross build.
add_library(kb_port_stm32_platform_free OBJECT EXCLUDE_FROM_ALL ${_kb_port_logic})
target_compile_options(kb_port_stm32_platform_free PRIVATE ${KB_PORT_STM32_STRICT_FLAGS})
target_include_directories(kb_port_stm32_platform_free PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(kb_port_stm32_platform_free PRIVATE kb_port_stm32_api)

if(KB_PORT_STM32_DEVICE)
  include("${CMAKE_CURRENT_LIST_DIR}/cmake/kb_port_stm32_hal.cmake")
  kb_port_stm32_hal()
  if(NOT PROJECT_IS_TOP_LEVEL)
    set(KB_PORT_STM32_FAMILY "${KB_PORT_STM32_FAMILY}" PARENT_SCOPE)
    set(KB_PORT_STM32_STARTUP "${KB_PORT_STM32_STARTUP}" PARENT_SCOPE)
  endif()
  set(_sources ${_kb_port_logic} ${_kb_port_boundary})
  if(KB_PORT_STM32_VECTORS)
    list(APPEND _sources "${CMAKE_CURRENT_SOURCE_DIR}/${KB_PORT_STM32_VECTOR_SOURCE}")
  endif()
  add_library(kb_port_stm32 STATIC ${_sources})
  add_library(kb::port_stm32 ALIAS kb_port_stm32)
  target_include_directories(kb_port_stm32 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(kb_port_stm32 PUBLIC kb_port_stm32_api kb_port_stm32_hal)
  # The logic with kb-core's strict flags; the boundary files without the conversion and pedantic warnings, which
  # the HAL's and CMSIS's own headers and macros do not build cleanly with (the host build of the boundary files
  # over the fakes has every warning).
  set_source_files_properties(${_kb_port_logic} PROPERTIES COMPILE_OPTIONS "${KB_PORT_STM32_STRICT_FLAGS}")
  target_compile_options(kb_port_stm32 PRIVATE -fno-exceptions -fno-rtti -Wall -Wextra -Werror)
else()
  add_library(kb::port_stm32 ALIAS kb_port_stm32_api)
endif()

if(KB_PORT_STM32_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
