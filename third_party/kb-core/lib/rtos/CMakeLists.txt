# kb::rtos: depends on kb::utils. The typed API is header-only (kb_rtos_api); KB_RTOS selects the one backend that
# kb::rtos links (lib/rtos/README.md):
#   NONE      backends/none/          no scheduler, deterministic (host default)
#   HOST      host/rtos/              std::thread, PC tests only, outside the MISRA claim
#   FREERTOS  backends/freertos/      the boundary files; ESP-IDF provides the kernel, elsewhere KB_FREERTOS_INCLUDE_DIRS
#   THREADX   planned (backends/threadx/README.md)
# With KB_BUILD_TESTS the none backend, the FreeRTOS boundary over the fake kernel (tests/fakes/freertos) and, on a
# host build, the std::thread backend are all built, each tested against the same API tests.
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The kb-core authors
kb_add_part(rtos DEPENDS utils)

add_library(kb_rtos_api INTERFACE)
target_compile_features(kb_rtos_api INTERFACE cxx_std_20)
target_include_directories(kb_rtos_api INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(kb_rtos_api INTERFACE kb::utils)

add_subdirectory(backends/none)

# The FreeRTOS boundary over the fake kernel: the link-seam test target, and what the MISRA scans compile.
if(KB_BUILD_TESTS OR KB_RTOS STREQUAL "FREERTOS")
  add_subdirectory(backends/freertos)
endif()

string(TOLOWER "${KB_RTOS}" _kb_backend)
if(KB_RTOS STREQUAL "HOST" OR (KB_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING))
  add_subdirectory("${KB_ROOT}/host/rtos" "${CMAKE_CURRENT_BINARY_DIR}/host-backend")
endif()
target_link_libraries(kb_rtos INTERFACE kb_rtos_api kb_rtos_backend_${_kb_backend})
install(TARGETS kb_rtos_api kb_rtos_backend_${_kb_backend} EXPORT kb_rtosTargets)

kb_header_checks(rtos _kb_rtos_checks HEADERS
  kb/rtos/backend.hpp kb/rtos/critical_section.hpp kb/rtos/event_flags.hpp kb/rtos/mutex.hpp kb/rtos/queue.hpp
  kb/rtos/rtos.hpp kb/rtos/semaphore.hpp kb/rtos/thread.hpp kb/rtos/time.hpp kb/rtos/timer.hpp)
set_property(GLOBAL APPEND PROPERTY KB_CROSS_SOURCES ${_kb_rtos_checks}
  "${CMAKE_CURRENT_SOURCE_DIR}/backends/none/none_backend.cpp")
set_property(GLOBAL APPEND PROPERTY KB_CROSS_LIBS kb_rtos_api)
set_property(GLOBAL APPEND PROPERTY KB_CROSS_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/backends/none/inc")
if(KB_BUILD_TESTS)
  set_property(GLOBAL APPEND PROPERTY KB_CROSS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/backends/freertos/freertos_boundary.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/backends/freertos/freertos_isr.cpp")
  set_property(GLOBAL APPEND PROPERTY KB_CROSS_SYSTEM_INCLUDES "${KB_ROOT}/tests/fakes/freertos")
endif()

if(KB_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
  add_subdirectory(tests)
endif()
