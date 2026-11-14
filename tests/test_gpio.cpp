// OutputPin and InputPin over the fake HAL GPIO and EXTI: levels, the atomic toggle, claims, pulls
// and interrupts with their line sharing rule. Test code. SPDX-License-Identifier: MIT Copyright
// (c) 2026 The port-stm32 authors
#include "support.hpp"

namespace {
using kb::iface::Edge;
using kb::iface::Level;
constexpr int kPortC = 2;

void Count(void* context) noexcept {
  ++*static_cast<int*>(context);
}
} // namespace

KB_TEST("gpio: an output drives its initial level first, then sets and toggles atomically") {
  Fresh();
  stm::OutputConfig c{};
  c.pin = PinId{GpioPort::C, 7U, 0U};
  c.initial = Level::High;
  c.speed = stm::Speed::VeryHigh;
  stm::OutputPin pin{c};
  KB_CHECK_EQ(pin.Set(Level::Low), Status::NotReady);
  KB_CHECK_EQ(pin.Toggle(), Status::NotReady);
  KB_REQUIRE(pin.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(pin.EnsureReady(), Status::Ok);
  KB_CHECK(fake::ClockEnabled("GPIOC"));
  KB_CHECK_EQ(fake::ModeOf(kPortC, 7), GPIO_MODE_OUTPUT_PP);
  KB_CHECK_EQ(fake::Driven(kPortC, 7), 1);
  KB_CHECK_EQ(pin.Set(Level::Low), Status::Ok);
  KB_CHECK_EQ(fake::Driven(kPortC, 7), 0);
  KB_CHECK_EQ(pin.Toggle(), Status::Ok);
  KB_CHECK_EQ(fake::Driven(kPortC, 7), 1);
  KB_CHECK_EQ(fake::Toggles(), 1U);
  KB_CHECK_EQ(__get_PRIMASK(), 0U); // interrupts unmasked again after the toggle
  KB_CHECK(pin.LastError().IsOk());
  KB_CHECK_EQ(pin.Release(), Status::Ok);
  KB_CHECK_EQ(fake::ModeOf(kPortC, 7), GPIO_MODE_ANALOG);
  KB_CHECK_EQ(pin.Release(), Status::Ok);
}

KB_TEST("gpio: open drain with a pull-up, pull-down, speeds; claims and invalid pins") {
  Fresh();
  stm::OutputConfig c{};
  c.pin = PinId{GpioPort::C, 1U, 0U};
  c.open_drain = true;
  c.pull = stm::Pull::Up;
  c.speed = stm::Speed::Medium;
  stm::OutputPin od{c};
  KB_REQUIRE(od.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(fake::ModeOf(kPortC, 1), GPIO_MODE_OUTPUT_OD);
  KB_CHECK_EQ(fake::PullOf(kPortC, 1), GPIO_PULLUP);
  stm::OutputConfig d{};
  d.pin = PinId{GpioPort::C, 2U, 0U};
  d.pull = stm::Pull::Down;
  d.speed = stm::Speed::High;
  stm::OutputPin down{d};
  KB_REQUIRE(down.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(fake::PullOf(kPortC, 2), GPIO_PULLDOWN);
  stm::OutputPin second{c};
  KB_CHECK_EQ(second.EnsureReady(), Status::Busy);
  stm::InputConfig i{};
  i.pin = c.pin;
  stm::InputPin same{i};
  KB_CHECK_EQ(same.EnsureReady(), Status::Busy);
  stm::OutputConfig bad{};
  stm::OutputPin unused{bad};
  KB_CHECK_EQ(unused.EnsureReady(), Status::InvalidArgument);
  bad.pin = PinId{GpioPort::K, 1U, 0U};
  stm::OutputPin no_port{bad};
  KB_CHECK_EQ(no_port.EnsureReady(), Status::InvalidArgument);
  i.pin = PinId{GpioPort::C, 16U, 0U};
  stm::InputPin bad_in{i};
  KB_CHECK_EQ(bad_in.EnsureReady(), Status::InvalidArgument);
}

KB_TEST("gpio: an input reads the pin with its pull") {
  Fresh();
  stm::InputConfig c{};
  c.pin = PinId{GpioPort::C, 8U, 0U};
  c.pull = stm::Pull::Up;
  stm::InputPin pin{c};
  Level level = Level::Low;
  KB_CHECK_EQ(pin.Get(level), Status::NotReady);
  KB_REQUIRE(pin.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(pin.EnsureReady(), Status::Ok);
  KB_CHECK_EQ(fake::ModeOf(kPortC, 8), GPIO_MODE_INPUT);
  KB_CHECK_EQ(pin.Get(level), Status::Ok);
  KB_CHECK(level == Level::High); // the pull-up
  fake::ForceLevel(kPortC, 8, 0);
  KB_CHECK_EQ(pin.Get(level), Status::Ok);
  KB_CHECK(level == Level::Low);
  KB_CHECK(pin.LastError().IsOk());
}

KB_TEST("gpio: EXTI calls the callback on its line, edge by edge, until disabled") {
  Fresh();
  stm::InputConfig c{};
  c.pin = PinId{GpioPort::C, 13U, 0U};
  c.pull = stm::Pull::Up;
  c.irq_priority = 9U;
  stm::InputPin pin{c};
  int hits = 0;
  KB_CHECK_EQ(pin.EnableInterrupt(Edge::Falling, Count, &hits), Status::NotReady);
  KB_CHECK_EQ(pin.DisableInterrupt(), Status::NotReady);
  KB_REQUIRE(pin.EnsureReady() == Status::Ok);
  KB_CHECK_EQ(pin.EnableInterrupt(Edge::Falling, nullptr, &hits), Status::InvalidArgument);
  KB_CHECK_EQ(pin.DisableInterrupt(), Status::Ok);
  KB_CHECK_EQ(pin.EnableInterrupt(Edge::Falling, Count, &hits), Status::Ok);
  KB_CHECK_EQ(fake::ModeOf(kPortC, 13), GPIO_MODE_IT_FALLING);
  KB_CHECK_EQ(fake::PullOf(kPortC, 13), GPIO_PULLUP); // the pull is kept
  KB_CHECK(fake::IrqEnabled(EXTI15_10_IRQn));
  KB_CHECK_EQ(fake::IrqPriority(EXTI15_10_IRQn), 9U);
  KB_CHECK(fake::ClockEnabled("SYSCFG"));
  fake::ExtiPending(13);
  stm::HandleExtiIrq(10U, 15U);
  KB_CHECK_EQ(hits, 1);
  KB_CHECK_EQ(kb_fake_exti_pr, 0U); // cleared
  stm::HandleExtiIrq(10U, 15U);
  KB_CHECK_EQ(hits, 1);
  KB_CHECK_EQ(pin.EnableInterrupt(Edge::Rising, Count, &hits), Status::Ok);
  KB_CHECK_EQ(fake::ModeOf(kPortC, 13), GPIO_MODE_IT_RISING);
  KB_CHECK_EQ(pin.EnableInterrupt(Edge::Both, Count, &hits), Status::Ok);
  KB_CHECK_EQ(fake::ModeOf(kPortC, 13), GPIO_MODE_IT_RISING_FALLING);
  KB_CHECK_EQ(pin.DisableInterrupt(), Status::Ok);
  KB_CHECK_EQ(fake::ModeOf(kPortC, 13), GPIO_MODE_INPUT);
  fake::ExtiPending(13);
  stm::HandleExtiIrq(10U, 15U);
  KB_CHECK_EQ(hits, 1);
  KB_CHECK_EQ(kb_fake_exti_pr, 0U); // a pending line without a callback is still cleared
}

KB_TEST("gpio: one EXTI line per pin number; every line range maps to its IRQ") {
  Fresh();
  stm::InputConfig a{};
  a.pin = PinId{GpioPort::A, 5U, 0U};
  stm::InputConfig b{};
  b.pin = PinId{GpioPort::B, 5U, 0U};
  stm::InputPin pa{a};
  stm::InputPin pb{b};
  KB_REQUIRE(pa.EnsureReady() == Status::Ok);
  KB_REQUIRE(pb.EnsureReady() == Status::Ok);
  int hits = 0;
  KB_CHECK_EQ(pa.EnableInterrupt(Edge::Rising, Count, &hits), Status::Ok);
  KB_CHECK_EQ(pb.EnableInterrupt(Edge::Rising, Count, &hits), Status::Busy);
  KB_CHECK(fake::IrqEnabled(EXTI9_5_IRQn));
  KB_CHECK_EQ(pa.Release(), Status::Ok);
  KB_CHECK_EQ(pb.EnableInterrupt(Edge::Rising, Count, &hits), Status::Ok);
  const std::array<IRQn_Type, 5> low{EXTI0_IRQn, EXTI1_IRQn, EXTI2_IRQn, EXTI3_IRQn, EXTI4_IRQn};
  for (uint8_t line = 0U; line < 5U; ++line) {
    stm::InputConfig c{};
    c.pin = PinId{GpioPort::D, line, 0U};
    stm::InputPin p{c};
    KB_REQUIRE(p.EnsureReady() == Status::Ok);
    KB_CHECK_EQ(p.EnableInterrupt(Edge::Falling, Count, &hits), Status::Ok);
    KB_CHECK(fake::IrqEnabled(low[line]));
    fake::ExtiPending(line);
    stm::HandleExtiIrq(line, line);
  }
  KB_CHECK_EQ(hits, 5);
  stm::HandleExtiIrq(14U, 20U); // lines past 15 are ignored
}
