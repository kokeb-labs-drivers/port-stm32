// STM32 example firmware: the port's self test (clock, busy wait, log, GPIO, PWM, ADC, CAN in loopback,
// UART), then pcal9555 and bno08x sharing one I2C controller and tmc5160 on SPI, served from one loop
// (no RTOS: kb::rtos's RTOS-free backend). The HAL runs on the reset clock (HSI or MSI). Example code.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <array>
#include <cstdint>

#include "bno08x.hpp"
#include "bno08x_kconfig.hpp"
#include "board.hpp"
#include "driver_adapters.hpp"
#include "kb/port/stm32/port.hpp"
#include "pcal9555.hpp"
#include "tmc5160.hpp"
#include "tmc5160_kconfig.hpp"

namespace stm = kb::port::stm32;

extern "C" {
// The HAL time base: SysTick at 1 kHz (HAL_Init). The startup file names these handlers.
void SysTick_Handler(void);
void HAL_IncTick(void);
int HAL_Init(void);
}

void SysTick_Handler(void) {
  HAL_IncTick();
}

namespace {
stm::ItmLogSink& Log() {
  static stm::ItmLogSink log{};
  return log;
}

stm::I2cController& Bus() {
  static stm::I2cController bus{[] {
    stm::I2cConfig c{};
    c.instance = board::kI2c;
    c.scl = board::kI2cScl;
    c.sda = board::kI2cSda;
    c.clock_hz = 100000U;
    return c;
  }()};
  return bus;
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

int main() {
  static_cast<void>(HAL_Init());
  static stm::Clock clock{};
  static_cast<void>(clock.Start());
  SelfTest(clock);

  using PcalPort = adapters::Pcal9555I2c<stm::I2cController, stm::ItmLogSink>;
  static PcalPort pcal_port{Bus(), Log()};
  static pcal9555::PCAL9555<PcalPort> expander{pcal_port};

  stm::SpiBusConfig sb{};
  sb.instance = board::kSpi;
  sb.sck = board::kSpiSck;
  sb.miso = board::kSpiMiso;
  sb.mosi = board::kSpiMosi;
  stm::SpiDeviceConfig sd{};
  sd.cs = board::kSpiCs;
  sd.mode = 3U;
  sd.clock_hz = 4000000U;
  stm::OutputConfig enn{};
  enn.pin = board::kTmcEnn;
  enn.initial = kb::iface::Level::High;
  static stm::SpiBus spi_bus{sb};
  static stm::SpiDevice spi{spi_bus, sd};
  static stm::OutputPin drv_enn{enn};
  static stm::BusyWait delay{};
  using TmcPort = adapters::Tmc5160Spi<stm::SpiDevice, stm::OutputPin, stm::BusyWait>;
  static TmcPort tmc_port{spi, drv_enn, delay};
  static tmc5160::TMC5160<TmcPort> motor{tmc_port, tmc5160::KconfigDefaults()};
  static_cast<void>(drv_enn.EnsureReady());

  stm::InputConfig ic{};
  ic.pin = board::kBnoInt;
  ic.pull = stm::Pull::Up;
  static stm::InputPin h_intn{ic};
  static_cast<void>(h_intn.EnsureReady());
  using BnoPort = adapters::Bno08xI2c<stm::I2cController, stm::InputPin, stm::Clock>;
  static BnoPort bno_port{Bus(), h_intn, clock};
  static bno08x::Bno08x<BnoPort, bno08x::KconfigLimits> hub{bno_port};
  static_cast<void>(hub.Begin(bno08x::KconfigDefaults()));

  uint32_t last = 0U;
  while (true) {
    hub.Poll(kb::utils::NarrowUs(clock.Now()));
    bno08x::Event ev{};
    while (hub.PopEvent(ev)) {
    }
    const uint32_t now = kb::utils::NarrowMs(clock.Now());
    if (kb::utils::ElapsedSince(now, last) >= 500U) {
      last = now;
      if (expander.EnsureInitialized()) {
        static_cast<void>(expander.ToggleOutputs(0x0001U));
      }
      if (motor.EnsureInitialized()) {
        static_cast<void>(motor.ReadPosition());
      }
    }
  }
}
