# A toolchain file for the Arm GNU toolchain (arm-none-eabi-gcc on PATH). KB_ARM_CORE selects the core flags:
# cortex-m4f (G4, F4, L4; the default) or cortex-m7f (H7). It sets what CI's cross builds set (ci-tools
# cmake_matrix), so a local build is the CI build; the example project adds the link options itself.
#   cmake -S examples/stm32 -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DKB_ARM_CORE=cortex-m7f ...
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The port-stm32 authors
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
if(NOT DEFINED KB_ARM_CORE)
  set(KB_ARM_CORE cortex-m4f)
endif()
if(KB_ARM_CORE STREQUAL "cortex-m7f")
  set(_cpu "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")
else()
  set(_cpu "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
endif()
set(CMAKE_C_FLAGS_INIT "${_cpu} -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${_cpu} -ffunction-sections -fdata-sections")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
