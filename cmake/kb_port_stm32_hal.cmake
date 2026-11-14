# The STM32Cube HAL and CMSIS of one device, fetched unchanged from STMicroelectronics' repositories at a pinned
# tag and commit (SOUP, docs/soup.md), and built as the static library kb_port_stm32_hal.
#
#   KB_PORT_STM32_DEVICE        the CMSIS device macro, e.g. STM32G474xx, STM32H743xx, STM32F446xx, STM32L476xx
#   KB_PORT_STM32_HAL_CONF_DIR  the folder of the application's stm32<family>xx_hal_conf.h (required)
#
# FetchContent takes each repository at the commit below (never a branch): FETCHCONTENT_SOURCE_DIR_<NAME> may point
# at a local clone of the same commit (offline builds); the commit of such a folder is checked too.
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The port-stm32 authors
include_guard(GLOBAL)
include(FetchContent)

# family: (hal repository, tag, commit), (CMSIS device repository, tag, commit)
set(_kb_hal_G4 stm32g4xx-hal-driver v1.2.7 f5929f431f9effe45fbe18f5337e4753ced9ac92)
set(_kb_dev_G4 cmsis-device-g4 v1.2.6 25664ddc3a7624ae9627ae8c4c672073dc5b2539)
set(_kb_hal_H7 stm32h7xx-hal-driver v1.11.7 65e5c7e40456f4d76d79f972738a16cb3a17762c)
set(_kb_dev_H7 cmsis-device-h7 v1.10.8 277b283ad9ab79ea85ee42920e371b019d7defb0)
set(_kb_hal_F4 stm32f4xx-hal-driver v1.8.6 6ef30cf0ef6cc29ad518f37b9e6de89b6b815f90)
set(_kb_dev_F4 cmsis-device-f4 v2.6.11 0fa0e489e053fa1ca7790bb40b4d76458f64c55d)
set(_kb_hal_L4 stm32l4xx-hal-driver v1.13.6 047a8610c7dc0dcfdb4cd63b86cd3a2cbd3e5899)
set(_kb_dev_L4 cmsis-device-l4 v1.7.5 dc04e41afbd7c204cd09f2fa5dd29658efbaff6c)
set(_kb_core cmsis-core v5.9.0_20250520 b7487de1303e2eb77c402432e368691e9dcfb5f0)

# The HAL modules the port uses, by file stem; a stem the family does not have is skipped.
set(_kb_hal_modules hal hal_cortex hal_rcc hal_rcc_ex hal_gpio hal_dma hal_dma_ex hal_pwr hal_pwr_ex hal_i2c hal_i2c_ex
    hal_spi hal_spi_ex hal_uart hal_uart_ex hal_adc hal_adc_ex hal_tim hal_tim_ex hal_fdcan hal_can hal_exti)

function(_kb_port_stm32_fetch name repo tag commit)
  string(TOUPPER "${name}" _upper)
  FetchContent_Declare(${name} GIT_REPOSITORY "https://github.com/STMicroelectronics/${repo}.git" GIT_TAG "${commit}"
                       GIT_PROGRESS OFF)
  FetchContent_MakeAvailable(${name}) # none of the three repositories has a CMakeLists.txt: fetch only
  execute_process(COMMAND git -C "${${name}_SOURCE_DIR}" rev-parse HEAD OUTPUT_VARIABLE _head
                  OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0 OR NOT _head STREQUAL commit)
    message(FATAL_ERROR "[kb-port-stm32] ${repo} is at '${_head}', not the pinned ${tag} (${commit})")
  endif()
  set(${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
  message(STATUS "[kb-port-stm32] ${repo} ${tag} (${commit})")
endfunction()

function(kb_port_stm32_hal)
  if(NOT KB_PORT_STM32_DEVICE MATCHES "^STM32(G4|H7|F4|L4)[0-9A-Z]+xx$")
    message(FATAL_ERROR "[kb-port-stm32] KB_PORT_STM32_DEVICE '${KB_PORT_STM32_DEVICE}' is not a G4, H7, F4 or L4 "
                        "device macro (for example STM32G474xx)")
  endif()
  set(family "${CMAKE_MATCH_1}")
  string(TOLOWER "${family}" low)
  if(NOT EXISTS "${KB_PORT_STM32_HAL_CONF_DIR}/stm32${low}xx_hal_conf.h")
    message(FATAL_ERROR "[kb-port-stm32] KB_PORT_STM32_HAL_CONF_DIR must hold stm32${low}xx_hal_conf.h")
  endif()
  _kb_port_stm32_fetch(kb_stm32_hal ${_kb_hal_${family}})
  _kb_port_stm32_fetch(kb_stm32_device ${_kb_dev_${family}})
  _kb_port_stm32_fetch(kb_cmsis_core ${_kb_core})

  set(sources "")
  foreach(m IN LISTS _kb_hal_modules)
    set(f "${kb_stm32_hal_SOURCE_DIR}/Src/stm32${low}xx_${m}.c")
    if(EXISTS "${f}")
      list(APPEND sources "${f}")
    endif()
  endforeach()
  list(APPEND sources "${kb_stm32_device_SOURCE_DIR}/Source/Templates/system_stm32${low}xx.c")
  add_library(kb_port_stm32_hal STATIC ${sources})
  target_include_directories(kb_port_stm32_hal SYSTEM PUBLIC "${KB_PORT_STM32_HAL_CONF_DIR}"
    "${kb_stm32_hal_SOURCE_DIR}/Inc" "${kb_stm32_device_SOURCE_DIR}/Include" "${kb_cmsis_core_SOURCE_DIR}/Include")
  target_compile_definitions(kb_port_stm32_hal PUBLIC ${KB_PORT_STM32_DEVICE} USE_HAL_DRIVER KB_PORT_STM32_${family})
  # The HAL's own sources are SOUP: built with their own warnings, not the port's.
  target_compile_options(kb_port_stm32_hal PRIVATE -w)

  string(TOLOWER "${KB_PORT_STM32_DEVICE}" dev)
  set(KB_PORT_STM32_FAMILY "${family}" PARENT_SCOPE)
  set(KB_PORT_STM32_STARTUP "${kb_stm32_device_SOURCE_DIR}/Source/Templates/gcc/startup_${dev}.s" PARENT_SCOPE)
endfunction()
