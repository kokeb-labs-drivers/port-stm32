// Boundary file (deviations/boundary-files.yml): the ITM stimulus port 0 of CMSIS behind sdk.hpp,
// for the ItmLogSink (log.hpp). Every decision is in the platform-free src/log.cpp.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include <bit>
#include <cstdint>

#include "sdk.hpp"
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

namespace kb::port::stm32::sdk {

// --- ITM -----------------------------------------------------------------------------------------
bool ItmEnabled() noexcept {
  const uint32_t tcr = ITM->TCR;
  const uint32_t ter = ITM->TER;
  return ((tcr & ITM_TCR_ITMENA_Msk) != 0U) && ((ter & 1U) != 0U);
}

bool ItmPortFree() noexcept {
  return ITM->PORT[0U].u32 != 0U;
}

void ItmWrite(char c) noexcept {
  ITM->PORT[0U].u8 = std::bit_cast<uint8_t>(c); // the same bits, no numeric conversion
}

} // namespace kb::port::stm32::sdk
