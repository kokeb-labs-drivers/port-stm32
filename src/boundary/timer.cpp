// Boundary file (deviations/boundary-files.yml): the timer calls of the STM32 HAL behind sdk.hpp,
// for the PWM channels (pwm.hpp) and the clock's counter (time.hpp). Both use the TIM handles of
// the one table below, so they are one file: a timer is the clock's or PWM's, never both (the
// platform-free files enforce it). Each function converts its arguments, calls the HAL and returns
// VendorCode(HAL status, 0). Every decision is in src/pwm.cpp and src/time.cpp.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include <array>
#include <cstddef>
#include <cstdint>

#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/gpio.hpp"
#include "kb/port/stm32/pwm.hpp"
#include "kb/port/stm32/time.hpp"
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

namespace {
[[nodiscard]] uint32_t Code(HAL_StatusTypeDef status, uint32_t error_code) noexcept {
  return VendorCode(static_cast<uint32_t>(status), error_code);
}

// --- timers --------------------------------------------------------------------------------------
std::array<TIM_HandleTypeDef, kTimers>& Timers() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<TIM_HandleTypeDef, kTimers> timers{};
  return timers;
}

// The timers the port serves, by number, in two halves.
[[nodiscard]] TIM_TypeDef* TimerOfLow(uint8_t timer) noexcept {
  TIM_TypeDef* t = nullptr;
  switch (timer) {
#if defined(TIM1)
  case 1U:
    t = TIM1;
    break;
#endif
#if defined(TIM2)
  case 2U:
    t = TIM2;
    break;
#endif
#if defined(TIM3)
  case 3U:
    t = TIM3;
    break;
#endif
#if defined(TIM4)
  case 4U:
    t = TIM4;
    break;
#endif
#if defined(TIM5)
  case 5U:
    t = TIM5;
    break;
#endif
#if defined(TIM8)
  case 8U:
    t = TIM8;
    break;
#endif
  default:
    t = nullptr; // a timer of the other half, or one the device does not have
    break;
  }
  return t;
}

[[nodiscard]] TIM_TypeDef* TimerOfHigh(uint8_t timer) noexcept {
  TIM_TypeDef* t = nullptr;
  switch (timer) {
#if defined(TIM9)
  case 9U:
    t = TIM9;
    break;
#endif
#if defined(TIM12)
  case 12U:
    t = TIM12;
    break;
#endif
#if defined(TIM15)
  case 15U:
    t = TIM15;
    break;
#endif
#if defined(TIM16)
  case 16U:
    t = TIM16;
    break;
#endif
#if defined(TIM17)
  case 17U:
    t = TIM17;
    break;
#endif
#if defined(TIM20)
  case 20U:
    t = TIM20;
    break;
#endif
  default:
    t = nullptr; // a timer of the other half, or one the device does not have
    break;
  }
  return t;
}

[[nodiscard]] TIM_TypeDef* TimerOf(uint8_t timer) noexcept {
  TIM_TypeDef* const low = TimerOfLow(timer);
  return (low != nullptr) ? low : TimerOfHigh(timer);
}

// True for the timers on APB2 (TIM1, TIM8, TIM9 to TIM11, TIM15 to TIM17, TIM20).
[[nodiscard]] bool OnApb2(uint8_t timer) noexcept {
  return (timer == 1U) || (timer == 8U) || ((timer >= 9U) && (timer <= 11U)) ||
         ((timer >= 15U) && (timer <= 17U)) || (timer == 20U);
}

// The timer clocks, in two halves (one switch each).
void TimerClockLow(uint8_t timer) noexcept {
  switch (timer) {
#if defined(TIM1)
  case 1U:
    __HAL_RCC_TIM1_CLK_ENABLE();
    break;
#endif
#if defined(TIM2)
  case 2U:
    __HAL_RCC_TIM2_CLK_ENABLE();
    break;
#endif
#if defined(TIM3)
  case 3U:
    __HAL_RCC_TIM3_CLK_ENABLE();
    break;
#endif
#if defined(TIM4)
  case 4U:
    __HAL_RCC_TIM4_CLK_ENABLE();
    break;
#endif
#if defined(TIM5)
  case 5U:
    __HAL_RCC_TIM5_CLK_ENABLE();
    break;
#endif
#if defined(TIM8)
  case 8U:
    __HAL_RCC_TIM8_CLK_ENABLE();
    break;
#endif
  default:
    break; // a timer of the other half, or one the device does not have
  }
}

void TimerClockHigh(uint8_t timer) noexcept {
  switch (timer) {
#if defined(TIM9)
  case 9U:
    __HAL_RCC_TIM9_CLK_ENABLE();
    break;
#endif
#if defined(TIM12)
  case 12U:
    __HAL_RCC_TIM12_CLK_ENABLE();
    break;
#endif
#if defined(TIM15)
  case 15U:
    __HAL_RCC_TIM15_CLK_ENABLE();
    break;
#endif
#if defined(TIM16)
  case 16U:
    __HAL_RCC_TIM16_CLK_ENABLE();
    break;
#endif
#if defined(TIM17)
  case 17U:
    __HAL_RCC_TIM17_CLK_ENABLE();
    break;
#endif
#if defined(TIM20)
  case 20U:
    __HAL_RCC_TIM20_CLK_ENABLE();
    break;
#endif
  default:
    break; // a timer of the other half, or one the device does not have
  }
}

void TimerClock(uint8_t timer) noexcept {
  TimerClockLow(timer);
  TimerClockHigh(timer);
}

[[nodiscard]] uint32_t ChannelOf(uint8_t channel) noexcept {
  constexpr std::array<uint32_t, 4> kChannels{TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3,
                                              TIM_CHANNEL_4};
  return kChannels[static_cast<std::size_t>(channel - 1U)];
}

[[nodiscard]] uint32_t TimerInit(TIM_HandleTypeDef& h, uint8_t timer,
                                 const PwmPeriod& period) noexcept {
  TimerClock(timer);
  h.Instance = TimerOf(timer);
  h.Init.Prescaler = period.prescaler;
  h.Init.CounterMode = TIM_COUNTERMODE_UP;
  h.Init.Period = period.reload;
  h.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  h.Init.RepetitionCounter = 0U;
  h.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  const HAL_StatusTypeDef s = HAL_TIM_PWM_Init(&h);
  return Code(s, 0U);
}

// True when the timer clock prescaler selection (TIMPRE) is set: the timer clock is then HCLK for
// an APB divider up to 4 (F42x/F43x/F446 DCKCFGR, H7 CFGR).
[[nodiscard]] bool TimerPrescalerSelect() noexcept {
#if defined(RCC_DCKCFGR_TIMPRE)
  return (RCC->DCKCFGR & RCC_DCKCFGR_TIMPRE) != 0U;
#elif defined(RCC_CFGR_TIMPRE)
  return (RCC->CFGR & RCC_CFGR_TIMPRE) != 0U;
#else
  return false;
#endif
}
} // namespace

// --- timers (PWM) --------------------------------------------------------------------------------
bool TimerExists(uint8_t timer) noexcept {
  return (timer < kTimers) && (TimerOf(timer) != nullptr);
}

uint32_t TimerClockHz(uint8_t timer) noexcept {
  const uint32_t hclk = HAL_RCC_GetHCLKFreq();
  const uint32_t pclk = OnApb2(timer) ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();
  uint32_t hz = 0U;
  if (pclk == 0U) {
    hz = 0U;
  } else if (TimerPrescalerSelect()) {
    hz = ((hclk / pclk) <= 4U) ? hclk : (4U * pclk);
  } else {
    hz = (pclk == hclk) ? pclk : (2U * pclk); // the timer clock doubles behind an APB divider
  }
  return hz;
}

uint32_t PwmInit(uint8_t timer, const PwmPeriod& period) noexcept {
  return TimerInit(Timers()[timer], timer, period);
}

uint32_t PwmStart(uint8_t timer, uint8_t channel, PinId pin) noexcept {
  TIM_HandleTypeDef& h = Timers()[timer];
  uint32_t code = PinAlternate(pin, false, Pull::None);
  if (code == hal::kOk) {
    TIM_OC_InitTypeDef oc{};
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0U;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    HAL_StatusTypeDef s = HAL_TIM_PWM_ConfigChannel(&h, &oc, ChannelOf(channel));
    if (s == HAL_OK) {
      s = HAL_TIM_PWM_Start(&h, ChannelOf(channel));
    }
    code = Code(s, 0U);
  }
  return code;
}

uint32_t PwmSetPeriod(uint8_t timer, const PwmPeriod& period) noexcept {
  TIM_HandleTypeDef& h = Timers()[timer];
  __HAL_TIM_SET_PRESCALER(&h, period.prescaler);
  __HAL_TIM_SET_AUTORELOAD(&h, period.reload);
  const HAL_StatusTypeDef s = HAL_TIM_GenerateEvent(&h, TIM_EVENTSOURCE_UPDATE);
  return Code(s, 0U);
}

void PwmSetCompare(uint8_t timer, uint8_t channel, uint32_t compare) noexcept {
  TIM_HandleTypeDef& h = Timers()[timer];
  __HAL_TIM_SET_COMPARE(&h, ChannelOf(channel), compare);
}

uint32_t PwmStop(uint8_t timer, uint8_t channel) noexcept {
  const HAL_StatusTypeDef s = HAL_TIM_PWM_Stop(&Timers()[timer], ChannelOf(channel));
  return Code(s, 0U);
}

uint32_t PwmDeInit(uint8_t timer) noexcept {
  const HAL_StatusTypeDef s = HAL_TIM_PWM_DeInit(&Timers()[timer]);
  return Code(s, 0U);
}

// --- the clock's timer ---------------------------------------------------------------------------
bool ClockTimerExists(uint8_t timer) noexcept {
  return ((timer == 2U) || (timer == 5U)) && TimerExists(timer);
}

uint32_t ClockStart(const ClockConfig& config) noexcept {
  constexpr uint32_t kTickHz = 1000000U;
  TIM_HandleTypeDef& h = Timers()[config.timer];
  TimerClock(config.timer);
  h.Instance = TimerOf(config.timer);
  h.Init.Prescaler = ((config.timer_clock_hz + (kTickHz / 2U)) / kTickHz) - 1U;
  h.Init.CounterMode = TIM_COUNTERMODE_UP;
  h.Init.Period = 0xFFFFFFFFU;
  h.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  h.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  HAL_StatusTypeDef s = HAL_TIM_Base_Init(&h);
  if (s == HAL_OK) {
    __HAL_TIM_SET_COUNTER(&h, 0U); // a timer keeps counting across a jump from a loader
    __HAL_TIM_CLEAR_FLAG(&h, TIM_FLAG_UPDATE);
    __HAL_TIM_ENABLE_IT(&h, TIM_IT_UPDATE);
    const IRQn_Type irq = (config.timer == 2U) ? TIM2_IRQn : TIM5_IRQn;
    HAL_NVIC_SetPriority(irq, config.irq_priority, 0U);
    HAL_NVIC_EnableIRQ(irq);
    s = HAL_TIM_Base_Start(&h);
  }
  return Code(s, 0U);
}

uint32_t ClockCounter(uint8_t timer) noexcept {
  return Timers()[timer].Instance->CNT;
}

bool ClockWrapPending(uint8_t timer) noexcept {
  return __HAL_TIM_GET_FLAG(&Timers()[timer], TIM_FLAG_UPDATE) != 0U;
}

void ClockIrqOff(uint8_t timer) noexcept {
  HAL_NVIC_DisableIRQ((timer == 5U) ? TIM5_IRQn : TIM2_IRQn);
}

void ClockClearWrap(uint8_t timer) noexcept {
  __HAL_TIM_CLEAR_FLAG(&Timers()[timer], TIM_FLAG_UPDATE);
}

} // namespace kb::port::stm32::sdk
