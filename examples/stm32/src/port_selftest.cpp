// STM32 example firmware: the port's self test (clock, busy wait, log, PWM, ADC, I2C and SPI buses,
// CAN in loopback, UART), then an idle loop: bare metal (kb::rtos's none backend) by default, or one FreeRTOS task with EXAMPLE_RTOS=FREERTOS
// (src/freertos_start.c). The HAL runs on the reset clock (HSI or MSI). Example code.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <array>
#include <cstdint>

#include "board.hpp"
#include "kb/port/stm32/port.hpp"
#include "kb/rtos/time.hpp"

namespace stm = kb::port::stm32;

extern "C" {
int HAL_Init(void);
void HAL_IncTick(void);
void kb_example_main(void);
#if defined(EXAMPLE_RTOS_FREERTOS)
// FreeRTOS owns SysTick (FreeRTOSConfig.h); its tick hook advances the HAL time base.
void kb_example_start(void);
#else
// Bare metal: the HAL time base is SysTick at 1 kHz (HAL_Init). The startup file names this handler.
void SysTick_Handler(void);
void SysTick_Handler(void) {
  HAL_IncTick();
}
#endif
}

namespace {
stm::ItmLogSink& Log() {
  static stm::ItmLogSink log{};
  return log;
}

void SelfTest(stm::Clock& clock) {
  stm::BusyWait wait{};
  const kb::Instant t0 = clock.Now();
  wait.WaitUs(kb::Microseconds{500U});
  static_cast<void>(kb::utils::ElapsedUs(clock.Now(), t0));
  Log().Write(kb::iface::LogLevel::Info, "kb port self test");
  stm::PwmConfig p{};
  p.timer = board::kPwmTimer;
  p.channel = board::kPwmChannel;
  p.pin = board::kPwmPin;
  p.frequency_hz = 5000U;
  static stm::PwmChannel pwm{p};
  if (pwm.EnsureReady() == kb::Status::Ok) {
    static_cast<void>(pwm.SetDuty(kb::iface::Duty{2500U}));
    static_cast<void>(pwm.Enable(true));
  }
  stm::AdcConfig a{};
  a.instance = board::kAdc;
  a.channel = board::kAdcChannel;
  a.pin = board::kAdcPin;
  static stm::AdcChannel adc{a};
  if (adc.EnsureReady() == kb::Status::Ok) {
    static_cast<void>(adc.ReadMillivolts());
  }
  stm::CanConfig c{};
  c.tx = board::kCanTx;
  c.rx = board::kCanRx;
  c.mode = stm::CanMode::Loopback;
  static stm::CanController can{c};
  kb::iface::CanFrame f{};
  f.id = 0x123U;
  f.dlc = 2U;
  if ((can.EnsureReady() == kb::Status::Ok) && (can.Send(f, kb::Milliseconds{10U}) == kb::Status::Ok)) {
    kb::iface::CanFrame back{};
    static_cast<void>(can.Receive(back, kb::Milliseconds{10U}));
  }
  stm::I2cConfig i{};
  i.instance = board::kI2c;
  i.scl = board::kI2cScl;
  i.sda = board::kI2cSda;
  static stm::I2cController i2c{i};
  static_cast<void>(i2c.EnsureReady()); // no device is wired: only the bus comes up
  stm::SpiBusConfig sb{};
  sb.instance = board::kSpi;
  sb.sck = board::kSpiSck;
  sb.miso = board::kSpiMiso;
  sb.mosi = board::kSpiMosi;
  stm::SpiDeviceConfig sd{};
  sd.cs = board::kSpiCs;
  static stm::SpiBus spi_bus{sb};
  static stm::SpiDevice spi{spi_bus, sd};
  static_cast<void>(spi.EnsureReady());
  stm::UartConfig u{};
  u.instance = board::kUart;
  u.tx = board::kUartTx;
  u.rx = board::kUartRx;
  static stm::Uart uart{u};
  const std::array<uint8_t, 2> out{0x55U, 0xAAU};
  if (uart.EnsureReady() == kb::Status::Ok) {
    static_cast<void>(uart.Write(out));
  }
}
} // namespace

// The application: EnsureReady() of every object runs here, after the scheduler (under FreeRTOS).
void kb_example_main(void) {
  static stm::Clock clock{};
  static_cast<void>(clock.Start());
  SelfTest(clock);

  while (true) {
#if defined(EXAMPLE_RTOS_FREERTOS)
    kb::rtos::SleepFor(kb::Milliseconds{1000U});
#endif
  }
}

int main() {
  static_cast<void>(HAL_Init());
#if defined(EXAMPLE_RTOS_FREERTOS)
  kb_example_start(); // the scheduler runs kb_example_main() as a task; never returns
#else
  kb_example_main();
#endif
  return 0;
}
