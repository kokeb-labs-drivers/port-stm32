// Boundary file (deviations/boundary-files.yml): the system, GPIO, EXTI, ADC, timer (PWM and
// clock), DWT and ITM calls of the STM32 HAL and CMSIS behind sdk.hpp, the ADC completion callbacks
// and the port's shared state (state.hpp). Family differences use the CMSIS family macros (STM32F4,
// STM32G4, STM32H7, STM32L4) and the instance macros of the device header, here and in the other
// boundary files only. Every rule over the shared state is in the platform-free files.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include <array>
// Slot pointers shared with the interrupts are std::atomic.
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>

#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/adc.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/gpio.hpp"
#include "kb/port/stm32/irq.hpp"
#include "kb/port/stm32/pwm.hpp"
#include "kb/port/stm32/time.hpp"
#include "sdk.hpp"
#include "state.hpp"
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
#if defined(STM32H7) && defined(CORE_CM4)
// The second core of a dual-core H7 has its own EXTI pending registers (C2PR1) and shares the
// peripherals with the first: not supported (docs/families.md).
#error "port-stm32 supports the Cortex-M7 of STM32H7 only, not CORE_CM4"
#endif

namespace kb::port::stm32::sdk {

// error.hpp names the HAL's values; a change in the HAL stops the build here.
static_assert(hal::kOk == static_cast<uint32_t>(HAL_OK));
static_assert(hal::kError == static_cast<uint32_t>(HAL_ERROR));
static_assert(hal::kBusy == static_cast<uint32_t>(HAL_BUSY));
static_assert(hal::kTimeout == static_cast<uint32_t>(HAL_TIMEOUT));

namespace {
constexpr std::size_t kPorts = kGpioPorts;

[[nodiscard]] uint32_t Code(HAL_StatusTypeDef status, uint32_t error_code) noexcept {
  return VendorCode(static_cast<uint32_t>(status), error_code);
}

// --- GPIO ----------------------------------------------------------------------------------------
[[nodiscard]] uint32_t ExtiModeOf(kb::iface::Edge edge) noexcept {
  uint32_t mode = GPIO_MODE_IT_RISING_FALLING;
  if (edge == kb::iface::Edge::Rising) {
    mode = GPIO_MODE_IT_RISING;
  } else if (edge == kb::iface::Edge::Falling) {
    mode = GPIO_MODE_IT_FALLING;
  } else {
    mode = GPIO_MODE_IT_RISING_FALLING;
  }
  return mode;
}

[[nodiscard]] GPIO_TypeDef* PortOf(GpioPort port) noexcept {
  // The ports this device has, by index; nullptr where it has none.
  const std::array<GPIO_TypeDef*, kPorts> ports{
      GPIOA, GPIOB, GPIOC,
#if defined(GPIOD)
      GPIOD,
#else
      nullptr,
#endif
#if defined(GPIOE)
      GPIOE,
#else
      nullptr,
#endif
#if defined(GPIOF)
      GPIOF,
#else
      nullptr,
#endif
#if defined(GPIOG)
      GPIOG,
#else
      nullptr,
#endif
#if defined(GPIOH)
      GPIOH,
#else
      nullptr,
#endif
#if defined(GPIOI)
      GPIOI,
#else
      nullptr,
#endif
#if defined(GPIOJ)
      GPIOJ,
#else
      nullptr,
#endif
#if defined(GPIOK)
      GPIOK,
#else
      nullptr,
#endif
  };
  const auto index = static_cast<std::size_t>(port);
  return (index < ports.size()) ? ports[index] : nullptr;
}

// The port clocks, in two halves.
void PortClockLow(GpioPort port) noexcept {
  if (port == GpioPort::A) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
  } else if (port == GpioPort::B) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
  } else if (port == GpioPort::C) {
    __HAL_RCC_GPIOC_CLK_ENABLE();
#if defined(GPIOD)
  } else if (port == GpioPort::D) {
    __HAL_RCC_GPIOD_CLK_ENABLE();
#endif
#if defined(GPIOE)
  } else if (port == GpioPort::E) {
    __HAL_RCC_GPIOE_CLK_ENABLE();
#endif
#if defined(GPIOF)
  } else if (port == GpioPort::F) {
    __HAL_RCC_GPIOF_CLK_ENABLE();
#endif
  } else {
    // a port of the other half, or one the device does not have
  }
}

void PortClockHigh(GpioPort port) noexcept {
  static_cast<void>(port);
#if defined(GPIOG)
  if (port == GpioPort::G) {
    __HAL_RCC_GPIOG_CLK_ENABLE();
  }
#endif
#if defined(GPIOH)
  if (port == GpioPort::H) {
    __HAL_RCC_GPIOH_CLK_ENABLE();
  }
#endif
#if defined(GPIOI)
  if (port == GpioPort::I) {
    __HAL_RCC_GPIOI_CLK_ENABLE();
  }
#endif
#if defined(GPIOJ)
  if (port == GpioPort::J) {
    __HAL_RCC_GPIOJ_CLK_ENABLE();
  }
#endif
#if defined(GPIOK)
  if (port == GpioPort::K) {
    __HAL_RCC_GPIOK_CLK_ENABLE();
  }
#endif
}

void PortClock(GpioPort port) noexcept {
  PortClockLow(port);
  PortClockHigh(port);
}

[[nodiscard]] uint16_t MaskOf(PinId pin) noexcept {
  return static_cast<uint16_t>(1U << pin.pin);
}

[[nodiscard]] uint32_t PullOf(Pull pull) noexcept {
  uint32_t p = GPIO_NOPULL;
  if (pull == Pull::Up) {
    p = GPIO_PULLUP;
  } else if (pull == Pull::Down) {
    p = GPIO_PULLDOWN;
  } else {
    p = GPIO_NOPULL;
  }
  return p;
}

[[nodiscard]] uint32_t SpeedOf(Speed speed) noexcept {
  uint32_t s = GPIO_SPEED_FREQ_LOW;
  if (speed == Speed::Medium) {
    s = GPIO_SPEED_FREQ_MEDIUM;
  } else if (speed == Speed::High) {
    s = GPIO_SPEED_FREQ_HIGH;
  } else if (speed == Speed::VeryHigh) {
    s = GPIO_SPEED_FREQ_VERY_HIGH;
  } else {
    s = GPIO_SPEED_FREQ_LOW;
  }
  return s;
}

[[nodiscard]] IRQn_Type ExtiIrq(uint8_t line) noexcept {
  IRQn_Type irq = EXTI15_10_IRQn;
  if (line == 0U) {
    irq = EXTI0_IRQn;
  } else if (line == 1U) {
    irq = EXTI1_IRQn;
  } else if (line == 2U) {
    irq = EXTI2_IRQn;
  } else if (line == 3U) {
    irq = EXTI3_IRQn;
  } else if (line == 4U) {
    irq = EXTI4_IRQn;
  } else if (line <= 9U) {
    irq = EXTI9_5_IRQn;
  } else {
    irq = EXTI15_10_IRQn;
  }
  return irq;
}

// --- ADC -----------------------------------------------------------------------------------------
struct AdcSlot {
  ADC_HandleTypeDef handle{};
  std::atomic<Completion*> conversion{nullptr}; ///< The instance's completion while it is open.
};

std::array<AdcSlot, kAdcs>& Adcs() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<AdcSlot, kAdcs> adcs{};
  return adcs;
}

[[nodiscard]] IRQn_Type AdcIrq(uint8_t instance) noexcept {
#if defined(STM32F4)
  static_cast<void>(instance);
  return ADC_IRQn; // ADC1 to ADC3 share one vector
#elif defined(STM32H7)
  IRQn_Type irq = ADC_IRQn; // ADC1 and ADC2
#if defined(ADC3)
  if (instance == 3U) {
    irq = ADC3_IRQn;
  }
#endif
  return irq;
#elif defined(STM32G4)
  IRQn_Type irq = ADC1_2_IRQn;
#if defined(ADC3)
  if (instance == 3U) {
    irq = ADC3_IRQn;
  }
#endif
#if defined(ADC4)
  if (instance == 4U) {
    irq = ADC4_IRQn;
  }
#endif
#if defined(ADC5)
  if (instance == 5U) {
    irq = ADC5_IRQn;
  }
#endif
  return irq;
#else
#if defined(ADC2)
  IRQn_Type irq = ADC1_2_IRQn;
#else
  IRQn_Type irq = ADC1_IRQn;
#endif
#if defined(ADC3)
  if (instance == 3U) {
    irq = ADC3_IRQn;
  }
#endif
  return irq;
#endif
}

[[nodiscard]] ADC_TypeDef* AdcOf(uint8_t instance) noexcept {
  ADC_TypeDef* a = nullptr;
  if (instance == 1U) {
    a = ADC1;
#if defined(ADC2)
  } else if (instance == 2U) {
    a = ADC2;
#endif
#if defined(ADC3)
  } else if (instance == 3U) {
    a = ADC3;
#endif
#if defined(ADC4)
  } else if (instance == 4U) {
    a = ADC4;
#endif
#if defined(ADC5)
  } else if (instance == 5U) {
    a = ADC5;
#endif
  } else {
    a = nullptr;
  }
  return a;
}

void AdcClock(uint8_t instance) noexcept {
#if defined(STM32F4)
  if (instance == 1U) {
    __HAL_RCC_ADC1_CLK_ENABLE();
#if defined(ADC2)
  } else if (instance == 2U) {
    __HAL_RCC_ADC2_CLK_ENABLE();
#endif
#if defined(ADC3)
  } else if (instance == 3U) {
    __HAL_RCC_ADC3_CLK_ENABLE();
#endif
  } else {
    // no other ADC on F4
  }
#elif defined(STM32G4)
  if (instance <= 2U) {
    __HAL_RCC_ADC12_CLK_ENABLE();
  } else {
#if defined(ADC345_COMMON)
    __HAL_RCC_ADC345_CLK_ENABLE();
#endif
  }
#elif defined(STM32H7)
  if (instance <= 2U) {
    __HAL_RCC_ADC12_CLK_ENABLE();
  } else {
    __HAL_RCC_ADC3_CLK_ENABLE();
  }
#else
  static_cast<void>(instance);
  __HAL_RCC_ADC_CLK_ENABLE();
#endif
}

void AdcInitFields(ADC_HandleTypeDef& h) noexcept {
#if !defined(STM32H7)
  h.Init.DataAlign = ADC_DATAALIGN_RIGHT; // H7: right aligned unless LeftBitShift says otherwise
#endif
  h.Init.ContinuousConvMode = DISABLE;
  h.Init.DiscontinuousConvMode = DISABLE;
  h.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  h.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  h.Init.NbrOfConversion = 1U;
  h.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
#if defined(STM32F4)
  h.Init.Resolution = ADC_RESOLUTION_12B;
  h.Init.ScanConvMode = DISABLE;
  h.Init.DMAContinuousRequests = DISABLE;
#elif defined(STM32H7)
  h.Init.Resolution = ADC_RESOLUTION_16B;
  h.Init.ScanConvMode = ADC_SCAN_DISABLE;
  h.Init.LowPowerAutoWait = DISABLE;
  h.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
  h.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  h.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  h.Init.OversamplingMode = DISABLE;
#else
  h.Init.Resolution = ADC_RESOLUTION_12B;
  h.Init.ScanConvMode = ADC_SCAN_DISABLE;
  h.Init.LowPowerAutoWait = DISABLE;
  h.Init.DMAContinuousRequests = DISABLE;
  h.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  h.Init.OversamplingMode = DISABLE;
#endif
}

[[nodiscard]] uint32_t AdcChannelOf(uint8_t channel) noexcept {
#if defined(STM32F4)
  return channel; // ADC_CHANNEL_<n> is n on F4
#else
  return __LL_ADC_DECIMAL_NB_TO_CHANNEL(channel);
#endif
}

[[nodiscard]] uint32_t SamplingOf(AdcSampling sampling) noexcept {
#if defined(STM32F4)
  constexpr std::array<uint32_t, 3> kTimes{ADC_SAMPLETIME_15CYCLES, ADC_SAMPLETIME_84CYCLES,
                                           ADC_SAMPLETIME_480CYCLES};
#elif defined(STM32H7)
  constexpr std::array<uint32_t, 3> kTimes{ADC_SAMPLETIME_8CYCLES_5, ADC_SAMPLETIME_64CYCLES_5,
                                           ADC_SAMPLETIME_810CYCLES_5};
#else
  constexpr std::array<uint32_t, 3> kTimes{ADC_SAMPLETIME_6CYCLES_5, ADC_SAMPLETIME_47CYCLES_5,
                                           ADC_SAMPLETIME_640CYCLES_5};
#endif
  return kTimes[static_cast<std::size_t>(sampling)];
}

[[nodiscard]] uint32_t AdcCalibrate(ADC_HandleTypeDef& h) noexcept {
#if defined(STM32F4)
  static_cast<void>(h); // the F4 ADC has no calibration
  return hal::kOk;
#elif defined(STM32H7)
  const HAL_StatusTypeDef s =
      HAL_ADCEx_Calibration_Start(&h, ADC_CALIB_OFFSET_LINEARITY, ADC_SINGLE_ENDED);
  return Code(s, h.ErrorCode);
#else
  const HAL_StatusTypeDef s = HAL_ADCEx_Calibration_Start(&h, ADC_SINGLE_ENDED);
  return Code(s, h.ErrorCode);
#endif
}

[[nodiscard]] uint32_t AdcStartOne(ADC_HandleTypeDef& h, uint8_t channel,
                                   AdcSampling sampling) noexcept {
  ADC_ChannelConfTypeDef c{};
  c.Channel = AdcChannelOf(channel);
  c.SamplingTime = SamplingOf(sampling);
#if defined(STM32F4)
  c.Rank = 1U;
#else
  c.Rank = ADC_REGULAR_RANK_1;
  c.SingleDiff = ADC_SINGLE_ENDED;
  c.OffsetNumber = ADC_OFFSET_NONE;
#endif
  HAL_StatusTypeDef s = HAL_ADC_ConfigChannel(&h, &c);
  if (s == HAL_OK) {
    s = HAL_ADC_Start_IT(&h); // ends in HAL_ADC_ConvCpltCallback or HAL_ADC_ErrorCallback
  }
  // ErrorCode only when the start failed: the conversion may already have ended.
  return (s == HAL_OK) ? hal::kOk : Code(s, h.ErrorCode);
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

PortState& State() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static PortState state{};
  return state;
}

// --- GPIO ----------------------------------------------------------------------------------------
bool PortExists(GpioPort port) noexcept {
  return PortOf(port) != nullptr;
}

uint32_t PinOutput(PinId pin, bool open_drain, Pull pull, Speed speed, bool high) noexcept {
  PortClock(pin.port);
  HAL_GPIO_WritePin(PortOf(pin.port), MaskOf(pin),
                    high ? GPIO_PIN_SET : GPIO_PIN_RESET); // first: no glitch
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = open_drain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT_PP;
  init.Pull = PullOf(pull);
  init.Speed = SpeedOf(speed);
  HAL_GPIO_Init(PortOf(pin.port), &init);
  return hal::kOk;
}

uint32_t PinInput(PinId pin, Pull pull) noexcept {
  PortClock(pin.port);
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = GPIO_MODE_INPUT;
  init.Pull = PullOf(pull);
  init.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PortOf(pin.port), &init);
  return hal::kOk;
}

uint32_t PinAlternate(PinId pin, bool open_drain, Pull pull) noexcept {
  PortClock(pin.port);
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = open_drain ? GPIO_MODE_AF_OD : GPIO_MODE_AF_PP;
  init.Pull = PullOf(pull);
  init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  init.Alternate = pin.af;
  HAL_GPIO_Init(PortOf(pin.port), &init);
  return hal::kOk;
}

uint32_t PinAnalog(PinId pin) noexcept {
  PortClock(pin.port);
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = GPIO_MODE_ANALOG;
  init.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PortOf(pin.port), &init);
  return hal::kOk;
}

void PinReset(PinId pin) noexcept {
  HAL_GPIO_DeInit(PortOf(pin.port), MaskOf(pin));
}

void PinWrite(PinId pin, bool high) noexcept {
  HAL_GPIO_WritePin(PortOf(pin.port), MaskOf(pin), high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool PinRead(PinId pin) noexcept {
  return HAL_GPIO_ReadPin(PortOf(pin.port), MaskOf(pin)) == GPIO_PIN_SET;
}

void PinToggle(PinId pin) noexcept {
  HAL_GPIO_TogglePin(PortOf(pin.port), MaskOf(pin));
}

namespace {
// The pull setting a pin has now (its PUPDR bits); no pull for a port the device lacks (refused
// earlier).
[[nodiscard]] uint32_t PullBitsOf(PinId pin) noexcept {
  const GPIO_TypeDef* const port = PortOf(pin.port);
  return (port != nullptr) ? ((port->PUPDR >> (2U * pin.pin)) & 3U) : GPIO_NOPULL;
}
} // namespace

uint32_t ExtiConfigure(PinId pin, kb::iface::Edge edge, uint8_t priority) noexcept {
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = ExtiModeOf(edge);
  init.Pull = PullBitsOf(pin);
  init.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PortOf(pin.port), &init);
  HAL_NVIC_SetPriority(ExtiIrq(pin.pin), priority, 0U);
  HAL_NVIC_EnableIRQ(ExtiIrq(pin.pin));
  return hal::kOk;
}

uint32_t ExtiDeconfigure(PinId pin) noexcept {
  const uint32_t pull = PullBitsOf(pin);
  HAL_GPIO_DeInit(PortOf(pin.port), MaskOf(pin)); // clears the EXTI line's configuration
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = GPIO_MODE_INPUT;
  init.Pull = pull;
  init.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PortOf(pin.port), &init);
  return hal::kOk;
}

bool ExtiTakePending(uint8_t line) noexcept {
  const uint32_t mask = 1U << line;
  const bool pending = __HAL_GPIO_EXTI_GET_IT(mask) != 0U;
  if (pending) {
    __HAL_GPIO_EXTI_CLEAR_IT(mask);
  }
  return pending;
}

// --- ADC -----------------------------------------------------------------------------------------
bool AdcExists(uint8_t instance, uint8_t channel) noexcept {
#if defined(STM32F4)
  constexpr uint8_t kMaxChannel = 18U; // IN0 to IN15, temperature, VREFINT, VBAT
#else
  constexpr uint8_t kMaxChannel = 19U;
#endif
  return (instance < kAdcs) && (AdcOf(instance) != nullptr) && (channel <= kMaxChannel);
}

uint8_t AdcBits(uint8_t instance) noexcept {
  static_cast<void>(instance);
#if defined(STM32H7)
  return 16U;
#else
  return 12U;
#endif
}

namespace {
// The ADC clock: the synchronous HCLK / 4 (PCLK2 / 4 on F4); on H7, when HCLK / 4 is above the
// limit, the asynchronous kernel clock (RCC ADCSEL) with the smallest divider that meets it.
[[nodiscard]] uint32_t AdcClockChoice(uint32_t& prescaler) noexcept {
  prescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
#if defined(STM32F4)
  const uint32_t sync_hz = HAL_RCC_GetPCLK2Freq() / 4U;
#else
  const uint32_t sync_hz = HAL_RCC_GetHCLKFreq() / 4U;
#endif
  uint32_t code = (sync_hz == 0U) ? port_code::kNoClock : hal::kOk;
#if defined(STM32H7)
  if ((code == hal::kOk) && (sync_hz > kH7AdcMaxHz)) {
    constexpr std::array<uint32_t, kAdcAsyncDividers.size()> kAsync{
        ADC_CLOCK_ASYNC_DIV1,  ADC_CLOCK_ASYNC_DIV2,   ADC_CLOCK_ASYNC_DIV4,
        ADC_CLOCK_ASYNC_DIV6,  ADC_CLOCK_ASYNC_DIV8,   ADC_CLOCK_ASYNC_DIV10,
        ADC_CLOCK_ASYNC_DIV12, ADC_CLOCK_ASYNC_DIV16,  ADC_CLOCK_ASYNC_DIV32,
        ADC_CLOCK_ASYNC_DIV64, ADC_CLOCK_ASYNC_DIV128, ADC_CLOCK_ASYNC_DIV256};
    const uint32_t kernel_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_ADC);
    bool ok = false;
    const std::size_t index = AdcAsyncDividerIndex(kernel_hz, kH7AdcMaxHz, ok);
    prescaler = kAsync[index];
    code = ok ? hal::kOk : ((kernel_hz == 0U) ? port_code::kNoClock : port_code::kUnsupported);
  }
#endif
  return code;
}
} // namespace

uint32_t AdcClockCheck() noexcept {
  uint32_t prescaler = 0U;
  return AdcClockChoice(prescaler);
}

uint32_t AdcOpen(uint8_t instance, Completion& conversion, uint8_t priority) noexcept {
  AdcSlot& slot = Adcs()[instance];
  AdcClock(instance);
  slot.handle.Instance = AdcOf(instance);
  AdcInitFields(slot.handle);
  uint32_t prescaler = 0U;
  static_cast<void>(AdcClockChoice(prescaler)); // checked by AdcClockCheck() before the open
  slot.handle.Init.ClockPrescaler = prescaler;  // the HAL sets the H7 BOOST mode from it
  const HAL_StatusTypeDef s = HAL_ADC_Init(&slot.handle);
  uint32_t code = Code(s, slot.handle.ErrorCode);
  if (code == hal::kOk) {
    code = AdcCalibrate(slot.handle);
  }
  if (code == hal::kOk) {
    slot.conversion.store(&conversion, std::memory_order_release);
    HAL_NVIC_SetPriority(AdcIrq(instance), priority, 0U);
    HAL_NVIC_EnableIRQ(
        AdcIrq(instance)); // shared vectors stay enabled: the handler checks the slot
  }
  return code;
}

uint32_t AdcClose(uint8_t instance) noexcept {
  AdcSlot& slot = Adcs()[instance];
  slot.conversion.store(nullptr, std::memory_order_release);
  const HAL_StatusTypeDef s = HAL_ADC_DeInit(&slot.handle);
  return Code(s, slot.handle.ErrorCode);
}

uint32_t AdcStart(uint8_t instance, uint8_t channel, AdcSampling sampling) noexcept {
  return AdcStartOne(Adcs()[instance].handle, channel, sampling);
}

uint32_t AdcStop(uint8_t instance) noexcept {
  ADC_HandleTypeDef& h = Adcs()[instance].handle;
  const HAL_StatusTypeDef s = HAL_ADC_Stop_IT(&h);
  return Code(s, h.ErrorCode);
}

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

void ClockClearWrap(uint8_t timer) noexcept {
  __HAL_TIM_CLEAR_FLAG(&Timers()[timer], TIM_FLAG_UPDATE);
}

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

namespace {
// The slot of the ADC handle `h`, or nullptr.
[[nodiscard]] AdcSlot* AdcSlotOf(const ADC_HandleTypeDef* h) noexcept {
  AdcSlot* found = nullptr;
  for (AdcSlot& slot : Adcs()) {
    if (&slot.handle == h) {
      found = &slot;
    }
  }
  return found;
}
} // namespace

} // namespace kb::port::stm32::sdk

namespace kb::port::stm32 {
// The interrupt entry point (irq.hpp): the HAL's ADC handler of each open instance on the vector.
void HandleAdcIrq(uint8_t first, uint8_t last) noexcept {
  bool any = false;
  for (uint8_t i = first; (i <= last) && (i < kAdcs); ++i) {
    sdk::AdcSlot& slot = sdk::Adcs()[i];
    if (slot.conversion.load(std::memory_order_acquire) != nullptr) {
      any = true;
      HAL_ADC_IRQHandler(&slot.handle);
    }
  }
  if (!any) {
    HAL_NVIC_DisableIRQ(sdk::AdcIrq(first)); // no instance of the vector is the port's
  }
}
} // namespace kb::port::stm32

// The HAL's ADC callbacks (weak in the HAL, defined here): the end of the conversion the port
// started. C linkage and global names by definition.
extern "C" {
// NOLINTBEGIN(readability-identifier-naming): the HAL fixes these names
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
  kb::port::stm32::sdk::AdcSlot* const slot = kb::port::stm32::sdk::AdcSlotOf(hadc);
  kb::port::stm32::Completion* const done =
      (slot != nullptr) ? slot->conversion.load(std::memory_order_acquire) : nullptr;
  if (done != nullptr) {
    done->Complete(kb::port::stm32::hal::kOk, HAL_ADC_GetValue(hadc));
  }
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef* hadc) {
  kb::port::stm32::sdk::AdcSlot* const slot = kb::port::stm32::sdk::AdcSlotOf(hadc);
  kb::port::stm32::Completion* const done =
      (slot != nullptr) ? slot->conversion.load(std::memory_order_acquire) : nullptr;
  if (done != nullptr) {
    done->Complete(kb::port::stm32::VendorCode(static_cast<uint32_t>(HAL_ERROR), hadc->ErrorCode));
  }
}
// NOLINTEND(readability-identifier-naming)
} // extern "C"
