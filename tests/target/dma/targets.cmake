# The DMA bench app (tests/target/dma/dma_bench.cpp), built by the example project (examples/stm32) for every board,
# bare metal: CI's STM32 family matrix compiles and links it; docs/assurance/bench/ says how to run it. The H7 build
# uses its own linker script with the AXI and D2 DMA sections (h7_dma.ld). Included from examples/stm32/CMakeLists.txt,
# which defines _root, _board and the startup file.
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The port-stm32 authors
if(EXAMPLE_RTOS STREQUAL "NONE")
  set(_bench "${_root}/tests/target/dma")
  add_executable(dma_bench "${_bench}/dma_bench.cpp" "${_bench}/atomic64.c" "${KB_PORT_STM32_STARTUP}")
  target_include_directories(dma_bench PRIVATE "${_board}" "${_root}/src")
  target_link_libraries(dma_bench PRIVATE kb::port_stm32)
  target_compile_options(dma_bench PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics
                         -Wall -Wextra -Werror> $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Werror>)
  if(EXAMPLE_BOARD STREQUAL "h7")
    set(_bench_ld "${_bench}/h7_dma.ld")
  else()
    set(_bench_ld "${_board}/linker.ld")
  endif()
  target_link_options(dma_bench PRIVATE "-T${_bench_ld}" "-Wl,-Map=$<TARGET_FILE_DIR:dma_bench>/dma_bench.map"
                      -specs=nano.specs -specs=nosys.specs -Wl,--gc-sections)
  set_target_properties(dma_bench PROPERTIES SUFFIX ".elf")
endif()
