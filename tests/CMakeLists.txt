# Host tests of the STM32 port (docs/testing.md): the logic and the boundary files built unchanged against the HAL
# fakes (a link seam per HAL call) in two family variants, G4-like and F4-like; the concept and *Ref proofs; one
# translation unit per public header; and compile-fail tests of misuse.
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The port-stm32 authors
set(_harness "${KB_PORT_STM32_KB_CORE_DIR}/tests/harness")
set(_fakes "${CMAKE_CURRENT_SOURCE_DIR}/fakes/stm32")
list(TRANSFORM KB_PORT_STM32_LOGIC_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/" OUTPUT_VARIABLE _logic)
list(TRANSFORM KB_PORT_STM32_BOUNDARY_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/" OUTPUT_VARIABLE _boundary)
set(_vectors "${PROJECT_SOURCE_DIR}/${KB_PORT_STM32_VECTOR_SOURCE}")

# One variant: the fake HAL of a family and the port over it. `defines` selects the family.
function(kb_port_stm32_variant name)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "DEFINES")
  add_library(${name}_hal STATIC "${_fakes}/fake_stm32_hal.cpp")
  target_include_directories(${name}_hal PUBLIC "${_fakes}" "${_fakes}/include")
  target_compile_definitions(${name}_hal PUBLIC ${ARG_DEFINES})
  # The fake runs the port's interrupt entry points as the hardware would (irq.hpp).
  target_link_libraries(${name}_hal PRIVATE kb_port_stm32_api)
  target_compile_options(${name}_hal PRIVATE -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Wshadow -Wundef
                         -Werror)
  add_library(${name} STATIC ${_logic} ${_boundary} ${_vectors})
  target_compile_options(${name} PRIVATE ${KB_PORT_STM32_STRICT_FLAGS})
  target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/src")
  # The fake HAL headers stand where the HAL's would: -isystem, like the SDK on target.
  target_include_directories(${name} SYSTEM PRIVATE "${_fakes}/include")
  target_link_libraries(${name} PUBLIC kb_port_stm32_api ${name}_hal)
endfunction()

kb_port_stm32_variant(kb_port_stm32_g4 DEFINES KB_PORT_STM32_G4)
kb_port_stm32_variant(kb_port_stm32_f4 DEFINES KB_PORT_STM32_F4 KB_FAKE_STM32F4)
# What the MISRA scans build (ci-misra.yml): the port in both family variants.
add_custom_target(kb_port_stm32_scan)
add_dependencies(kb_port_stm32_scan kb_port_stm32_g4 kb_port_stm32_f4)

set(_tests test_error.cpp test_i2c.cpp test_spi.cpp test_uart.cpp test_gpio.cpp test_adc_pwm.cpp test_can.cpp
    test_time_log.cpp test_clock_read.cpp test_wait.cpp test_contracts.cpp)
foreach(_v IN ITEMS g4 f4)
  set(_name kb_port_stm32_tests_${_v})
  add_executable(${_name} "${_harness}/kb_test_main.cpp" ${_tests})
  target_compile_options(${_name} PRIVATE ${KB_PORT_STM32_STRICT_FLAGS})
  target_include_directories(${_name} PRIVATE "${_harness}" "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
  target_compile_definitions(${_name} PRIVATE _GLIBCXX_ASSERTIONS=1)
  target_link_libraries(${_name} PRIVATE kb_port_stm32_${_v})
  add_test(NAME ${_name} COMMAND ${_name})
endforeach()

# The configure-time RTOS logic (cmake/kb_port_stm32_rtos.cmake): every KB_RTOS spelling, the FreeRTOSConfig.h read.
add_test(NAME kb_port_stm32_rtos_config
         COMMAND ${CMAKE_COMMAND} "-DROOT=${PROJECT_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_rtos.cmake")

# Each public header compiles on its own as ISO C++20 with the strict flags.
set(_headers adc can error gpio i2c irq log port pwm spi time uart version)
set(_checks "")
foreach(_h IN LISTS _headers)
  set(_check "${CMAKE_CURRENT_BINARY_DIR}/header_checks/${_h}.cpp")
  file(WRITE "${_check}.in" "// Generated: kb/port/stm32/${_h}.hpp compiles on its own.\n#include \"kb/port/stm32/${_h}.hpp\"\n")
  configure_file("${_check}.in" "${_check}" COPYONLY)
  list(APPEND _checks "${_check}")
endforeach()
add_library(kb_port_stm32_header_checks OBJECT ${_checks})
target_compile_options(kb_port_stm32_header_checks PRIVATE ${KB_PORT_STM32_STRICT_FLAGS})
target_link_libraries(kb_port_stm32_header_checks PRIVATE kb_port_stm32_api)

# Compile-fail tests: each file must fail to build, for the reason in its "EXPECT: <regex>" line.
foreach(_file IN ITEMS ref_from_wrong_contract.cpp controller_copied.cpp output_pin_as_input.cpp)
  set(_src "${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/${_file}")
  get_filename_component(_stem "${_src}" NAME_WE)
  file(STRINGS "${_src}" _expect REGEX "EXPECT: ")
  string(REGEX REPLACE ".*EXPECT: " "" _expect "${_expect}")
  set(_target kb_port_stm32_compile_fail_${_stem})
  add_library(${_target} OBJECT EXCLUDE_FROM_ALL "${_src}")
  target_compile_options(${_target} PRIVATE ${KB_PORT_STM32_STRICT_FLAGS})
  target_link_libraries(${_target} PRIVATE kb_port_stm32_api)
  add_test(NAME ${_target} COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${_target})
  set_tests_properties(${_target} PROPERTIES PASS_REGULAR_EXPRESSION "${_expect}")
endforeach()
