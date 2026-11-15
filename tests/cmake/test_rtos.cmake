# Configure-time logic test (ctest kb_port_stm32_rtos_config): the blocking-wait define follows every spelling of
# KB_RTOS, and the RTOS priority limit is read from a FreeRTOSConfig.h. Test code.
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The port-stm32 authors
include("${ROOT}/cmake/kb_port_stm32_rtos.cmake")
foreach(_case IN ITEMS "HOST;TRUE" "host;TRUE" "FREERTOS;TRUE" "freertos;TRUE" "FreeRTOS;TRUE" "NONE;FALSE" "none;FALSE"
              ";FALSE")
  list(GET _case 0 _rtos)
  list(LENGTH _case _n)
  if(_n EQUAL 1)
    set(_rtos "")
    list(GET _case 0 _want)
  else()
    list(GET _case 1 _want)
  endif()
  kb_port_stm32_rtos_blocks("${_rtos}" _got)
  if(NOT _got STREQUAL _want)
    message(FATAL_ERROR "KB_RTOS '${_rtos}': blocks ${_got}, want ${_want}")
  endif()
endforeach()
kb_port_stm32_freertos_min_priority("${ROOT}/examples/stm32/freertos/FreeRTOSConfig.h" _min)
if(NOT _min STREQUAL "5")
  message(FATAL_ERROR "configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY read as '${_min}', want 5")
endif()
message(STATUS "rtos configuration logic: ok")
