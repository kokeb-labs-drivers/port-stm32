# What the port needs to know of the kb::rtos backend it is built with:
#   kb_port_stm32_rtos_blocks(<KB_RTOS value> <out>)  TRUE when takes and sleeps block (FreeRTOS, host), FALSE for
#                                                    none or an empty value; any spelling kb-core accepts
#   kb_port_stm32_freertos_min_priority(<FreeRTOSConfig.h> <out>)  configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY of it
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The port-stm32 authors
include_guard(GLOBAL)

function(kb_port_stm32_rtos_blocks rtos out)
  string(TOUPPER "${rtos}" _rtos)
  if(_rtos STREQUAL "FREERTOS" OR _rtos STREQUAL "HOST")
    set(${out} TRUE PARENT_SCOPE)
  elseif(_rtos STREQUAL "" OR _rtos STREQUAL "NONE")
    set(${out} FALSE PARENT_SCOPE)
  else()
    message(FATAL_ERROR "[kb-port-stm32] KB_RTOS '${rtos}': the port knows NONE, FREERTOS and HOST")
  endif()
endfunction()

function(kb_port_stm32_freertos_min_priority config out)
  set(_value "")
  if(EXISTS "${config}")
    file(STRINGS "${config}" _lines REGEX "^#define[ \t]+configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY[ \t]+")
    foreach(_line IN LISTS _lines)
      if(_line MATCHES "configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY[ \t]+\\(?([0-9]+)U?\\)?")
        set(_value "${CMAKE_MATCH_1}")
      endif()
    endforeach()
  endif()
  set(${out} "${_value}" PARENT_SCOPE)
endfunction()
