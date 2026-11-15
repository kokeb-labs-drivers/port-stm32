// Boundary file (deviations/boundary-files.yml): error.hpp names the HAL's HAL_StatusTypeDef
// values; a change in the HAL stops the build here. SPDX-License-Identifier: MIT Copyright (c) 2026
// The port-stm32 authors
#include <cstdint>

#include "kb/port/stm32/error.hpp"
#if defined(KB_PORT_STM32_G4)
#include "stm32g4xx_hal.h"
#elif defined(KB_PORT_STM32_H7)
#include "stm32h7xx_hal.h"
#elif defined(KB_PORT_STM32_F4)
#include "stm32f4xx_hal.h"
#elif defined(KB_PORT_STM32_L4)
#include "stm32l4xx_hal.h"
#else
#error "define one of KB_PORT_STM32_G4, KB_PORT_STM32_H7, KB_PORT_STM32_F4, KB_PORT_STM32_L4"
#endif

namespace kb::port::stm32 {

static_assert(hal::kOk == static_cast<uint32_t>(HAL_OK));
static_assert(hal::kError == static_cast<uint32_t>(HAL_ERROR));
static_assert(hal::kBusy == static_cast<uint32_t>(HAL_BUSY));
static_assert(hal::kTimeout == static_cast<uint32_t>(HAL_TIMEOUT));

} // namespace kb::port::stm32
