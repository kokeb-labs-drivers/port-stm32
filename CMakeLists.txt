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
option(KB_PORT_STM32_VECTORS "Define the interrupt vectors the port serves (USART, EXTI, TIM2/TIM5, I2C, SPI, ADC, CAN)" ON)
set(KB_PORT_STM32_VECTORS_EXCLUDE "" CACHE STRING
    "Vectors the application keeps for itself, e.g. USART1_IRQHandler;EXTI15_10_IRQHandler (docs/integration.md)")
set(KB_PORT_STM32_DEVICE "" CACHE STRING "CMSIS device macro, e.g. STM32G474xx (empty: off target)")
set(KB_PORT_STM32_HAL_CONF_DIR "" CACHE PATH "Folder of the application's stm32<family>xx_hal_conf.h")

# One source per public header (src/<stem>.cpp).
set(KB_PORT_STM32_LOGIC_SOURCES
    src/adc.cpp src/can.cpp src/completion.cpp src/error.cpp src/gpio.cpp src/i2c.cpp src/log.cpp src/port.cpp
    src/pwm.cpp src/spi.cpp src/time.cpp src/uart.cpp)
# The only files that include HAL or CMSIS headers (deviations/boundary-files.yml).
# One file per HAL component, in src/boundary/ (named after the header it serves); the vectors are a file of their own below.
set(KB_PORT_STM32_BOUNDARY_SOURCES
    src/boundary/adc.cpp src/boundary/can.cpp src/boundary/error.cpp src/boundary/gpio.cpp src/boundary/i2c.cpp
    src/boundary/log.cpp src/boundary/spi.cpp src/boundary/state.cpp src/boundary/time.cpp src/boundary/timer.cpp
    src/boundary/uart.cpp)
set(KB_PORT_STM32_VECTOR_SOURCE src/boundary/vectors.cpp)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/kb_port_stm32_kb_core.cmake")

# The public headers: platform-free, no HAL type in a signature.
add_library(kb_port_stm32_api INTERFACE)
target_compile_features(kb_port_stm32_api INTERFACE cxx_std_20)
target_include_directories(kb_port_stm32_api INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>")
target_link_libraries(kb_port_stm32_api INTERFACE kb::interfaces kb::rtos kb::utils)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/kb_port_stm32_rtos.cmake")
# Whether kb::rtos takes and sleeps block (completion.hpp: the bound of a wait when the clocks stop): from KB_RTOS in
# any spelling, or from the backend target an installed kb-core package brings.
kb_port_stm32_rtos_blocks("${KB_RTOS}" _kb_port_blocks)
foreach(_backend IN ITEMS kb_rtos_backend_freertos kb::rtos_backend_freertos kb_rtos_backend_host kb::rtos_backend_host)
  if(TARGET ${_backend})
    set(_kb_port_blocks TRUE)
  endif()
endforeach()
string(TOUPPER "${KB_RTOS}" _kb_port_rtos)
if(TARGET kb_rtos_backend_freertos OR TARGET kb::rtos_backend_freertos)
  set(_kb_port_rtos FREERTOS)
endif()
# The lowest NVIC priority number of an interrupt that gives a kb::rtos semaphore (error.hpp): under FreeRTOS
# configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (cmake/kb_port_stm32_freertos.cmake reads it from FreeRTOSConfig.h).
# EnsureReady() refuses a lower number (a more urgent interrupt). A FreeRTOS build must set it.
set(KB_PORT_STM32_MIN_RTOS_IRQ_PRIORITY "0" CACHE STRING "Lowest NVIC priority of an RTOS-calling interrupt (0-15)")
if((_kb_port_rtos STREQUAL "FREERTOS") AND (KB_PORT_STM32_MIN_RTOS_IRQ_PRIORITY STREQUAL "0"))
  message(FATAL_ERROR "[kb-port-stm32] KB_RTOS=FREERTOS: set KB_PORT_STM32_MIN_RTOS_IRQ_PRIORITY to "
                      "configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (kb_port_stm32_freertos() reads it)")
endif()
if(NOT KB_PORT_STM32_MIN_RTOS_IRQ_PRIORITY MATCHES "^([0-9]|1[0-5])$")
  message(FATAL_ERROR "[kb-port-stm32] KB_PORT_STM32_MIN_RTOS_IRQ_PRIORITY must be 0 to 15")
endif()
target_compile_definitions(kb_port_stm32_api INTERFACE
  "KB_PORT_STM32_MIN_RTOS_IRQ_PRIORITY=${KB_PORT_STM32_MIN_RTOS_IRQ_PRIORITY}U")
if(_kb_port_blocks)
  target_compile_definitions(kb_port_stm32_api INTERFACE KB_PORT_STM32_RTOS_BLOCKS=1)
endif()

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
  add_library(kb_port_stm32 STATIC ${_kb_port_logic} ${_kb_port_boundary})
  add_library(kb::port_stm32 ALIAS kb_port_stm32)
  target_include_directories(kb_port_stm32 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(kb_port_stm32 PUBLIC kb_port_stm32_api kb_port_stm32_hal)
  if(KB_PORT_STM32_VECTORS)
    # The vectors are strong definitions of names the startup file only references weakly, and a weak
    # reference never pulls a member out of an archive: in libkb_port_stm32.a they would be dropped
    # and every vector would stay Default_Handler. They are an object library whose object is added
    # to the sources of every executable that links the port, so the linker always takes it.
    add_library(kb_port_stm32_vectors OBJECT "${CMAKE_CURRENT_SOURCE_DIR}/${KB_PORT_STM32_VECTOR_SOURCE}")
    target_link_libraries(kb_port_stm32_vectors PRIVATE kb_port_stm32_api kb_port_stm32_hal)
    target_compile_options(kb_port_stm32_vectors PRIVATE -fno-exceptions -fno-rtti -Wall -Wextra -Werror)
    foreach(_vector IN LISTS KB_PORT_STM32_VECTORS_EXCLUDE)
      if(NOT _vector MATCHES "^[A-Za-z0-9_]+_IRQHandler$")
        message(FATAL_ERROR "[kb-port-stm32] KB_PORT_STM32_VECTORS_EXCLUDE: '${_vector}' is not a vector name")
      endif()
      target_compile_definitions(kb_port_stm32_vectors PRIVATE "KB_PORT_STM32_NO_${_vector}")
    endforeach()
    target_sources(kb_port_stm32 INTERFACE $<TARGET_OBJECTS:kb_port_stm32_vectors>)
  endif()
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
