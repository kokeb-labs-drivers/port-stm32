# The firmware check the cross builds run after linking (examples/stm32, POST_BUILD): every interrupt vector the
# port's vector object defines, and every HAL callback the port's library defines, must be a strong definition in
# the linked ELF, at an address other than Default_Handler's. A vector left weak means the interrupt runs the
# startup file's Default_Handler (an endless loop): the firmware links and hangs at the first interrupt.
#   cmake -DNM=<arm-none-eabi-nm> -DELF=<firmware.elf> -DVECTORS=<vector object> -DLIBRARY=<libkb_port_stm32.a>
#         -P tools/check_vectors.cmake
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The port-stm32 authors
foreach(_v NM ELF VECTORS LIBRARY)
  if(NOT DEFINED ${_v})
    message(FATAL_ERROR "[check_vectors] -D${_v}= is required")
  endif()
endforeach()

function(_kb_nm file out)
  execute_process(COMMAND "${NM}" "${file}" OUTPUT_VARIABLE _text RESULT_VARIABLE _rc ERROR_QUIET)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "[check_vectors] ${NM} ${file} failed")
  endif()
  string(REPLACE "\n" ";" _lines "${_text}")
  set(${out} "${_lines}" PARENT_SCOPE)
endfunction()

# The names to check: the vectors of the object, the HAL callbacks of the library (defined, type T).
set(_wanted "")
_kb_nm("${VECTORS}" _vector_lines)
foreach(_line IN LISTS _vector_lines)
  if(_line MATCHES "^[0-9a-fA-F]+ T ([A-Za-z0-9_]+_IRQHandler)$")
    list(APPEND _wanted "${CMAKE_MATCH_1}")
  endif()
endforeach()
_kb_nm("${LIBRARY}" _library_lines)
foreach(_line IN LISTS _library_lines)
  if(_line MATCHES "^[0-9a-fA-F]+ T (HAL_[A-Za-z0-9_]+Callback)$")
    list(APPEND _wanted "${CMAKE_MATCH_1}")
  endif()
endforeach()
list(REMOVE_DUPLICATES _wanted)
list(LENGTH _wanted _count)
if(_count EQUAL 0)
  message(FATAL_ERROR "[check_vectors] no vector or callback found in ${VECTORS} and ${LIBRARY}")
endif()

# What the ELF has: name -> "type address".
_kb_nm("${ELF}" _elf_lines)
set(_default "")
foreach(_line IN LISTS _elf_lines)
  if(_line MATCHES "^([0-9a-fA-F]+) ([A-Za-z]) ([A-Za-z0-9_]+)$")
    set(_type_${CMAKE_MATCH_3} "${CMAKE_MATCH_2}")
    set(_addr_${CMAKE_MATCH_3} "${CMAKE_MATCH_1}")
    if(CMAKE_MATCH_3 STREQUAL "Default_Handler")
      set(_default "${CMAKE_MATCH_1}")
    endif()
  endif()
endforeach()

set(_bad "")
foreach(_name IN LISTS _wanted)
  if(NOT DEFINED _type_${_name})
    list(APPEND _bad "${_name} (missing)")
  elseif(NOT _type_${_name} STREQUAL "T")
    list(APPEND _bad "${_name} (${_type_${_name}}, weak)")
  elseif(_default AND (_addr_${_name} STREQUAL _default))
    list(APPEND _bad "${_name} (is Default_Handler)")
  endif()
endforeach()
if(_bad)
  string(REPLACE ";" "\n  " _list "${_bad}")
  message(FATAL_ERROR "[check_vectors] ${ELF}: not served by the port:\n  ${_list}")
endif()
message(STATUS "[check_vectors] ${ELF}: ${_count} vectors and HAL callbacks are the port's strong definitions")
