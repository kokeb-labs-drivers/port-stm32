// Every port type satisfies its kb::iface concepts (compile time) and works through the contract's
// type-erased *Ref (run time, over the fakes).
// Test code. SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <string>

#include "support.hpp"

namespace i = kb::iface;

static_assert(i::I2cBus<stm::I2cController>);
static_assert(i::SpiDevice<stm::SpiDevice>);
static_assert(i::SpiClockControl<stm::SpiDevice>);
static_assert(i::UartPort<stm::Uart>);
static_assert(i::UartBaudControl<stm::Uart>);
static_assert(i::UartTxDrain<stm::Uart>);
static_assert(i::GpioOutput<stm::OutputPin>);
static_assert(i::GpioInput<stm::InputPin>);
static_assert(i::GpioInterrupt<stm::InputPin>);
static_assert(i::AdcChannel<stm::AdcChannel>);
static_assert(i::PwmOutput<stm::PwmChannel>);
static_assert(i::CanBus<stm::CanController>);
static_assert(i::MonotonicClock<stm::Clock>);
static_assert(i::BusyWait<stm::BusyWait>);
static_assert(i::LogSink<stm::ItmLogSink>);
static_assert(i::LogSink<stm::UartLogSink>);
static_assert(!i::GpioInput<stm::OutputPin>);
static_assert(!i::GpioOutput<stm::InputPin>);
static_assert(!i::SpiDevice<stm::SpiBus>);
static_assert(!std::is_copy_constructible_v<stm::I2cController>);
static_assert(!std::is_move_constructible_v<stm::Uart>);

KB_TEST("contracts: bus references reach the port") {
  Fresh();
  fake::I2cPresent(0x40U, true);
  stm::I2cConfig ic{};
  ic.scl = PinId{GpioPort::B, 6U, 4U};
  ic.sda = PinId{GpioPort::B, 7U, 4U};
  stm::I2cController i2c{ic};
  i::I2cBusRef bus{i2c};
  const std::array<uint8_t, 1> one{0x01U};
  std::array<uint8_t, 1> rx{};
  KB_CHECK(bus.EnsureReady() == Status::Ok);
  KB_CHECK(bus.Write(0x40U, one) == Status::Ok);
  KB_CHECK(bus.WriteRead(0x40U, one, rx) == Status::Ok);
  KB_CHECK(bus.Read(0x41U, rx) == Status::NoDevice);
  KB_CHECK(bus.LastFailure() == i::I2cFailure::AddressNack);

  SystemCoreClock = 640000000U; // the fake's 160 MHz SPI kernel clock needs a fast core's ceiling
  stm::SpiBusConfig sc{};
  sc.sck = PinId{GpioPort::A, 5U, 5U};
  sc.mosi = PinId{GpioPort::A, 7U, 5U};
  stm::SpiBus spi_bus{sc};
  stm::SpiDeviceConfig dc{};
  dc.cs = PinId{GpioPort::A, 4U, 0U};
  stm::SpiDevice spi{spi_bus, dc};
  i::SpiDeviceRef dev{spi};
  KB_CHECK(dev.EnsureReady() == Status::Ok);
  KB_CHECK(dev.Transfer(one, rx) == Status::Ok);

  stm::UartConfig uc{};
  uc.tx = PinId{GpioPort::A, 2U, 7U};
  uc.rx = PinId{GpioPort::A, 3U, 7U};
  stm::Uart uart{uc};
  i::UartPortRef port{uart};
  KB_CHECK(port.EnsureReady() == Status::Ok);
  KB_CHECK(port.Write(one) == Status::Ok);
  KB_CHECK(port.FlushInput() == Status::Ok);
  KB_CHECK(!port.Read(rx, kb::Milliseconds{1U}).IsOk());
}

KB_TEST("contracts: pin, ADC, PWM, CAN, clock, delay and log references reach the port") {
  Fresh();
  stm::OutputConfig oc{};
  oc.pin = PinId{GpioPort::C, 0U, 0U};
  stm::OutputPin out{oc};
  KB_REQUIRE(out.EnsureReady() == Status::Ok);
  i::GpioOutputRef out_ref{out};
  KB_CHECK(out_ref.Set(i::Level::High) == Status::Ok);
  KB_CHECK(out_ref.Toggle() == Status::Ok);
  stm::InputConfig inc{};
  inc.pin = PinId{GpioPort::C, 1U, 0U};
  inc.pull = stm::Pull::Up;
  stm::InputPin in{inc};
  KB_REQUIRE(in.EnsureReady() == Status::Ok);
  i::GpioInputRef in_ref{in};
  i::Level level = i::Level::Low;
  KB_CHECK(in_ref.Get(level) == Status::Ok);
  KB_CHECK(level == i::Level::High);
  stm::AdcConfig ac{};
  stm::AdcChannel adc{ac};
  KB_REQUIRE(adc.EnsureReady() == Status::Ok);
  i::AdcChannelRef adc_ref{adc};
  KB_CHECK(adc_ref.ReadRaw().IsOk());
  KB_CHECK(adc_ref.ReadMillivolts().IsOk());
  KB_CHECK_EQ(adc_ref.FullScale(), 4095U);
  stm::PwmConfig pc{};
  pc.pin = PinId{GpioPort::B, 4U, 2U};
  stm::PwmChannel pwm{pc};
  KB_REQUIRE(pwm.EnsureReady() == Status::Ok);
  i::PwmOutputRef pwm_ref{pwm};
  KB_CHECK(pwm_ref.SetFrequencyHz(500U) == Status::Ok);
  KB_CHECK(pwm_ref.SetDuty(i::Duty{5000U}) == Status::Ok);
  KB_CHECK(pwm_ref.Enable(true) == Status::Ok);
  stm::CanConfig cc{};
  cc.tx = PinId{GpioPort::A, 12U, 9U};
  cc.rx = PinId{GpioPort::A, 11U, 9U};
  stm::CanController can{cc};
  i::CanBusRef can_ref{can};
  KB_CHECK(can_ref.EnsureReady() == Status::Ok);
  KB_CHECK(can_ref.Send(i::CanFrame{}, kb::Milliseconds{1U}) == Status::Ok);
  i::CanFrame f{};
  KB_CHECK(can_ref.Receive(f, kb::Milliseconds{1U}) == Status::Timeout);
  stm::Clock clock{};
  KB_REQUIRE(clock.Start() == Status::Ok);
  kb_fake_tim[2].CNT = 42U;
  const i::MonotonicClockRef clock_ref{clock};
  KB_CHECK((clock_ref.Now().Us() & 0xFFFFFFFFU) == 42U); // the wraps counted by earlier tests stay
  stm::BusyWait wait{};
  i::BusyWaitRef wait_ref{wait};
  wait_ref.WaitUs(kb::Microseconds{1U});
  stm::ItmLogSink itm{};
  i::LogSinkRef log_ref{itm};
  log_ref.Write(i::LogLevel::Info, "hello");
}

KB_TEST("contracts: an interrupt priority outside the RTOS-callable range is refused") {
  static_assert(stm::RtosIrqPriorityValid(stm::kMinRtosIrqPriority));
  static_assert(stm::RtosIrqPriorityValid(15U));
  static_assert(!stm::RtosIrqPriorityValid(16U));
  Fresh();
  stm::UartConfig u{};
  u.tx = PinId{GpioPort::A, 2U, 7U};
  u.rx = PinId{GpioPort::A, 3U, 7U};
  u.irq_priority = 16U;
  stm::Uart uart{u};
  KB_CHECK_EQ(uart.EnsureReady(), Status::InvalidArgument);
  stm::I2cConfig i{};
  i.scl = PinId{GpioPort::B, 8U, 4U};
  i.sda = PinId{GpioPort::B, 9U, 4U};
  i.irq_priority = 16U;
  stm::I2cController bus{i};
  KB_CHECK_EQ(bus.EnsureReady(), Status::InvalidArgument);
  stm::AdcConfig a{};
  a.channel = 1U;
  a.irq_priority = 7U; // configurable, the instance's first channel sets it
  stm::AdcChannel adc{a};
  KB_REQUIRE(adc.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(fake::IrqPriority(kF4 ? ADC_IRQn : ADC1_2_IRQn), 7U);
  a.irq_priority = 16U;
  stm::AdcChannel bad{a};
  KB_CHECK_EQ(bad.EnsureReady(), Status::InvalidArgument);
}
