/**
 * @file version.hpp
 * @brief The version of the STM32 port (CMakeLists.txt carries the same).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_VERSION_HPP
#define KB_PORT_STM32_VERSION_HPP

#include <cstdint>

namespace kb::port::stm32 {

inline constexpr uint32_t kVersionMajor = 0U; ///< Major version.
inline constexpr uint32_t kVersionMinor = 1U; ///< Minor version.
inline constexpr uint32_t kVersionPatch = 0U; ///< Patch version.
/** @brief The version as text. */
inline constexpr const char* kVersion = "0.1.0";
/** @brief The HAL line this port is written and tested for. */
inline constexpr const char* kSdkLine = "STM32Cube HAL (G4, H7, F4, L4)";

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_VERSION_HPP
