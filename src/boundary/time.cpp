// Boundary file (deviations/boundary-files.yml): the HAL tick, the core clock, the DWT cycle
// counter and the PRIMASK of the STM32 HAL and CMSIS behind sdk.hpp. The 64-bit clock's timer is in
// timer.cpp; every decision is in the platform-free src/time.cpp.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
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

// --- system --------------------------------------------------------------------------------------
uint32_t TickMs() noexcept {
  return HAL_GetTick();
}

uint32_t TickPeriodMs() noexcept {
  return static_cast<uint32_t>(HAL_GetTickFreq());
}

uint32_t CoreClockHz() noexcept {
  return SystemCoreClock;
}

uint32_t Cycles() noexcept {
  if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
#if defined(STM32H7)
    DWT->LAR = 0xC5ACCE55U; // the Cortex-M7 DWT is locked after reset
#endif
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; // CYCCNT itself is never written
  }
  return DWT->CYCCNT;
}

uint32_t IrqMask() noexcept {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

void IrqRestore(uint32_t saved) noexcept {
  __set_PRIMASK(saved);
}

} // namespace kb::port::stm32::sdk
